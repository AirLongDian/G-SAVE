#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gsave::core {

using CoreClock = std::chrono::steady_clock;
using TimePoint = CoreClock::time_point;

enum class CommitStrategy {
    quiet,
    periodic,
    on_exit,
    hybrid,
};

struct CommitPolicy final {
    CommitStrategy strategy{CommitStrategy::on_exit};
    std::optional<std::chrono::seconds> quiet_interval;
    std::optional<std::chrono::seconds> max_interval;
    bool commit_on_exit{true};
};

enum class CommitReason {
    quiet_period_elapsed,
    max_interval_elapsed,
    game_exit,
};

// These helpers deliberately apply Windows path rules without consulting the
// filesystem. Both slash characters are separators, dot components are
// collapsed, and comparison keys are ASCII case-insensitive.
[[nodiscard]] std::filesystem::path normalize_windows_path_lexically(
    const std::filesystem::path& path);
[[nodiscard]] std::string windows_path_key(const std::filesystem::path& path);
[[nodiscard]] bool is_windows_absolute_path(const std::filesystem::path& path);
[[nodiscard]] bool path_glob_matches(
    std::string_view pattern,
    std::string_view relative_path) noexcept;

}  // namespace gsave::core
