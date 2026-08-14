#include "gsave/repository/lua_metadata.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string_view>
#include <vector>

namespace gsave::repository {
namespace {

constexpr std::size_t lua_memory_limit = 16U * 1024U * 1024U;
constexpr std::size_t read_limit = 4U * 1024U * 1024U;
constexpr std::size_t total_read_limit = 16U * 1024U * 1024U;
constexpr std::size_t metadata_limit = 64U * 1024U;
constexpr std::size_t file_count_limit = 4096;
constexpr int instruction_hook_granularity = 10'000;
constexpr int instruction_limit = 4'000'000;

struct AllocatorState final {
    std::size_t used{};
};

struct ParserContext final {
    std::filesystem::path root;
    std::size_t bytes_read{};
    int instructions{};
};

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path path_from_utf8(
    const char* data,
    const std::size_t size) {
    return std::filesystem::path{std::u8string{
        reinterpret_cast<const char8_t*>(data),
        reinterpret_cast<const char8_t*>(data + size)}};
}

void* limited_allocator(
    void* userdata,
    void* pointer,
    const std::size_t old_size,
    const std::size_t new_size) noexcept {
    auto& allocator = *static_cast<AllocatorState*>(userdata);
    if (new_size == 0) {
        if (pointer != nullptr) {
            allocator.used -= std::min(allocator.used, old_size);
            std::free(pointer);
        }
        return nullptr;
    }
    const auto retained = pointer == nullptr ? 0U : old_size;
    if (new_size > lua_memory_limit || allocator.used - std::min(allocator.used, retained)
            > lua_memory_limit - new_size) {
        return nullptr;
    }
    void* resized = std::realloc(pointer, new_size);
    if (resized != nullptr) {
        allocator.used = allocator.used - std::min(allocator.used, retained) + new_size;
    }
    return resized;
}

[[nodiscard]] ParserContext& parser_context(lua_State* state) {
    return *static_cast<ParserContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

void instruction_hook(lua_State* state, lua_Debug*) {
    lua_getfield(state, LUA_REGISTRYINDEX, "gsave.parser_context");
    auto* context = static_cast<ParserContext*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (context == nullptr) {
        luaL_error(state, "missing parser context");
        return;
    }
    context->instructions += instruction_hook_granularity;
    if (context->instructions > instruction_limit) {
        luaL_error(state, "package parser instruction limit exceeded");
    }
}

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()
        || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == L".." || component == L".") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path checked_path(
    lua_State* state,
    ParserContext& context,
    const int argument) {
    std::size_t size = 0;
    const char* text = luaL_checklstring(state, argument, &size);
    auto relative = path_from_utf8(text, size).lexically_normal();
    if (!safe_relative_path(relative)) {
        luaL_error(state, "repository path must be safe and relative");
    }
    auto absolute = context.root / relative;
    std::error_code error;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(absolute, error))) {
        luaL_error(state, "symbolic links are not readable by package parsers");
    }
    return absolute;
}

