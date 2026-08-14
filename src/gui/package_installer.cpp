#include "gsave/gui/gui_model.hpp"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gsave::gui {
namespace {

constexpr std::size_t lua_memory_limit = 16U * 1024U * 1024U;

struct LuaAllocator final {
    std::size_t used{};
};

void* limited_allocator(
    void* user,
    void* memory,
    const std::size_t old_size,
    const std::size_t new_size) {
    auto& allocator = *static_cast<LuaAllocator*>(user);
    const std::size_t previous = memory == nullptr ? 0 : old_size;
    if (new_size == 0) {
        std::free(memory);
        allocator.used = previous > allocator.used ? 0 : allocator.used - previous;
        return nullptr;
    }
    if (new_size > previous
        && new_size - previous > lua_memory_limit - std::min(allocator.used, lua_memory_limit)) {
        return nullptr;
    }
    void* resized = std::realloc(memory, new_size);
    if (resized != nullptr) {
        allocator.used = allocator.used - std::min(previous, allocator.used) + new_size;
    }
    return resized;
}

void instruction_limit(lua_State* state, lua_Debug*) {
    luaL_error(state, "support package install exceeded its instruction limit");
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path utf8_path(const std::string_view value) {
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path{std::u8string{first, first + value.size()}};
}

void push_path(lua_State* state, const std::filesystem::path& path) {
    const auto text = path_utf8(path);
    lua_pushlstring(state, text.data(), text.size());
}

[[nodiscard]] std::optional<std::wstring> registry_text(
    HKEY root,
    const wchar_t* key,
    const wchar_t* name,
    const REGSAM view = 0) {
    HKEY opened = nullptr;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE | view, &opened) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG measured = RegQueryValueExW(
        opened, name, nullptr, &type, nullptr, &bytes);
    if (measured != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)
        || bytes < sizeof(wchar_t)) {
        RegCloseKey(opened);
        return std::nullopt;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LONG read = RegQueryValueExW(
        opened, name, nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(opened);
    if (read != ERROR_SUCCESS) return std::nullopt;
    value.resize(wcsnlen(value.c_str(), value.size()));
    if (type == REG_EXPAND_SZ) {
        const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (required != 0) {
            std::wstring expanded(required, L'\0');
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), required) != 0) {
                expanded.resize(wcsnlen(expanded.c_str(), expanded.size()));
                value = std::move(expanded);
            }
        }
    }
    return value.empty() ? std::nullopt : std::optional<std::wstring>{std::move(value)};
}

