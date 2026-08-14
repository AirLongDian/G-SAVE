#include "gsave/gui/windows_control.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <wincred.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string_view>
#include <system_error>
#include <vector>

namespace gsave::gui {
namespace {

[[nodiscard]] Error windows_error(
    const DWORD code,
    std::string context) {
    return make_error(
        std::error_code(static_cast<int>(code), std::system_category()),
        std::move(context));
}

[[nodiscard]] std::wstring canonical_text(
    const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error).lexically_normal().wstring();
    if (error) absolute = path.lexically_normal().wstring();
    std::ranges::transform(absolute, absolute.begin(), [](const wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return absolute;
}

[[nodiscard]] std::wstring quote_argument(const std::wstring_view value) {
    if (value.empty()) return L"\"\"";
    if (value.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring{value};
    }
    std::wstring result{L'\"'};
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(character);
        }
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

[[nodiscard]] std::wstring join_arguments(
    const std::vector<std::wstring>& arguments) {
    std::wstring result;
    for (const auto& argument : arguments) {
        if (!result.empty()) result.push_back(L' ');
        result.append(quote_argument(argument));
    }
    return result;
}

[[nodiscard]] Result<DWORD> run_program(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments,
    const bool elevated) {
    if (elevated) {
        const auto parameters = join_arguments(arguments);
        SHELLEXECUTEINFOW execute{};
        execute.cbSize = sizeof(SHELLEXECUTEINFOW);
        execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        execute.lpVerb = L"runas";
        execute.lpFile = executable.c_str();
        execute.lpParameters = parameters.c_str();
        execute.lpDirectory = executable.parent_path().c_str();
        execute.nShow = SW_HIDE;
        if (ShellExecuteExW(&execute) == FALSE) {
            return std::unexpected(windows_error(
                GetLastError(), "cannot start elevated Windows task command"));
        }
        std::unique_ptr<void, decltype(&CloseHandle)> process{
            execute.hProcess, CloseHandle};
        if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
            return std::unexpected(windows_error(
                GetLastError(), "cannot wait for Windows task command"));
        }
        DWORD exit_code = 1;
        if (GetExitCodeProcess(process.get(), &exit_code) == FALSE) {
            return std::unexpected(windows_error(
                GetLastError(), "cannot read Windows task command result"));
        }
        return exit_code;
    }

    std::wstring command_line = quote_argument(executable.wstring());
    const auto parameters = join_arguments(arguments);
    if (!parameters.empty()) {
        command_line.push_back(L' ');
        command_line.append(parameters);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(STARTUPINFOW);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(
            executable.c_str(), command_line.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(),
            &startup, &process) == FALSE) {
        return std::unexpected(windows_error(
            GetLastError(), "cannot start Windows task command"));
    }
    CloseHandle(process.hThread);
    std::unique_ptr<void, decltype(&CloseHandle)> process_handle{
        process.hProcess, CloseHandle};
    if (WaitForSingleObject(process_handle.get(), INFINITE) != WAIT_OBJECT_0) {
        return std::unexpected(windows_error(
            GetLastError(), "cannot wait for Windows task command"));
    }
    DWORD exit_code = 1;
    if (GetExitCodeProcess(process_handle.get(), &exit_code) == FALSE) {
        return std::unexpected(windows_error(
            GetLastError(), "cannot read Windows task command result"));
    }
    return exit_code;
}

[[nodiscard]] Result<std::filesystem::path> task_scheduler_path() {
    std::wstring system_directory(MAX_PATH, L'\0');
    const auto size = GetSystemDirectoryW(
        system_directory.data(), static_cast<UINT>(system_directory.size()));
    if (size == 0 || size >= system_directory.size()) {
        return std::unexpected(windows_error(
            GetLastError(), "cannot locate Windows Task Scheduler command"));
    }
    system_directory.resize(size);
    return std::filesystem::path{std::move(system_directory)} / L"schtasks.exe";
}

[[nodiscard]] std::string to_utf8(const std::wstring_view text) {
    if (text.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            result.data(), required, nullptr, nullptr) <= 0) {
        return {};
    }
    return result;
}

[[nodiscard]] std::wstring from_utf8(const std::string_view text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            result.data(), required) <= 0) {
        return {};
    }
    return result;
}

}  // namespace

