#pragma once

#include "gsave/base/error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gsave::repository {

struct InitOptions final {
    std::filesystem::path repository;
    std::string game_id;
    std::filesystem::path parser;
    std::vector<std::string> exclude_patterns;
};

struct CommitOptions final {
    std::filesystem::path repository;
    std::string game_id;
    std::filesystem::path parser;
    std::string reason;
    bool allow_unchanged_tree{};
};

struct PushOptions final {
    std::filesystem::path repository;
    std::string remote{"origin"};
    std::wstring credential_reference;
};

struct RemoteTestOptions final {
    std::filesystem::path repository;
    std::string url;
    std::wstring credential_reference;
    std::string username;
    std::string secret;
};

struct RemoteTestResult final {
    std::size_t advertised_references{};
};

enum class CommitOutcome {
    created,
    no_changes,
    worktree_unstable,
};

struct CommitInfo final {
    std::string id;
    std::string summary;
    std::string message;
    std::string metadata_json;
    std::int64_t committed_at{};
};

struct RepositoryInfo final {
    std::string branch;
    std::optional<std::string> remote_url;
    bool worktree_dirty{};
    std::size_t ahead{};
    std::size_t behind{};
};

struct RestoreOptions final {
    std::filesystem::path repository;
    std::string commit_id;
    std::string game_id;
    std::filesystem::path parser;
};

struct IntegrateOptions final {
    std::filesystem::path repository;
    std::string remote{"origin"};
    std::wstring credential_reference;
};

enum class IntegrateOutcome {
    up_to_date,
    fast_forward,
    diverged,
};

enum class TimelineChoice {
    local_as_main,
    remote_as_main,
};

struct TimelineResolution final {
    std::string main_commit;
    std::string preserved_commit;
    std::string preserved_branch;
};

[[nodiscard]] Result<CommitOutcome> initialize_repository(const InitOptions& options);
[[nodiscard]] Result<CommitOutcome> commit_repository(const CommitOptions& options);
[[nodiscard]] Status push_repository(const PushOptions& options);
[[nodiscard]] Result<std::vector<CommitInfo>> list_history(
    const std::filesystem::path& repository,
    std::size_t maximum_count = 200);
[[nodiscard]] Result<RepositoryInfo> inspect_repository(
    const std::filesystem::path& repository,
    std::string_view remote = "origin");
[[nodiscard]] Status set_remote_url(
    const std::filesystem::path& repository,
    std::string_view remote,
    std::string_view url);
[[nodiscard]] Result<RemoteTestResult> test_remote_connection(
    RemoteTestOptions options);
[[nodiscard]] Result<CommitOutcome> restore_repository(
    const RestoreOptions& options);
[[nodiscard]] Result<IntegrateOutcome> integrate_repository(
    const IntegrateOptions& options);
[[nodiscard]] Result<TimelineResolution> resolve_divergence(
    const IntegrateOptions& options,
    TimelineChoice choice);

}  // namespace gsave::repository
