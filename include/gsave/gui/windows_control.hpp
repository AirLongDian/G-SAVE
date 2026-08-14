#pragma once

#include "gsave/base/error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace gsave::gui {

inline constexpr wchar_t core_stop_event_name[] = L"Local\\G-SAVE.Core.Stop";

struct ProcessState final {
    bool running{};
    std::uint32_t process_id{};
};

struct GitCredential final {
    std::wstring username;
    std::wstring secret;
};

[[nodiscard]] Result<ProcessState> inspect_process(
    const std::filesystem::path& executable);
[[nodiscard]] Status start_core_elevated(
    const std::filesystem::path& executable,
    const std::filesystem::path& config);
[[nodiscard]] Status stop_core(
    const std::filesystem::path& executable,
    std::uint32_t timeout_milliseconds = 15000);

[[nodiscard]] Result<bool> autostart_enabled();
[[nodiscard]] Status set_autostart(
    bool enabled,
    const std::filesystem::path& core_executable,
    const std::filesystem::path& config);

[[nodiscard]] Status store_git_credential(
    const std::wstring& reference,
    const std::wstring& username,
    const std::wstring& secret);
[[nodiscard]] Status delete_git_credential(const std::wstring& reference);
[[nodiscard]] Result<bool> credential_exists(const std::wstring& reference);
[[nodiscard]] Result<GitCredential> load_git_credential(
    const std::wstring& reference);

}  // namespace gsave::gui
