extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#endif

#include <cstddef>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

#ifdef _WIN32
[[nodiscard]] bool nt_success(const NTSTATUS status) noexcept {
    return status >= 0;
}

int aes_128_cbc_decrypt(lua_State* state) {
    std::size_t ciphertext_size = 0;
    std::size_t key_size = 0;
    std::size_t iv_size = 0;
    const auto* ciphertext = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 1, &ciphertext_size));
    const auto* key = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 2, &key_size));
    const auto* iv = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 3, &iv_size));

    if (key_size != 16 || iv_size != 16 || ciphertext_size == 0
        || ciphertext_size % 16 != 0) {
        return luaL_error(
            state,
            "AES-128-CBC requires a 16-byte key/IV and block-aligned ciphertext");
    }
    if (ciphertext_size > static_cast<std::size_t>(ULONG_MAX)) {
        return luaL_error(state, "AES ciphertext is too large");
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    const auto cleanup = [&]() noexcept {
        if (key_handle != nullptr) {
            BCryptDestroyKey(key_handle);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    };
    const auto fail = [&](const char* message) {
        cleanup();
        return luaL_error(state, "%s", message);
    };

    if (!nt_success(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        return fail("BCryptOpenAlgorithmProvider(AES) failed");
    }
    if (!nt_success(BCryptSetProperty(
            algorithm,
            BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
            sizeof(BCRYPT_CHAIN_MODE_CBC),
            0))) {
        return fail("BCryptSetProperty(CBC) failed");
    }

    DWORD object_size = 0;
    DWORD bytes_written = 0;
    if (!nt_success(BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size),
            &bytes_written,
            0))) {
        return fail("BCryptGetProperty(OBJECT_LENGTH) failed");
    }

    std::vector<unsigned char> key_object(object_size);
    if (!nt_success(BCryptGenerateSymmetricKey(
            algorithm,
            &key_handle,
            key_object.data(),
            object_size,
            const_cast<PUCHAR>(key),
            static_cast<ULONG>(key_size),
            0))) {
        return fail("BCryptGenerateSymmetricKey failed");
    }

    std::vector<unsigned char> mutable_iv(iv, iv + iv_size);
    std::vector<unsigned char> plaintext(ciphertext_size);
    ULONG plaintext_size = 0;
    if (!nt_success(BCryptDecrypt(
            key_handle,
            const_cast<PUCHAR>(ciphertext),
            static_cast<ULONG>(ciphertext_size),
            nullptr,
            mutable_iv.data(),
            static_cast<ULONG>(mutable_iv.size()),
            plaintext.data(),
            static_cast<ULONG>(plaintext.size()),
            &plaintext_size,
            0))) {
        return fail("BCryptDecrypt failed");
    }

    cleanup();
    lua_pushlstring(
        state,
        reinterpret_cast<const char*>(plaintext.data()),
        plaintext_size);
    return 1;
}

int md5(lua_State* state) {
    std::size_t input_size = 0;
    const auto* input = reinterpret_cast<const unsigned char*>(
        luaL_checklstring(state, 1, &input_size));
    if (input_size > static_cast<std::size_t>(ULONG_MAX)) {
        return luaL_error(state, "MD5 input is too large");
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    const auto cleanup = [&]() noexcept {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    };
    const auto fail = [&](const char* message) {
        cleanup();
        return luaL_error(state, "%s", message);
    };

    if (!nt_success(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0))) {
        return fail("BCryptOpenAlgorithmProvider(MD5) failed");
    }
    DWORD object_size = 0;
    DWORD digest_size = 0;
    DWORD bytes_written = 0;
    if (!nt_success(BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size),
            &bytes_written,
            0))
        || !nt_success(BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digest_size),
            sizeof(digest_size),
            &bytes_written,
            0))) {
        return fail("BCryptGetProperty(MD5) failed");
    }

    std::vector<unsigned char> object(object_size);
    std::vector<unsigned char> digest(digest_size);
    if (!nt_success(BCryptCreateHash(
            algorithm, &hash, object.data(), object_size, nullptr, 0, 0))) {
        return fail("BCryptCreateHash(MD5) failed");
    }
    if (!nt_success(BCryptHashData(
            hash,
            const_cast<PUCHAR>(input),
            static_cast<ULONG>(input_size),
            0))) {
        return fail("BCryptHashData(MD5) failed");
    }
    if (!nt_success(BCryptFinishHash(
            hash, digest.data(), digest_size, 0))) {
        return fail("BCryptFinishHash(MD5) failed");
    }

    cleanup();
    lua_pushlstring(
        state,
        reinterpret_cast<const char*>(digest.data()),
        digest.size());
    return 1;
}
#else
int aes_128_cbc_decrypt(lua_State* state) {
    return luaL_error(state, "native AES test helper is only available on Windows");
}

int md5(lua_State* state) {
    return luaL_error(state, "native MD5 test helper is only available on Windows");
}
#endif

void set_lua_arguments(lua_State* state, const int argc, char** argv) {
    lua_createtable(state, argc, 0);
    lua_pushstring(state, argv[1]);
    lua_rawseti(state, -2, 0);
    for (int index = 2; index < argc; ++index) {
        lua_pushstring(state, argv[index]);
        lua_rawseti(state, -2, index - 1);
    }
    lua_setglobal(state, "arg");
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc < 2) {
        std::fputs("usage: gsave_package_lua_tests <script> [args...]\n", stderr);
        return 2;
    }

    lua_State* state = luaL_newstate();
    if (state == nullptr) {
        std::fputs("cannot create Lua state\n", stderr);
        return 2;
    }
    luaL_openlibs(state);
    lua_pushcfunction(state, aes_128_cbc_decrypt);
    lua_setglobal(state, "native_aes_128_cbc_decrypt");
    lua_pushcfunction(state, md5);
    lua_setglobal(state, "native_md5");
    set_lua_arguments(state, argc, argv);

    const int status = luaL_dofile(state, argv[1]);
    if (status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        std::fprintf(stderr, "%s\n", message == nullptr ? "Lua test failed" : message);
        lua_close(state);
        return 1;
    }
    lua_close(state);
    return 0;
}