int repository_files(lua_State* state) {
    try {
        auto& context = parser_context(state);
        lua_newtable(state);
        std::error_code error;
        std::size_t index = 1;
        for (std::filesystem::recursive_directory_iterator iterator{
                 context.root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error}, end;
             iterator != end && !error; iterator.increment(error)) {
            const auto relative = iterator->path().lexically_relative(context.root);
            if (!relative.empty() && relative.begin()->native() == L".git") {
                if (iterator->is_directory(error)) {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (iterator->is_symlink(error)) {
                if (iterator->is_directory(error)) {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (!iterator->is_regular_file(error)) {
                continue;
            }
            if (index > file_count_limit) {
                return luaL_error(state, "repository file count exceeds parser limit");
            }
            const auto name = path_utf8(relative);
            lua_pushlstring(state, name.data(), name.size());
            lua_rawseti(state, -2, static_cast<lua_Integer>(index++));
        }
        if (error) {
            return luaL_error(state, "cannot enumerate repository files");
        }
        return 1;
    } catch (...) {
        return luaL_error(state, "cannot enumerate repository files");
    }
}

int repository_read(lua_State* state) {
    try {
        auto& context = parser_context(state);
        const auto path = checked_path(state, context, 2);
        const auto offset = luaL_checkinteger(state, 3);
        const auto length = luaL_checkinteger(state, 4);
        if (offset < 0 || length < 0 || static_cast<std::size_t>(length) > read_limit
            || context.bytes_read > total_read_limit - static_cast<std::size_t>(length)) {
            return luaL_error(state, "repository read is outside parser limits");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            lua_pushnil(state);
            return 1;
        }
        input.seekg(offset, std::ios::beg);
        if (!input) {
            lua_pushnil(state);
            return 1;
        }
        std::string bytes(static_cast<std::size_t>(length), '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        context.bytes_read += bytes.size();
        lua_pushlstring(state, bytes.data(), bytes.size());
        return 1;
    } catch (...) {
        return luaL_error(state, "repository read failed");
    }
}

int repository_stat(lua_State* state) {
    try {
        auto& context = parser_context(state);
        const auto path = checked_path(state, context, 2);
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            lua_pushnil(state);
            return 1;
        }
        const auto modified = std::filesystem::last_write_time(path, error);
        if (error) {
            lua_pushnil(state);
            return 1;
        }
        lua_createtable(state, 0, 2);
        lua_pushinteger(state, static_cast<lua_Integer>(size));
        lua_setfield(state, -2, "size");
        lua_pushinteger(state, static_cast<lua_Integer>(modified.time_since_epoch().count()));
        lua_setfield(state, -2, "modified_unix_ns");
        return 1;
    } catch (...) {
        return luaL_error(state, "repository stat failed");
    }
}

[[nodiscard]] bool nt_success(const NTSTATUS status) noexcept {
    return status >= 0;
}

int repository_aes_decrypt(lua_State* state) {
    std::size_t ciphertext_size = 0;
    std::size_t key_size = 0;
    std::size_t iv_size = 0;
    const auto* ciphertext = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 2, &ciphertext_size));
    const auto* key = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 3, &key_size));
    const auto* iv = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 4, &iv_size));
    if (key_size != 16 || iv_size != 16 || ciphertext_size == 0
        || ciphertext_size % 16 != 0 || ciphertext_size > ULONG_MAX) {
        return luaL_error(state, "invalid AES-128-CBC input");
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    const auto cleanup = [&]() noexcept {
        if (key_handle != nullptr) BCryptDestroyKey(key_handle);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    const auto fail = [&](const char* message) {
        cleanup();
        return luaL_error(state, "%s", message);
    };
    if (!nt_success(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        return fail("cannot open AES provider");
    }
    if (!nt_success(BCryptSetProperty(
            algorithm, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
            sizeof(BCRYPT_CHAIN_MODE_CBC), 0))) {
        return fail("cannot select AES-CBC");
    }
    DWORD object_size = 0;
    DWORD written = 0;
    if (!nt_success(BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &written, 0))) {
        return fail("cannot query AES provider");
    }
    std::vector<unsigned char> key_object(object_size);
    if (!nt_success(BCryptGenerateSymmetricKey(
            algorithm, &key_handle, key_object.data(), object_size,
            const_cast<PUCHAR>(key), static_cast<ULONG>(key_size), 0))) {
        return fail("cannot create AES key");
    }
    std::vector<unsigned char> mutable_iv(iv, iv + iv_size);
    std::vector<unsigned char> plaintext(ciphertext_size);
    ULONG plaintext_size = 0;
    if (!nt_success(BCryptDecrypt(
            key_handle, const_cast<PUCHAR>(ciphertext),
            static_cast<ULONG>(ciphertext_size), nullptr, mutable_iv.data(),
            static_cast<ULONG>(mutable_iv.size()), plaintext.data(),
            static_cast<ULONG>(plaintext.size()), &plaintext_size, 0))) {
        return fail("AES decryption failed");
    }
    cleanup();
    lua_pushlstring(
        state, reinterpret_cast<const char*>(plaintext.data()), plaintext_size);
    return 1;
}

int repository_md5(lua_State* state) {
    std::size_t input_size = 0;
    const auto* input = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 2, &input_size));
    if (input_size > ULONG_MAX) {
        return luaL_error(state, "MD5 input is too large");
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    const auto cleanup = [&]() noexcept {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    const auto fail = [&](const char* message) {
        cleanup();
        return luaL_error(state, "%s", message);
    };
    if (!nt_success(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0))) {
        return fail("cannot open MD5 provider");
    }
    DWORD object_size = 0;
    DWORD digest_size = 0;
    DWORD written = 0;
    if (!nt_success(BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size), &written, 0))
        || !nt_success(BCryptGetProperty(
            algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_size),
            sizeof(digest_size), &written, 0))) {
        return fail("cannot query MD5 provider");
    }
    std::vector<unsigned char> object(object_size);
    std::vector<unsigned char> digest(digest_size);
    if (!nt_success(BCryptCreateHash(
            algorithm, &hash, object.data(), object_size, nullptr, 0, 0))
        || !nt_success(BCryptHashData(
            hash, const_cast<PUCHAR>(input), static_cast<ULONG>(input_size), 0))
        || !nt_success(BCryptFinishHash(
            hash, digest.data(), digest_size, 0))) {
        return fail("MD5 hashing failed");
    }
    cleanup();
    lua_pushlstring(
        state, reinterpret_cast<const char*>(digest.data()), digest.size());
    return 1;
}