[[nodiscard]] std::optional<std::filesystem::path> steam_root() {
    if (auto value = registry_text(
            HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) {
        return std::filesystem::path{*value};
    }
    if (auto value = registry_text(
            HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", L"InstallPath",
            KEY_WOW64_32KEY)) {
        return std::filesystem::path{*value};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::string> vdf_values(
    const std::string_view source,
    const std::string_view requested_key) {
    std::string lower_source{source};
    std::ranges::transform(lower_source, lower_source.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    std::string key{requested_key};
    std::ranges::transform(key, key.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    key = '"' + key + '"';

    std::vector<std::string> result;
    std::size_t cursor = 0;
    while ((cursor = lower_source.find(key, cursor)) != std::string::npos) {
        const auto value_start = source.find('"', cursor + key.size());
        if (value_start == std::string_view::npos) break;
        const auto value_end = source.find('"', value_start + 1);
        if (value_end == std::string_view::npos) break;
        std::string value{source.substr(value_start + 1, value_end - value_start - 1)};
        for (std::size_t slash = 0; (slash = value.find("\\\\", slash)) != std::string::npos;) {
            value.replace(slash, 2, "\\");
            ++slash;
        }
        result.push_back(std::move(value));
        cursor = value_end + 1;
    }
    return result;
}

[[nodiscard]] std::optional<std::filesystem::path> find_steam_executable(
    const std::int64_t app_id,
    const std::filesystem::path& relative) {
    const auto root = steam_root();
    if (!root) return std::nullopt;
    std::vector<std::filesystem::path> libraries{*root};
    if (const auto folders = read_text(*root / L"steamapps" / L"libraryfolders.vdf")) {
        for (const auto& value : vdf_values(*folders, "path")) {
            auto candidate = utf8_path(value);
            if (std::ranges::find(libraries, candidate) == libraries.end()) {
                libraries.push_back(std::move(candidate));
            }
        }
    }
    for (const auto& library : libraries) {
        const auto manifest = read_text(
            library / L"steamapps" / (L"appmanifest_" + std::to_wstring(app_id) + L".acf"));
        if (!manifest) continue;
        const auto install_dirs = vdf_values(*manifest, "installdir");
        if (install_dirs.empty()) continue;
        const auto executable = library / L"steamapps" / L"common"
            / utf8_path(install_dirs.front()) / relative;
        std::error_code error;
        if (std::filesystem::is_regular_file(executable, error)) return executable;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::filesystem::path> find_steam_userdata(
    const std::int64_t app_id) {
    std::vector<std::filesystem::path> result;
    const auto root = steam_root();
    if (!root) return result;
    const auto userdata = *root / L"userdata";
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{userdata, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_directory(error) || error) continue;
        const auto remote = iterator->path() / std::to_wstring(app_id) / L"remote";
        if (std::filesystem::is_directory(remote, error) && !error) {
            result.push_back(remote);
        }
        error.clear();
    }
    std::ranges::sort(result);
    return result;
}

int context_known_folder(lua_State* state) {
    const std::string_view name = luaL_checkstring(state, 1);
    REFKNOWNFOLDERID id = name == "roaming_app_data"
        ? FOLDERID_RoamingAppData : FOLDERID_LocalAppData;
    if (name != "roaming_app_data" && name != "local_app_data") {
        lua_pushnil(state);
        return 1;
    }
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value))) {
        lua_pushnil(state);
        return 1;
    }
    push_path(state, value);
    CoTaskMemFree(value);
    return 1;
}

int context_steam_executable(lua_State* state) {
    try {
        const auto app_id = static_cast<std::int64_t>(luaL_checkinteger(state, 1));
        const auto relative = utf8_path(luaL_checkstring(state, 2));
        const auto executable = find_steam_executable(app_id, relative);
        if (!executable) lua_pushnil(state);
        else push_path(state, *executable);
        return 1;
    } catch (...) {
        lua_pushnil(state);
        return 1;
    }
}

int context_steam_userdata(lua_State* state) {
    try {
        const auto app_id = static_cast<std::int64_t>(luaL_checkinteger(state, 1));
        const auto paths = find_steam_userdata(app_id);
        lua_createtable(state, static_cast<int>(paths.size()), 0);
        for (std::size_t index = 0; index < paths.size(); ++index) {
            push_path(state, paths[index]);
            lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
        }
        return 1;
    } catch (...) {
        lua_newtable(state);
        return 1;
    }
}

int context_path_join(lua_State* state) {
    std::filesystem::path result;
    const int count = lua_gettop(state);
    for (int index = 1; index <= count; ++index) {
        result /= utf8_path(luaL_checkstring(state, index));
    }
    push_path(state, result.lexically_normal());
    return 1;
}

int context_is_file(lua_State* state) {
    std::error_code error;
    lua_pushboolean(state, std::filesystem::is_regular_file(
        utf8_path(luaL_checkstring(state, 1)), error));
    return 1;
}

int context_is_directory(lua_State* state) {
    std::error_code error;
    lua_pushboolean(state, std::filesystem::is_directory(
        utf8_path(luaL_checkstring(state, 1)), error));
    return 1;
}

int context_list_directories(lua_State* state) {
    const auto root = utf8_path(luaL_checkstring(state, 1));
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{root, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_directory(error) && !error) paths.push_back(iterator->path());
    }
    std::ranges::sort(paths);
    lua_createtable(state, static_cast<int>(paths.size()), 0);
    for (std::size_t index = 0; index < paths.size(); ++index) {
        push_path(state, paths[index]);
        lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

int context_basename(lua_State* state) {
    push_path(state, utf8_path(luaL_checkstring(state, 1)).filename());
    return 1;
}

void set_context_function(lua_State* state, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

void open_install_libraries(lua_State* state) {
    const struct Library { const char* name; lua_CFunction open; } libraries[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
    };
    for (const auto& library : libraries) {
        luaL_requiref(state, library.name, library.open, 1);
        lua_pop(state, 1);
    }
    for (const char* removed : {"dofile", "loadfile", "load", "collectgarbage"}) {
        lua_pushnil(state);
        lua_setglobal(state, removed);
    }
}

[[nodiscard]] std::optional<std::string> table_string(
    lua_State* state,
    const int table,
    const char* key) {
    lua_getfield(state, table, key);
    std::optional<std::string> result;
    if (lua_type(state, -1) == LUA_TSTRING) {
        std::size_t length = 0;
        const char* value = lua_tolstring(state, -1, &length);
        result.emplace(value, length);
    }
    lua_pop(state, 1);
    return result;
}

[[nodiscard]] std::vector<std::string> table_string_array(
    lua_State* state,
    const int table,
    const char* key) {
    std::vector<std::string> result;
    lua_getfield(state, table, key);
    if (lua_istable(state, -1)) {
        const auto count = lua_rawlen(state, -1);
        result.reserve(count);
        for (std::size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
            if (lua_type(state, -1) == LUA_TSTRING) {
                std::size_t length = 0;
                const char* value = lua_tolstring(state, -1, &length);
                if (length != 0) result.emplace_back(value, length);
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    return result;
}

[[nodiscard]] Error lua_error(lua_State* state, const std::string_view context) {
    const char* message = lua_tostring(state, -1);
    return make_error(
        std::errc::invalid_argument,
        std::string{context} + (message == nullptr ? "" : ": " + std::string{message}));
}

}  // namespace

Result<PackageInstallDetection> detect_package_install(
    const PackageManifest& package) {
    LuaAllocator allocator;
    lua_State* state = lua_newstate(limited_allocator, &allocator);
    if (state == nullptr) {
        return std::unexpected(make_error(
            std::errc::not_enough_memory, "cannot create support package install sandbox"));
    }
    const auto close = [&] { lua_close(state); };
    open_install_libraries(state);
    lua_sethook(state, instruction_limit, LUA_MASKCOUNT, 2'000'000);

    const auto adapter = path_utf8(package.adapter);
    if (luaL_loadfilex(state, adapter.c_str(), nullptr) != LUA_OK
        || lua_pcall(state, 0, 0, 0) != LUA_OK) {
        const auto error = lua_error(state, "cannot load support package adapter");
        close();
        return std::unexpected(error);
    }
    lua_getglobal(state, "install");
    if (!lua_isfunction(state, -1)) {
        close();
        return std::unexpected(make_error(
            std::errc::invalid_argument, "support package does not export install(context)"));
    }
    lua_createtable(state, 0, 7);
    set_context_function(state, "known_folder", context_known_folder);
    set_context_function(state, "steam_executable", context_steam_executable);
    set_context_function(state, "steam_userdata", context_steam_userdata);
    set_context_function(state, "path_join", context_path_join);
    set_context_function(state, "is_file", context_is_file);
    set_context_function(state, "is_directory", context_is_directory);
    set_context_function(state, "list_directories", context_list_directories);
    set_context_function(state, "basename", context_basename);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
        const auto error = lua_error(state, "support package install failed");
        close();
        return std::unexpected(error);
    }
    if (!lua_istable(state, -1)) {
        close();
        return std::unexpected(make_error(
            std::errc::invalid_argument, "support package install must return a table"));
    }

    PackageInstallDetection result;
    if (auto name = table_string(state, -1, "process_name")) {
        result.process_name = std::move(*name);
    }
    if (auto path = table_string(state, -1, "process_path")) {
        result.process_path = utf8_path(*path);
    }

    lua_getfield(state, -1, "repositories");
    if (lua_istable(state, -1)) {
        const auto count = lua_rawlen(state, -1);
        for (std::size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
            if (lua_istable(state, -1)) {
                if (auto path = table_string(state, -1, "path")) {
                    auto includes = table_string_array(state, -1, "include_globs");
                    auto excludes = table_string_array(state, -1, "exclude_globs");
                    if (includes.empty()) includes = package.watch_include_patterns;
                    if (excludes.empty()) excludes = package.watch_exclude_patterns;
                    result.repositories.push_back(InstallRepository{
                        .path = utf8_path(*path),
                        .include_globs = std::move(includes),
                        .exclude_globs = std::move(excludes),
                    });
                }
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, -1, "problems");
    if (lua_istable(state, -1)) {
        const auto count = lua_rawlen(state, -1);
        for (std::size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
            if (lua_istable(state, -1)) {
                auto message = table_string(state, -1, "message");
                auto code = table_string(state, -1, "code");
                if (message) result.problems.push_back(std::move(*message));
                else if (code) result.problems.push_back(std::move(*code));
            } else if (lua_type(state, -1) == LUA_TSTRING) {
                result.problems.emplace_back(lua_tostring(state, -1));
            }
            lua_pop(state, 1);
        }
    }
    close();
    return result;
}

}  // namespace gsave::gui
