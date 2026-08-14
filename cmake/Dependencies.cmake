include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)

# Repository work is linked statically into Core.  Its libraries are initialized
# only inside transient commit/push tasks and shut down before those tasks exit.
FetchContent_Declare(
    libgit2
    GIT_REPOSITORY https://github.com/libgit2/libgit2.git
    GIT_TAG 26055f5af74ab1cf636d272e8a34315496d3f06f # v1.9.6
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    lua_source
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

if(BUILD_TESTING)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(USE_SSH OFF CACHE STRING "" FORCE)
set(USE_HTTPS WinHTTP CACHE STRING "" FORCE)
set(USE_BUNDLED_ZLIB ON CACHE BOOL "" FORCE)
set(REGEX_BACKEND builtin CACHE STRING "" FORCE)
set(USE_HTTP_PARSER builtin CACHE STRING "" FORCE)
set(SONAME OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tomlplusplus libgit2 lua_source)
if(BUILD_TESTING)
    FetchContent_MakeAvailable(googletest)
endif()