void add_repository_function(
    lua_State* state,
    ParserContext& context,
    const char* name,
    lua_CFunction function) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void push_repository(lua_State* state, ParserContext& context) {
    lua_createtable(state, 0, 6);
    const auto root = path_utf8(context.root);
    lua_pushlstring(state, root.data(), root.size());
    lua_setfield(state, -2, "path");
    add_repository_function(state, context, "files", repository_files);
    add_repository_function(state, context, "read", repository_read);
    add_repository_function(state, context, "stat", repository_stat);
    add_repository_function(state, context, "aes_128_cbc_decrypt", repository_aes_decrypt);
    add_repository_function(state, context, "md5", repository_md5);
}

void open_sandbox_libraries(lua_State* state) {
    luaL_requiref(state, LUA_GNAME, luaopen_base, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(state, 1);
    for (const char* name : {"dofile", "loadfile", "load", "require"}) {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }
}

void append_json_string(std::string& output, const std::string_view text) {
    output.push_back('"');
    constexpr char hexadecimal[] = "0123456789abcdef";
    for (const unsigned char character : text) {
        switch (character) {
        case '"': output.append("\\\""); break;
        case '\\': output.append("\\\\"); break;
        case '\b': output.append("\\b"); break;
        case '\f': output.append("\\f"); break;
        case '\n': output.append("\\n"); break;
        case '\r': output.append("\\r"); break;
        case '\t': output.append("\\t"); break;
        default:
            if (character < 0x20) {
                output.append("\\u00");
                output.push_back(hexadecimal[character >> 4]);
                output.push_back(hexadecimal[character & 0x0F]);
            } else {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    output.push_back('"');
}

struct LuaKey final {
    bool integer{};
    lua_Integer number{};
    std::string text;
};

[[nodiscard]] bool serialize_lua_value(
    lua_State* state,
    int index,
    std::string& output,
    std::set<const void*>& active_tables,
    const unsigned depth,
    std::string& failure) {
    if (output.size() > metadata_limit || depth > 8) {
        failure = "metadata exceeds serialization limits";
        return false;
    }
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        output.append("null");
        return true;
    case LUA_TBOOLEAN:
        output.append(lua_toboolean(state, index) ? "true" : "false");
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(state, index)) {
            output.append(std::to_string(lua_tointeger(state, index)));
        } else {
            char buffer[64]{};
            const int length = std::snprintf(
                buffer, sizeof(buffer), "%.17g", lua_tonumber(state, index));
            if (length <= 0) {
                failure = "cannot serialize metadata number";
                return false;
            }
            output.append(buffer, static_cast<std::size_t>(length));
        }
        return true;
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char* value = lua_tolstring(state, index, &length);
        append_json_string(output, {value, length});
        return true;
    }
    case LUA_TTABLE: {
        const void* identity = lua_topointer(state, index);
        if (!active_tables.emplace(identity).second) {
            failure = "metadata contains a table cycle";
            return false;
        }
        std::vector<LuaKey> keys;
        lua_pushnil(state);
        while (lua_next(state, index) != 0) {
            if (lua_type(state, -2) == LUA_TSTRING) {
                std::size_t length = 0;
                const char* value = lua_tolstring(state, -2, &length);
                keys.push_back(LuaKey{.text = std::string(value, length)});
            } else if (lua_isinteger(state, -2)) {
                const auto number = lua_tointeger(state, -2);
                keys.push_back(LuaKey{
                    .integer = true,
                    .number = number,
                    .text = std::to_string(number),
                });
            } else {
                lua_pop(state, 2);
                active_tables.erase(identity);
                failure = "metadata table keys must be strings or integers";
                return false;
            }
            lua_pop(state, 1);
        }
        std::sort(keys.begin(), keys.end(), [](const LuaKey& left, const LuaKey& right) {
            return left.text < right.text;
        });
        output.push_back('{');
        bool first = true;
        for (const auto& key : keys) {
            if (!first) output.push_back(',');
            first = false;
            append_json_string(output, key.text);
            output.push_back(':');
            if (key.integer) {
                lua_geti(state, index, key.number);
            } else {
                lua_getfield(state, index, key.text.c_str());
            }
            const bool valid = serialize_lua_value(
                state, -1, output, active_tables, depth + 1, failure);
            lua_pop(state, 1);
            if (!valid) {
                active_tables.erase(identity);
                return false;
            }
        }
        output.push_back('}');
        active_tables.erase(identity);
        return true;
    }
    default:
        failure = "metadata contains an unsupported Lua value";
        return false;
    }
}

}  // namespace