Result<ProcessState> inspect_process(const std::filesystem::path& executable) {
    const auto expected = canonical_text(executable);
    HANDLE raw_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (raw_snapshot == INVALID_HANDLE_VALUE) {
        return std::unexpected(windows_error(
            GetLastError(), "cannot enumerate running processes"));
    }
    std::unique_ptr<void, decltype(&CloseHandle)> snapshot{raw_snapshot, CloseHandle};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(snapshot.get(), &entry) == FALSE) {
        const auto error = GetLastError();
        if (error == ERROR_NO_MORE_FILES) return ProcessState{};
        return std::unexpected(windows_error(error, "cannot read running processes"));
    }
    do {
        HANDLE raw_process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
            FALSE, entry.th32ProcessID);
        if (raw_process == nullptr) continue;
        std::unique_ptr<void, decltype(&CloseHandle)> process{raw_process, CloseHandle};
        std::wstring path(32768, L'\0');
        DWORD length = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(
                process.get(), 0, path.data(), &length) == FALSE) {
            continue;
        }
        path.resize(length);
        if (canonical_text(path) == expected) {
            return ProcessState{
                .running = true,
                .process_id = entry.th32ProcessID,
            };
        }
    } while (Process32NextW(snapshot.get(), &entry) != FALSE);
    return ProcessState{};
}

Status start_core_elevated(
    const std::filesystem::path& executable,
    const std::filesystem::path& config) {
    if (!std::filesystem::is_regular_file(executable)) {
        return std::unexpected(make_error(
            std::errc::no_such_file_or_directory, "G-SAVE Core executable is missing"));
    }
    const std::wstring parameters = L"--config " + quote_argument(config.wstring());
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(SHELLEXECUTEINFOW);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    execute.lpVerb = L"runas";
    execute.lpFile = executable.c_str();
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = executable.parent_path().c_str();
    execute.nShow = SW_HIDE;
    if (ShellExecuteExW(&execute) == FALSE) {
        return std::unexpected(windows_error(
            GetLastError(), "cannot start G-SAVE Core"));
    }
    if (execute.hProcess != nullptr) CloseHandle(execute.hProcess);
    return {};
}

Status stop_core(
    const std::filesystem::path& executable,
    const std::uint32_t timeout_milliseconds) {
    auto state = inspect_process(executable);
    if (!state) return std::unexpected(state.error());
    if (!state->running) return {};
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, core_stop_event_name);
    if (event == nullptr) {
        return std::unexpected(windows_error(
            GetLastError(), "running Core does not expose graceful stop control"));
    }
    const BOOL signaled = SetEvent(event);
    const auto signal_error = GetLastError();
    CloseHandle(event);
    if (signaled == FALSE) {
        return std::unexpected(windows_error(
            signal_error, "cannot signal G-SAVE Core to stop"));
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, state->process_id);
    if (process == nullptr) {
        const auto error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER) return {};
        return std::unexpected(windows_error(error, "cannot wait for G-SAVE Core"));
    }
    const auto wait = WaitForSingleObject(process, timeout_milliseconds);
    CloseHandle(process);
    if (wait == WAIT_OBJECT_0) return {};
    if (wait == WAIT_TIMEOUT) {
        return std::unexpected(make_error(
            std::errc::timed_out,
            "Core is still finishing a repository task; try stopping again later"));
    }
    return std::unexpected(windows_error(
        GetLastError(), "cannot wait for G-SAVE Core to stop"));
}

Result<bool> autostart_enabled() {
    auto scheduler = task_scheduler_path();
    if (!scheduler) return std::unexpected(scheduler.error());
    auto result = run_program(
        *scheduler, {L"/Query", L"/TN", L"G-SAVE Core"}, false);
    if (!result) return std::unexpected(result.error());
    return *result == 0;
}

