#pragma once

#include "gsave/base/error.hpp"
#include "gsave/core/types.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gsave::config {

enum class SyncBackend {
    git,
    webdav,
    onedrive,
};

enum class SyncTrigger {
    on_commit,
    on_exit,
    periodic,
    manual,
};

struct SaveConfig final {
    std::filesystem::path path;
    std::vector<std::string> include_globs;
    std::vector<std::string> exclude_globs;

    bool operator==(const SaveConfig&) const = default;
};

struct SyncPolicy final {
    SyncBackend backend{SyncBackend::git};
    SyncTrigger trigger{SyncTrigger::on_exit};
    std::optional<std::chrono::seconds> interval;
    std::string remote{"origin"};
    std::optional<std::string> credential_reference;

    bool operator==(const SyncPolicy&) const = default;
};

struct GameConfig final {
    std::string id;
    bool enabled{false};
    std::string process_name;
    std::filesystem::path process_path;
    std::filesystem::path parser;
    std::vector<SaveConfig> saves;
    core::CommitPolicy commit;
    SyncPolicy sync;
};

struct Config final {
    std::vector<GameConfig> games;
};

[[nodiscard]] Result<Config> parse_config(std::string_view source);
[[nodiscard]] Result<Config> load_config(const std::filesystem::path& path);
[[nodiscard]] Result<std::string> serialize_config(const Config& config);
[[nodiscard]] Status save_config_atomic(
    const std::filesystem::path& path,
    const Config& config);

}  // namespace gsave::config
