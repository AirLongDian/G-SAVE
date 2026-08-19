cmake_minimum_required(VERSION 3.24)

project(GSave VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MinGW otherwise retains unrelated functions from static dependency objects.
# Core is a single executable, so release builds keep only reachable sections
# and remove link-time symbols from the shipped PE image.
if(MINGW)
    string(APPEND CMAKE_C_FLAGS_RELEASE " -Os -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -Os -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,--gc-sections,--strip-all")
endif()

include(CTest)
include(cmake/Dependencies.cmake)

set(GSAVE_LUA_LIBRARY_SOURCES
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldblib.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    lutf8lib.c
    lvm.c
    lzio.c
)
list(TRANSFORM GSAVE_LUA_LIBRARY_SOURCES PREPEND "${lua_source_SOURCE_DIR}/src/")
add_library(gsave_lua STATIC ${GSAVE_LUA_LIBRARY_SOURCES})
target_include_directories(gsave_lua PUBLIC "${lua_source_SOURCE_DIR}/src")
if(UNIX AND NOT WIN32)
    target_compile_definitions(gsave_lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(gsave_lua PRIVATE ${CMAKE_DL_LIBS} m)
endif()

add_library(gsave_domain STATIC
    src/config/config.cpp
    src/core/windows_path.cpp
)
target_include_directories(gsave_domain PUBLIC include)
target_link_libraries(gsave_domain PUBLIC tomlplusplus::tomlplusplus)
target_compile_definitions(gsave_domain PRIVATE GSAVE_CONFIG_READ_ONLY)

if(MSVC)
    target_compile_options(gsave_domain PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gsave_domain PRIVATE -Wall -Wextra -Wpedantic)
endif()

# GUI-only TOML formatting and atomic replacement stay in a separate archive
# member so Core only carries the read/validation path.
add_library(gsave_config_writer STATIC src/config/config.cpp)
target_compile_definitions(gsave_config_writer PRIVATE GSAVE_CONFIG_WRITE_ONLY)
target_include_directories(gsave_config_writer PUBLIC include)
target_link_libraries(
    gsave_config_writer PUBLIC gsave_domain tomlplusplus::tomlplusplus
)
if(MSVC)
    target_compile_options(gsave_config_writer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gsave_config_writer PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(GSAVE_PLATFORM_SOURCES
    src/platform/windows/process_event_source_wmi.cpp
    src/platform/windows/directory_watcher_iocp.cpp
)
set(GSAVE_EXISTING_PLATFORM_SOURCES)
foreach(source IN LISTS GSAVE_PLATFORM_SOURCES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        list(APPEND GSAVE_EXISTING_PLATFORM_SOURCES "${source}")
    endif()
endforeach()

if(WIN32 AND GSAVE_EXISTING_PLATFORM_SOURCES)
    add_library(gsave_platform STATIC ${GSAVE_EXISTING_PLATFORM_SOURCES})
    target_include_directories(gsave_platform PUBLIC include)
    target_link_libraries(
        gsave_platform
        PUBLIC gsave_domain
        PRIVATE wbemuuid ole32 oleaut32 advapi32
    )
    if(MSVC)
        target_compile_options(gsave_platform PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(gsave_platform PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_library(gsave_core STATIC src/core/app.cpp)
    target_include_directories(gsave_core PUBLIC include)
    target_link_libraries(
        gsave_core
        PUBLIC gsave_domain gsave_platform
        PRIVATE gsave_repository
    )
    if(MSVC)
        target_compile_options(gsave_core PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(gsave_core PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set(GSAVE_WINDOWS_ICON_RESOURCE resources/gsave.rc)

    add_executable(gsave-core WIN32
        src/core/main_windows.cpp
        ${GSAVE_WINDOWS_ICON_RESOURCE}
    )
    target_include_directories(gsave-core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(gsave-core PRIVATE gsave_core)
    if(MSVC)
        target_compile_options(gsave-core PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(gsave-core PRIVATE -Wall -Wextra -Wpedantic)
        if(MINGW)
            target_link_options(gsave-core PRIVATE -municode)
        endif()
    endif()

    add_library(gsave_repository STATIC
        src/repository/repository_engine.cpp
        src/repository/lua_metadata.cpp
    )
    target_include_directories(
        gsave_repository
        PUBLIC include
        PRIVATE "${libgit2_SOURCE_DIR}/include" "${libgit2_BINARY_DIR}/include"
    )
    target_compile_definitions(gsave_repository PRIVATE GSAVE_CORE_ONLY)
    target_link_libraries(
        gsave_repository
        PUBLIC gsave_domain
        PRIVATE libgit2package gsave_lua bcrypt advapi32
    )
    if(MSVC)
        target_compile_options(gsave_repository PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(gsave_repository PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # Management-only repository operations are a separate archive member so
    # linking Core cannot pull history/checkout/divergence code or their libgit2
    # object dependencies into the resident executable.
    add_library(gsave_repository_management STATIC
        src/repository/repository_engine.cpp
    )
    target_compile_definitions(
        gsave_repository_management PRIVATE GSAVE_MANAGEMENT_ONLY
    )
    target_include_directories(
        gsave_repository_management
        PUBLIC include
        PRIVATE "${libgit2_SOURCE_DIR}/include" "${libgit2_BINARY_DIR}/include"
    )
    target_link_libraries(
        gsave_repository_management
        PUBLIC gsave_domain
        PRIVATE gsave_repository libgit2package gsave_lua bcrypt advapi32
    )
    if(MSVC)
        target_compile_options(gsave_repository_management PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(gsave_repository_management PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_library(gsave_gui_support STATIC
        src/gui/gui_model.cpp
        src/gui/package_installer.cpp
        src/gui/windows_control.cpp
    )
    target_include_directories(gsave_gui_support PUBLIC include)
    target_link_libraries(
        gsave_gui_support
        PUBLIC gsave_domain gsave_config_writer gsave_repository gsave_repository_management
        PRIVATE gsave_lua advapi32 shell32 ole32
    )
    if(MSVC)
        target_compile_options(gsave_gui_support PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(gsave_gui_support PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    find_package(Qt6 6.8 QUIET COMPONENTS Network Quick QuickControls2 Widgets)
    if(Qt6_FOUND)
        find_package(Qt6CorePrivate ${Qt6Core_VERSION} EXACT REQUIRED)
        qt_standard_project_setup(REQUIRES 6.8)
        qt_add_executable(gsave-gui WIN32
            src/gui/main_qt.cpp
            src/gui/gui_controller.cpp
            src/gui/cloud_service.cpp
            src/gui/package_archive.cpp
            src/gui/package_index.cpp
            ${GSAVE_WINDOWS_ICON_RESOURCE}
            include/gsave/gui/gui_controller.hpp
            include/gsave/gui/cloud_service.hpp
            include/gsave/gui/package_archive.hpp
            include/gsave/gui/package_index.hpp
        )
        set_source_files_properties(
            qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE
        )
        qt_add_qml_module(gsave-gui
            URI GSave
            VERSION 1.0
            QML_FILES
                qml/Main.qml
                qml/Theme.qml
                qml/components/AddGameCard.qml
                qml/components/AppButton.qml
                qml/components/AppComboBox.qml
                qml/components/AppTextField.qml
                qml/components/GameCard.qml
                qml/components/Panel.qml
                qml/pages/GameDetailPage.qml
                qml/pages/GameSavesTab.qml
                qml/pages/GameSupportTab.qml
                qml/pages/LibraryPage.qml
                qml/pages/SettingsPage.qml
        )
        qt_add_resources(gsave-gui gsave_app_resources
            PREFIX "/"
            FILES save.png
        )
        target_include_directories(gsave-gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_compile_definitions(
            gsave-gui PRIVATE
            UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00
        )
        target_link_libraries(
            gsave-gui
            PRIVATE
                gsave_gui_support
                Qt6::Network
                Qt6::Quick
                Qt6::QuickControls2
                Qt6::Widgets
                Qt6::CorePrivate
        )
        if(MSVC)
            target_compile_options(gsave-gui PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(gsave-gui PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_custom_command(
            TARGET gsave-gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory
                "$<TARGET_FILE_DIR:gsave-gui>/packages"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:gsave-gui>/packages/generic"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/packages/generic"
                "$<TARGET_FILE_DIR:gsave-gui>/packages/generic"
            VERBATIM
        )
        install(TARGETS gsave-core gsave-gui RUNTIME DESTINATION . COMPONENT GSave)

    else()
        message(WARNING
            "Qt 6.8+ Quick, QuickControls2 and Widgets were not found; "
            "gsave-gui will not be built. Set CMAKE_PREFIX_PATH to a Qt kit.")
        install(TARGETS gsave-core RUNTIME DESTINATION . COMPONENT GSave)
    endif()
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/packages/generic/"
        DESTINATION packages/generic
        COMPONENT GSave
    )
    install(
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/发布使用说明.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/游戏支持包使用指南.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/新游戏支持包制作教程.md"
        DESTINATION docs
        COMPONENT GSave
    )
    if(MINGW)
        get_filename_component(mingw_binary_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(runtime_name IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll)
            set(runtime_path "${mingw_binary_directory}/${runtime_name}")
            if(EXISTS "${runtime_path}")
                install(FILES "${runtime_path}" DESTINATION . COMPONENT GSave)
            endif()
        endforeach()
    endif()

endif()

if(BUILD_TESTING)
    set(GSAVE_TEST_SOURCES
        tests/config_tests.cpp
        tests/package_manifest_tests.cpp
    )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_contract_tests.cpp")
        list(APPEND GSAVE_TEST_SOURCES tests/platform_contract_tests.cpp)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/app_tests.cpp")
        list(APPEND GSAVE_TEST_SOURCES tests/app_tests.cpp)
    endif()

    add_executable(gsave_tests ${GSAVE_TEST_SOURCES})
    target_compile_definitions(
        gsave_tests
        PRIVATE GSAVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(gsave_tests PRIVATE gsave_domain GTest::gtest_main)
    target_link_libraries(gsave_tests PRIVATE gsave_config_writer)
    if(TARGET gsave_platform)
        target_link_libraries(gsave_tests PRIVATE gsave_platform)
    endif()
    if(TARGET gsave_core)
        target_link_libraries(gsave_tests PRIVATE gsave_core)
    endif()
    if(MSVC)
        target_compile_options(gsave_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(gsave_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    include(GoogleTest)
    gtest_discover_tests(gsave_tests)

    add_executable(gsave_package_lua_tests tests/package_lua_test_host.cpp)
    target_link_libraries(gsave_package_lua_tests PRIVATE gsave_lua)
    if(WIN32)
        target_link_libraries(gsave_package_lua_tests PRIVATE bcrypt)
    endif()
    add_test(
        NAME PackageAdapters.LuaContract
        COMMAND gsave_package_lua_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/lua/package_adapter_tests.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(TARGET gsave_repository)
        add_executable(gsave_repository_tests
            tests/repository_tests.cpp
            tests/real_ds3_core_tests.cpp
            tests/real_package_tests.cpp
        )
        target_compile_definitions(
            gsave_repository_tests
            PRIVATE GSAVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_include_directories(
            gsave_repository_tests
            PRIVATE "${libgit2_SOURCE_DIR}/include" "${libgit2_BINARY_DIR}/include"
        )
        target_link_libraries(
            gsave_repository_tests
            PRIVATE gsave_repository gsave_repository_management gsave_core GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(gsave_repository_tests PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(gsave_repository_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        gtest_discover_tests(gsave_repository_tests)

        add_executable(gsave_gui_tests tests/gui_model_tests.cpp)
        target_compile_definitions(
            gsave_gui_tests
            PRIVATE GSAVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_libraries(
            gsave_gui_tests PRIVATE gsave_gui_support GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(gsave_gui_tests PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(gsave_gui_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        gtest_discover_tests(gsave_gui_tests)

        if(TARGET gsave-gui AND Qt6_FOUND)
            add_executable(gsave_gui_controller_tests
                tests/qt_test_main.cpp
                tests/gui_controller_tests.cpp
                tests/cloud_service_tests.cpp
                tests/package_archive_tests.cpp
                tests/package_index_tests.cpp
                src/gui/gui_controller.cpp
                src/gui/cloud_service.cpp
                src/gui/package_archive.cpp
                src/gui/package_index.cpp
                include/gsave/gui/gui_controller.hpp
                include/gsave/gui/cloud_service.hpp
                include/gsave/gui/package_archive.hpp
                include/gsave/gui/package_index.hpp
            )
            target_link_libraries(
                gsave_gui_controller_tests
                PRIVATE
                    gsave_gui_support
                    Qt6::Network
                    Qt6::Quick
                    Qt6::QuickControls2
                    Qt6::Widgets
                    Qt6::CorePrivate
                    GTest::gtest
            )
            target_compile_definitions(
                gsave_gui_controller_tests
                PRIVATE GSAVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            )
            if(MSVC)
                target_compile_options(
                    gsave_gui_controller_tests PRIVATE /W4 /permissive- /utf-8)
            else()
                target_compile_options(
                    gsave_gui_controller_tests PRIVATE -Wall -Wextra -Wpedantic)
            endif()
            gtest_discover_tests(gsave_gui_controller_tests)

            add_executable(gsave_gui_visual_fixture tests/gui_visual_fixture.cpp)
            target_compile_definitions(
                gsave_gui_visual_fixture
                PRIVATE GSAVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            )
            target_link_libraries(
                gsave_gui_visual_fixture
                PRIVATE
                    gsave_repository
                    gsave_repository_management
                    gsave_config_writer
            )

            add_executable(gsave_gui_library_fixture tests/gui_library_fixture.cpp)
            target_compile_definitions(
                gsave_gui_library_fixture
                PRIVATE GSAVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            )
            target_link_libraries(
                gsave_gui_library_fixture
                PRIVATE
                    gsave_repository
                    gsave_repository_management
                    gsave_config_writer
            )

            # Dumps the real backend JSON contract so frontend mock data matches
            # production instead of being transcribed by hand.
            add_executable(gsave_gui_contract_dump
                tests/gui_contract_dump.cpp
                src/gui/gui_controller.cpp
                src/gui/cloud_service.cpp
                src/gui/package_archive.cpp
                src/gui/package_index.cpp
                include/gsave/gui/gui_controller.hpp
            )
            set_target_properties(
                gsave_gui_contract_dump PROPERTIES AUTOMOC ON
            )
            target_link_libraries(
                gsave_gui_contract_dump
                PRIVATE
                    gsave_gui_support
                    Qt6::Network
                    Qt6::Quick
                    Qt6::QuickControls2
                    Qt6::Widgets
                    Qt6::CorePrivate
            )
        endif()
    endif()
endif()

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "G-SAVE-${PROJECT_VERSION}-windows-x64")
set(CPACK_COMPONENTS_ALL GSave)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
include(CPack)