Status set_autostart(
    const bool enabled,
    const std::filesystem::path& core_executable,
    const std::filesystem::path& config) {
    auto scheduler = task_scheduler_path();
    if (!scheduler) return std::unexpected(scheduler.error());
    std::vector<std::wstring> arguments;
    if (enabled) {
        const std::wstring action = quote_argument(core_executable.wstring())
            + L" --config " + quote_argument(config.wstring());
        arguments = {
            L"/Create", L"/TN", L"G-SAVE Core", L"/SC", L"ONLOGON",
            L"/RL", L"HIGHEST", L"/TR", action, L"/F",
        };
    } else {
        arguments = {L"/Delete", L"/TN", L"G-SAVE Core", L"/F"};
    }
    auto result = run_program(*scheduler, arguments, true);
    if (!result) return std::unexpected(result.error());
    if (*result != 0) {
        return std::unexpected(make_error(
            std::errc::permission_denied,
            enabled ? "cannot create elevated Core logon task"
                    : "cannot remove Core logon task"));
    }
    return {};
}

Status store_git_credential(
    const std::wstring& reference,
    const std::wstring& username,
    const std::wstring& secret) {
    if (reference.empty() || username.empty() || secret.empty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "credential reference, username and secret must not be empty"));
    }
    auto secret_utf8 = to_utf8(secret);
    if (secret_utf8.empty() || secret_utf8.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        return std::unexpected(make_error(
            std::errc::value_too_large, "Git credential secret is invalid or too large"));
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(reference.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(secret_utf8.size());
    credential.CredentialBlob = reinterpret_cast<BYTE*>(secret_utf8.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(username.c_str());
    const BOOL written = CredWriteW(&credential, 0);
    const auto error = GetLastError();
    SecureZeroMemory(secret_utf8.data(), secret_utf8.size());
    if (written == FALSE) {
        return std::unexpected(windows_error(error, "cannot store Git credential"));
    }
    return {};
}

Status delete_git_credential(const std::wstring& reference) {
    if (CredDeleteW(reference.c_str(), CRED_TYPE_GENERIC, 0) == FALSE) {
        const auto error = GetLastError();
        if (error != ERROR_NOT_FOUND) {
            return std::unexpected(windows_error(error, "cannot delete Git credential"));
        }
    }
    return {};
}

Result<bool> credential_exists(const std::wstring& reference) {
    PCREDENTIALW raw = nullptr;
    if (CredReadW(reference.c_str(), CRED_TYPE_GENERIC, 0, &raw) != FALSE) {
        CredFree(raw);
        return true;
    }
    const auto error = GetLastError();
    if (error == ERROR_NOT_FOUND) return false;
    return std::unexpected(windows_error(error, "cannot inspect Git credential"));
}

Result<GitCredential> load_git_credential(const std::wstring& reference) {
    PCREDENTIALW raw = nullptr;
    if (CredReadW(reference.c_str(), CRED_TYPE_GENERIC, 0, &raw) == FALSE) {
        const auto error = GetLastError();
        if (error == ERROR_NOT_FOUND) {
            return std::unexpected(make_error(
                std::errc::no_such_file_or_directory,
                "saved Git service Token was not found"));
        }
        return std::unexpected(windows_error(error, "cannot read Git credential"));
    }
    std::unique_ptr<CREDENTIALW, decltype(&CredFree)> credential{raw, CredFree};
    std::string secret_utf8{
        reinterpret_cast<const char*>(credential->CredentialBlob),
        credential->CredentialBlobSize};
    auto secret = from_utf8(secret_utf8);
    if (!secret_utf8.empty()) {
        SecureZeroMemory(secret_utf8.data(), secret_utf8.size());
    }
    return GitCredential{
        credential->UserName == nullptr ? std::wstring{} : credential->UserName,
        std::move(secret),
    };
}

}  // namespace gsave::gui