Result<std::string> parse_metadata(const MetadataRequest& request) {
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(request.repository, filesystem_error)) {
        return std::unexpected(make_error(
            filesystem_error ? filesystem_error : std::make_error_code(std::errc::not_a_directory),
            "metadata repository is unavailable"));
    }
    filesystem_error.clear();
    if (!std::filesystem::is_regular_file(request.parser, filesystem_error)) {
        return std::unexpected(make_error(
            filesystem_error ? filesystem_error : std::make_error_code(std::errc::no_such_file_or_directory),
            "metadata parser is unavailable"));
    }

    AllocatorState allocator;
    lua_State* state = lua_newstate(limited_allocator, &allocator);
    if (state == nullptr) {
        return std::unexpected(make_error(
            std::errc::not_enough_memory, "cannot create package parser state"));
    }
    ParserContext context{.root = request.repository};
    lua_pushlightuserdata(state, &context);
    lua_setfield(state, LUA_REGISTRYINDEX, "gsave.parser_context");
    lua_sethook(state, instruction_hook, LUA_MASKCOUNT, instruction_hook_granularity);
    open_sandbox_libraries(state);

    const auto parser_text = request.parser.string();
    if (luaL_loadfilex(state, parser_text.c_str(), "t") != LUA_OK
        || lua_pcall(state, 0, 0, 0) != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        const auto error = make_error(
            std::errc::invalid_argument,
            std::string{"package parser load failed: "}
                + (message == nullptr ? "unknown Lua error" : message));
        lua_close(state);
        return std::unexpected(error);
    }

    lua_getglobal(state, "parse");
    if (!lua_isfunction(state, -1)) {
        lua_close(state);
        return std::unexpected(make_error(
            std::errc::invalid_argument, "package parser does not export parse()"));
    }
    push_repository(state, context);
    lua_createtable(state, static_cast<int>(request.changed_files.size()), 0);
    for (std::size_t index = 0; index < request.changed_files.size(); ++index) {
        const auto& path = request.changed_files[index];
        lua_pushlstring(state, path.data(), path.size());
        lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    if (lua_pcall(state, 2, 1, 0) != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        const auto error = make_error(
            std::errc::invalid_argument,
            std::string{"package parse() failed: "}
                + (message == nullptr ? "unknown Lua error" : message));
        lua_close(state);
        return std::unexpected(error);
    }

    std::string result;
    result.reserve(4096);
    std::set<const void*> active_tables;
    std::string failure;
    if (!serialize_lua_value(state, -1, result, active_tables, 0, failure)
        || result.size() > metadata_limit) {
        lua_close(state);
        return std::unexpected(make_error(
            std::errc::value_too_large,
            failure.empty() ? "metadata exceeds serialization limit" : failure));
    }
    lua_close(state);
    return result;
}

}  // namespace gsave::repository
