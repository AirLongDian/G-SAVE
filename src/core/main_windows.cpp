#include "gsave/config/config.hpp"
#include "gsave/core/app.hpp"
#include "gsave/platform/directory_watcher.hpp"
#include "gsave/platform/process_event_source.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <sddl.h>

#include <bit>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct StartupPaths final {
    std::filesystem::path config;
};

using CommandLineToArgvWFunction = LPWSTR*(WINAPI*)(LPCWSTR, int*);
using MessageBoxWFunction = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
constexpr wchar_t core_stop_event_name[] = L"Local\\G-SAVE.Core.Stop";

void CALLBACK request_core_stop(void* context, BOOLEAN) noexcept {
    static_cast<gsave::core::CoreApp*>(context)->request_stop();
}

[[nodiscard]] gsave::Result<std::filesystem::path> executable_path() {
    std::wstring buffer(512, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::unexpected(gsave::make_error(
                std::error_code{
                    static_cast<int>(GetLastError()), std::system_category()},
                "cannot locate gsave-core executable"));
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path{std::move(buffer)};
        }
        buffer.resize(buffer.size() * 2);
    }
}

[[nodiscard]] std::filesystem::path default_config_path(
    const std::filesystem::path& executable) {
    const auto required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required != 0) {
        std::wstring local_app_data(required, L'\0');
        const auto written = GetEnvironmentVariableW(
            L"LOCALAPPDATA", local_app_data.data(), required);
        if (written != 0 && written < required) {
            local_app_data.resize(written);
            return std::filesystem::path{std::move(local_app_data)}
                / L"G-SAVE" / L"config.toml";
        }
    }
    return executable.parent_path() / L"config.toml";
}

[[nodiscard]] gsave::Result<StartupPaths> parse_startup_paths(
    const std::wstring_view raw_arguments) {
    auto executable = executable_path();
    if (!executable) {
        return std::unexpected(executable.error());
    }
    StartupPaths result{
        .config = default_config_path(*executable),
    };

    if (raw_arguments.find_first_not_of(L" \t\r\n") == std::wstring_view::npos) {
        return result;
    }

    const auto shell = LoadLibraryExW(
        L"shell32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (shell == nullptr) {
        return std::unexpected(gsave::make_error(
            std::error_code{
                static_cast<int>(GetLastError()), std::system_category()},
            "cannot load the Windows command-line parser"));
    }
    const auto command_line_to_argv = std::bit_cast<CommandLineToArgvWFunction>(
        GetProcAddress(shell, "CommandLineToArgvW"));
    if (command_line_to_argv == nullptr) {
        const auto error = GetLastError();
        FreeLibrary(shell);
        return std::unexpected(gsave::make_error(
            std::error_code{static_cast<int>(error), std::system_category()},
            "cannot resolve the Windows command-line parser"));
    }

    int argument_count = 0;
    auto** arguments = command_line_to_argv(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        const auto error = GetLastError();
        FreeLibrary(shell);
        return std::unexpected(gsave::make_error(
            std::error_code{static_cast<int>(error), std::system_category()},
            "cannot parse gsave-core command line"));
    }

    const auto release_arguments = [&arguments, shell] {
        LocalFree(arguments);
        arguments = nullptr;
        FreeLibrary(shell);
    };
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view option{arguments[index]};
        if (option != L"--config") {
            release_arguments();
            return std::unexpected(gsave::make_error(
                std::errc::invalid_argument,
                "unsupported gsave-core command-line option"));
        }
        if (++index >= argument_count || arguments[index][0] == L'\0') {
            release_arguments();
            return std::unexpected(gsave::make_error(
                std::errc::invalid_argument,
                "gsave-core path option is missing its value"));
        }
        result.config = arguments[index];
    }
    release_arguments();
    return result;
}

[[nodiscard]] std::wstring widen_message(const std::string& message) {
    if (message.empty()) {
        return L"Unknown error";
    }
    const auto convert = [&message](const UINT code_page, const DWORD flags) {
        const auto size = MultiByteToWideChar(
            code_page, flags, message.data(), static_cast<int>(message.size()),
            nullptr, 0);
        if (size <= 0) {
            return std::wstring{};
        }
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(
            code_page, flags, message.data(), static_cast<int>(message.size()),
            result.data(), size);
        return result;
    };
    if (auto utf8 = convert(CP_UTF8, MB_ERR_INVALID_CHARS); !utf8.empty()) {
        return utf8;
    }
    return convert(CP_ACP, 0);
}

int show_failure(const gsave::Error& error) {
    const auto message = widen_message(error.message());
    const auto user32 = LoadLibraryExW(
        L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user32 != nullptr) {
        const auto message_box = std::bit_cast<MessageBoxWFunction>(
            GetProcAddress(user32, "MessageBoxW"));
        if (message_box != nullptr) {
            message_box(
                nullptr, message.c_str(), L"G-SAVE Core initialization failed",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        }
        FreeLibrary(user32);
    }
    return 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
    auto paths = parse_startup_paths(
        command_line == nullptr ? std::wstring_view{} : command_line);
    if (!paths) {
        return show_failure(paths.error());
    }

    auto config = gsave::config::load_config(paths->config);
    if (!config) {
        return show_failure(config.error());
    }
    auto watcher = gsave::platform::make_iocp_directory_watcher();
    if (!watcher) {
        return show_failure(watcher.error());
    }

    gsave::core::CoreApp app(
        gsave::core::CoreAppOptions{
            .config = std::move(*config),
            .config_directory = paths->config.parent_path(),
        },
        gsave::platform::make_wmi_process_event_source(),
        std::move(*watcher));

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = FALSE,
    };
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;IU)", SDDL_REVISION_1, &descriptor, nullptr) != FALSE) {
        security.lpSecurityDescriptor = descriptor;
    }
    HANDLE stop_event = CreateEventW(
        security.lpSecurityDescriptor == nullptr ? nullptr : &security,
        TRUE, FALSE, core_stop_event_name);
    const auto event_error = GetLastError();
    if (descriptor != nullptr) LocalFree(descriptor);
    if (stop_event == nullptr || event_error == ERROR_ALREADY_EXISTS) {
        if (stop_event != nullptr) CloseHandle(stop_event);
        return show_failure(gsave::make_error(
            event_error == ERROR_ALREADY_EXISTS
                ? std::make_error_code(std::errc::device_or_resource_busy)
                : std::error_code{
                    static_cast<int>(event_error), std::system_category()},
            event_error == ERROR_ALREADY_EXISTS
                ? "another G-SAVE Core instance is already running"
                : "cannot create Core stop control"));
    }
    HANDLE stop_wait = nullptr;
    if (RegisterWaitForSingleObject(
            &stop_wait, stop_event, request_core_stop, &app, INFINITE,
            WT_EXECUTEONLYONCE) == FALSE) {
        const auto error = GetLastError();
        CloseHandle(stop_event);
        return show_failure(gsave::make_error(
            std::error_code{static_cast<int>(error), std::system_category()},
            "cannot register Core stop control"));
    }

    auto status = app.run();
    UnregisterWaitEx(stop_wait, INVALID_HANDLE_VALUE);
    CloseHandle(stop_event);
    if (!status) {
        return show_failure(status.error());
    }
    return 0;
}
