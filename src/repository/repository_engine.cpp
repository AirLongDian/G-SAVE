#include "gsave/repository/repository_engine.hpp"

#include "gsave/repository/lua_metadata.hpp"

#include <git2.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wincred.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace gsave::repository {
namespace {

template <typename T, void (*Free)(T*)>
struct GitDeleter final {
    void operator()(T* value) const noexcept {
        if (value != nullptr) Free(value);
    }
};

template <typename T, void (*Free)(T*)>
using GitPointer = std::unique_ptr<T, GitDeleter<T, Free>>;

using Repository = GitPointer<git_repository, git_repository_free>;
using Index = GitPointer<git_index, git_index_free>;
using Tree = GitPointer<git_tree, git_tree_free>;
using Commit = GitPointer<git_commit, git_commit_free>;
using Signature = GitPointer<git_signature, git_signature_free>;
using StatusList = GitPointer<git_status_list, git_status_list_free>;
using Diff = GitPointer<git_diff, git_diff_free>;
using Remote = GitPointer<git_remote, git_remote_free>;
using Reference = GitPointer<git_reference, git_reference_free>;
using Config = GitPointer<git_config, git_config_free>;
#ifndef GSAVE_CORE_ONLY
using Object = GitPointer<git_object, git_object_free>;
using Revwalk = GitPointer<git_revwalk, git_revwalk_free>;
using AnnotatedCommit = GitPointer<git_annotated_commit, git_annotated_commit_free>;
#endif

class GitRuntime final {
public:
    GitRuntime() : initialized_(git_libgit2_init() >= 0) {}
    ~GitRuntime() {
        if (initialized_) git_libgit2_shutdown();
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_{};
};

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] Error git_error(const std::string_view operation) {
    const auto* last = git_error_last();
    std::string context{operation};
    if (last != nullptr && last->message != nullptr) {
        context.append(": ");
        context.append(last->message);
    }
    return make_error(std::errc::io_error, std::move(context));
}

[[nodiscard]] Error git_push_error(
    const std::string_view operation,
    const int result) {
    const auto* last = git_error_last();
    std::string context{operation};
    if (last != nullptr && last->message != nullptr) {
        context.append(": ");
        context.append(last->message);
    }
    const bool network = result != GIT_ENONFASTFORWARD && last != nullptr
        && (last->klass == GIT_ERROR_NET
            || last->klass == GIT_ERROR_HTTP
            || last->klass == GIT_ERROR_SSL
            || last->klass == GIT_ERROR_SSH);
    return make_error(
        network ? std::errc::network_unreachable : std::errc::io_error,
        std::move(context));
}

[[nodiscard]] Result<Repository> open_repository(
    const std::filesystem::path& repository_path,
    const bool require_idle_state = true) {
    git_repository* raw = nullptr;
    const auto path = path_utf8(repository_path);
    if (git_repository_open_ext(
            &raw, path.c_str(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr) < 0) {
        return std::unexpected(git_error("cannot open repository"));
    }
    Repository repository{raw};
    if (git_repository_is_bare(repository.get()) != 0) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "save repository must have a working tree"));
    }
    if (require_idle_state
        && git_repository_state(repository.get()) != GIT_REPOSITORY_STATE_NONE) {
        return std::unexpected(make_error(
            std::errc::device_or_resource_busy,
            "repository has an unfinished merge, rebase or other operation"));
    }
    return repository;
}

#ifndef GSAVE_CORE_ONLY
[[nodiscard]] Status ensure_clean_worktree(git_repository* repository) {
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
        | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
        | GIT_STATUS_OPT_INCLUDE_UNMODIFIED;
    git_status_list* raw = nullptr;
    if (git_status_list_new(&raw, repository, &options) < 0) {
        return std::unexpected(git_error("cannot inspect repository status"));
    }
    StatusList list{raw};
    const auto count = git_status_list_entrycount(list.get());
    for (std::size_t index = 0; index < count; ++index) {
        const auto* entry = git_status_byindex(list.get(), index);
        if (entry != nullptr && entry->status != GIT_STATUS_CURRENT
            && entry->status != GIT_STATUS_IGNORED) {
            return std::unexpected(make_error(
                std::errc::device_or_resource_busy,
                "repository contains uncommitted save changes"));
        }
    }
    return {};
}
#endif

#ifndef GSAVE_MANAGEMENT_ONLY
[[nodiscard]] Status write_exclude_patterns(
    git_repository* repository,
    const std::vector<std::string>& patterns) {
    if (patterns.empty()) return {};
    const char* git_directory = git_repository_path(repository);
    if (git_directory == nullptr) {
        return std::unexpected(git_error("cannot locate repository metadata"));
    }
    const auto git_directory_length = std::char_traits<char>::length(git_directory);
    const auto* git_directory_u8 = reinterpret_cast<const char8_t*>(git_directory);
    const auto exclude_path = std::filesystem::path{std::u8string{
        git_directory_u8, git_directory_u8 + git_directory_length}}
        / "info" / "exclude";
    std::error_code filesystem_error;
    std::filesystem::create_directories(exclude_path.parent_path(), filesystem_error);
    if (filesystem_error) {
        return std::unexpected(make_error(
            filesystem_error, "cannot create repository exclude directory"));
    }

    std::set<std::string> existing;
    {
        std::ifstream input(exclude_path, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) existing.emplace(line);
    }
    std::ofstream output(exclude_path, std::ios::binary | std::ios::app);
    if (!output) {
        return std::unexpected(make_error(
            std::errc::io_error, "cannot open repository exclude file"));
    }
    for (const auto& pattern : patterns) {
        if (!pattern.empty() && existing.emplace(pattern).second) {
            output << pattern << '\n';
        }
    }
    output.flush();
    if (!output) {
        return std::unexpected(make_error(
            std::errc::io_error, "cannot write repository exclude file"));
    }
    return {};
}
#endif

[[nodiscard]] std::vector<std::string> collect_changed_files(
    git_repository* repository) {
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
        | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
        | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
        | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR
        | GIT_STATUS_OPT_SORT_CASE_SENSITIVELY;
    git_status_list* raw = nullptr;
    if (git_status_list_new(&raw, repository, &options) < 0) {
        return {};
    }
    StatusList list{raw};
    std::vector<std::string> paths;
    const auto count = git_status_list_entrycount(list.get());
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto* entry = git_status_byindex(list.get(), index);
        const char* path = nullptr;
        if (entry != nullptr && entry->index_to_workdir != nullptr) {
            path = entry->index_to_workdir->new_file.path != nullptr
                ? entry->index_to_workdir->new_file.path
                : entry->index_to_workdir->old_file.path;
        }
        if (path == nullptr && entry != nullptr && entry->head_to_index != nullptr) {
            path = entry->head_to_index->new_file.path != nullptr
                ? entry->head_to_index->new_file.path
                : entry->head_to_index->old_file.path;
        }
        if (path != nullptr) paths.emplace_back(path);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

[[nodiscard]] Result<bool> worktree_matches_index(
    git_repository* repository,
    git_index* index) {
    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.flags = GIT_DIFF_INCLUDE_UNTRACKED
        | GIT_DIFF_RECURSE_UNTRACKED_DIRS
        | GIT_DIFF_INCLUDE_TYPECHANGE;
    git_diff* raw = nullptr;
    if (git_diff_index_to_workdir(&raw, repository, index, &options) < 0) {
        return std::unexpected(git_error("cannot verify worktree stability"));
    }
    Diff diff{raw};
    return git_diff_num_deltas(diff.get()) == 0;
}

[[nodiscard]] Result<Signature> repository_signature(git_repository* repository) {
    git_signature* raw = nullptr;
    if (git_signature_default(&raw, repository) < 0) {
        if (git_signature_now(&raw, "G-SAVE", "gsave@localhost") < 0) {
            return std::unexpected(git_error("cannot create commit identity"));
        }
    }
    return Signature{raw};
}

[[nodiscard]] std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &time);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

[[nodiscard]] Result<CommitOutcome> create_commit(
    git_repository* repository,
    const CommitOptions& options) {
    git_index* raw_index = nullptr;
    if (git_repository_index(&raw_index, repository) < 0) {
        return std::unexpected(git_error("cannot open repository index"));
    }
    Index index{raw_index};
    if (git_index_read(index.get(), 1) < 0) {
        return std::unexpected(git_error("cannot refresh repository index"));
    }

    const auto changed_files = collect_changed_files(repository);
    if (git_index_add_all(index.get(), nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr) < 0) {
        return std::unexpected(git_error("cannot stage save files"));
    }

    git_oid tree_id{};
    if (git_index_write_tree_to(&tree_id, index.get(), repository) < 0) {
        return std::unexpected(git_error("cannot create save tree"));
    }
    git_tree* raw_tree = nullptr;
    if (git_tree_lookup(&raw_tree, repository, &tree_id) < 0) {
        return std::unexpected(git_error("cannot load save tree"));
    }
    Tree tree{raw_tree};

    git_commit* raw_parent = nullptr;
    const int head_result = git_reference_name_to_id(&tree_id, repository, "HEAD");
    if (head_result == 0 && git_commit_lookup(&raw_parent, repository, &tree_id) < 0) {
        return std::unexpected(git_error("cannot load parent commit"));
    }
    if (head_result != 0 && head_result != GIT_ENOTFOUND && head_result != GIT_EUNBORNBRANCH) {
        return std::unexpected(git_error("cannot resolve repository HEAD"));
    }
    Commit parent{raw_parent};
    if (parent && !options.allow_unchanged_tree
        && git_oid_equal(git_tree_id(tree.get()), git_commit_tree_id(parent.get()))) {
        return CommitOutcome::no_changes;
    }

    auto stable = worktree_matches_index(repository, index.get());
    if (!stable) return std::unexpected(stable.error());
    if (!*stable) return CommitOutcome::worktree_unstable;

    std::string metadata = "{\"parser_error\":\"metadata unavailable\"}";
    if (!options.parser.empty()) {
        auto parsed = parse_metadata(MetadataRequest{
            .repository = options.repository,
            .parser = options.parser,
            .changed_files = changed_files,
        });
        if (parsed) metadata = std::move(*parsed);
    }

    stable = worktree_matches_index(repository, index.get());
    if (!stable) return std::unexpected(stable.error());
    if (!*stable) return CommitOutcome::worktree_unstable;

    auto signature = repository_signature(repository);
    if (!signature) return std::unexpected(signature.error());
    std::string message = "G-SAVE ";
    message.append(options.game_id.empty() ? "save" : options.game_id);
    message.append(": ");
    message.append(options.reason.empty() ? "automatic" : options.reason);
    message.append(" at ");
    message.append(utc_timestamp());
    message.append("\n\nG-SAVE-Metadata: ");
    message.append(metadata);
    message.push_back('\n');

    if (git_index_write(index.get()) < 0) {
        return std::unexpected(git_error("cannot persist repository index"));
    }
    git_oid commit_id{};
    const git_commit* parents[] = {parent.get()};
    if (git_commit_create(
            &commit_id,
            repository,
            "HEAD",
            signature->get(),
            signature->get(),
            "UTF-8",
            message.c_str(),
            tree.get(),
            parent ? 1U : 0U,
            parent ? parents : nullptr) < 0) {
        return std::unexpected(git_error("cannot create save commit"));
    }
    return CommitOutcome::created;
}

[[nodiscard]] std::string wide_to_utf8(const std::wstring_view text) {
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

struct CredentialPayload final {
    std::wstring reference;
    const std::string* username{};
    const std::string* secret{};
};

class SecretWiper final {
public:
    explicit SecretWiper(std::string& secret) noexcept : secret_(secret) {}
    ~SecretWiper() {
        if (!secret_.empty()) SecureZeroMemory(secret_.data(), secret_.size());
    }
    SecretWiper(const SecretWiper&) = delete;
    SecretWiper& operator=(const SecretWiper&) = delete;

private:
    std::string& secret_;
};

int credential_callback(
    git_credential** output,
    const char*,
    const char* username_from_url,
    const unsigned int allowed_types,
    void* payload) {
    auto& credentials = *static_cast<CredentialPayload*>(payload);
    if (credentials.secret != nullptr && !credentials.secret->empty()
        && (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) != 0) {
        const char* username = credentials.username == nullptr
                || credentials.username->empty()
            ? (username_from_url == nullptr ? "git" : username_from_url)
            : credentials.username->c_str();
        return git_credential_userpass_plaintext_new(
            output, username, credentials.secret->c_str());
    }
    if (!credentials.reference.empty()
        && (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) != 0) {
        PCREDENTIALW raw = nullptr;
        if (CredReadW(
                credentials.reference.c_str(), CRED_TYPE_GENERIC, 0, &raw) != FALSE) {
            std::unique_ptr<CREDENTIALW, decltype(&CredFree)> credential{raw, CredFree};
            const std::string username = credential->UserName == nullptr
                ? std::string{username_from_url == nullptr ? "git" : username_from_url}
                : wide_to_utf8(credential->UserName);
            std::string secret(
                reinterpret_cast<const char*>(credential->CredentialBlob),
                credential->CredentialBlobSize);
            const int result = git_credential_userpass_plaintext_new(
                output, username.c_str(), secret.c_str());
            SecureZeroMemory(secret.data(), secret.size());
            return result;
        }
    }
    if ((allowed_types & GIT_CREDENTIAL_DEFAULT) != 0) {
        return git_credential_default_new(output);
    }
    if ((allowed_types & GIT_CREDENTIAL_USERNAME) != 0
        && username_from_url != nullptr) {
        return git_credential_username_new(output, username_from_url);
    }
    return GIT_PASSTHROUGH;
}

#ifndef GSAVE_CORE_ONLY
[[nodiscard]] std::string commit_metadata(const char* message) {
    if (message == nullptr) return {};
    constexpr std::string_view marker{"\nG-SAVE-Metadata: "};
    const std::string_view text{message};
    const auto offset = text.find(marker);
    if (offset == std::string_view::npos) return {};
    auto metadata = text.substr(offset + marker.size());
    while (!metadata.empty()
           && (metadata.back() == '\r' || metadata.back() == '\n')) {
        metadata.remove_suffix(1);
    }
    return std::string{metadata};
}

[[nodiscard]] Status checkout_head(git_repository* repository) {
    git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
    checkout.checkout_strategy = GIT_CHECKOUT_FORCE
        | GIT_CHECKOUT_RECREATE_MISSING
        | GIT_CHECKOUT_REMOVE_UNTRACKED;
    if (git_checkout_head(repository, &checkout) < 0) {
        return std::unexpected(git_error("cannot restore repository worktree"));
    }
    return {};
}

[[nodiscard]] std::string oid_text(const git_oid& oid) {
    char value[GIT_OID_HEXSZ + 1]{};
    git_oid_tostr(value, sizeof(value), &oid);
    return value;
}

[[nodiscard]] Status create_preserved_branch(
    git_repository* repository,
    const std::string& reference_name,
    const git_oid& target) {
    git_reference* raw_existing = nullptr;
    const int lookup = git_reference_lookup(
        &raw_existing, repository, reference_name.c_str());
    if (lookup == 0) {
        Reference existing{raw_existing};
        const git_oid* current = git_reference_target(existing.get());
        if (current != nullptr && git_oid_equal(current, &target) != 0) return {};
        return std::unexpected(make_error(
            std::errc::file_exists, "preserved timeline branch already names another commit"));
    }
    if (lookup != GIT_ENOTFOUND) {
        return std::unexpected(git_error("cannot inspect preserved timeline branch"));
    }
    git_reference* raw_created = nullptr;
    if (git_reference_create(
            &raw_created, repository, reference_name.c_str(), &target, 0,
            "G-SAVE preserve divergent timeline") < 0) {
        return std::unexpected(git_error("cannot preserve divergent timeline"));
    }
    Reference created{raw_created};
    return {};
}

[[nodiscard]] Status push_refspec(
    git_remote* remote,
    const std::string& refspec,
    CredentialPayload& credentials) {
    char* refspec_text = const_cast<char*>(refspec.c_str());
    git_strarray refspecs{&refspec_text, 1};
    git_push_options options = GIT_PUSH_OPTIONS_INIT;
    options.callbacks.credentials = credential_callback;
    options.callbacks.payload = &credentials;
    const int result = git_remote_push(remote, &refspecs, &options);
    if (result < 0) {
        return std::unexpected(git_push_error("cannot publish timeline reference", result));
    }
    return {};
}
#endif

}  // namespace

#ifndef GSAVE_MANAGEMENT_ONLY
Result<CommitOutcome> initialize_repository(const InitOptions& options) {
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    git_repository* raw = nullptr;
    const auto path = path_utf8(options.repository);
    if (git_repository_init(&raw, path.c_str(), 0) < 0) {
        return std::unexpected(git_error("cannot initialize repository"));
    }
    Repository repository{raw};
    git_config* raw_config = nullptr;
    if (git_repository_config(&raw_config, repository.get()) < 0) {
        return std::unexpected(git_error("cannot open repository configuration"));
    }
    Config config{raw_config};
    if (git_config_set_bool(config.get(), "core.autocrlf", 0) < 0
        || git_config_set_bool(config.get(), "core.filemode", 0) < 0) {
        return std::unexpected(git_error("cannot configure binary save repository"));
    }
    if (auto excluded = write_exclude_patterns(repository.get(), options.exclude_patterns);
        !excluded) {
        return std::unexpected(excluded.error());
    }
    return create_commit(repository.get(), CommitOptions{
        .repository = options.repository,
        .game_id = options.game_id,
        .parser = options.parser,
        .reason = "initial-baseline",
        .allow_unchanged_tree = true,
    });
}

Result<CommitOutcome> commit_repository(const CommitOptions& options) {
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(options.repository);
    if (!repository) return std::unexpected(repository.error());
    return create_commit(repository->get(), options);
}

Status push_repository(const PushOptions& options) {
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(options.repository);
    if (!repository) return std::unexpected(repository.error());

    git_reference* raw_head = nullptr;
    if (git_repository_head(&raw_head, repository->get()) < 0) {
        return std::unexpected(git_error("cannot resolve branch for push"));
    }
    Reference head{raw_head};
    if (!git_reference_is_branch(head.get())) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "cannot push a detached repository HEAD"));
    }
    const char* branch_name = git_reference_name(head.get());
    if (branch_name == nullptr) {
        return std::unexpected(git_error("cannot read branch name"));
    }

    git_remote* raw_remote = nullptr;
    if (git_remote_lookup(&raw_remote, repository->get(), options.remote.c_str()) < 0) {
        return std::unexpected(git_error("cannot open configured remote"));
    }
    Remote remote{raw_remote};
    const std::string refspec = std::string{branch_name} + ":" + branch_name;
    char* refspec_text = const_cast<char*>(refspec.c_str());
    git_strarray refspecs{&refspec_text, 1};
    CredentialPayload credentials{.reference = options.credential_reference};
    git_push_options push_options = GIT_PUSH_OPTIONS_INIT;
    push_options.callbacks.credentials = credential_callback;
    push_options.callbacks.payload = &credentials;
    const int result = git_remote_push(remote.get(), &refspecs, &push_options);
    if (result < 0) {
        return std::unexpected(git_push_error("cannot push save history", result));
    }
    return {};
}
#endif

#ifndef GSAVE_CORE_ONLY
Result<std::vector<CommitInfo>> list_history(
    const std::filesystem::path& repository_path,
    const std::size_t maximum_count) {
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(repository_path, false);
    if (!repository) return std::unexpected(repository.error());
    git_revwalk* raw_walk = nullptr;
    if (git_revwalk_new(&raw_walk, repository->get()) < 0) {
        return std::unexpected(git_error("cannot create history walker"));
    }
    Revwalk walk{raw_walk};
    git_revwalk_sorting(walk.get(), GIT_SORT_TIME | GIT_SORT_TOPOLOGICAL);
    const int push_head = git_revwalk_push_head(walk.get());
    if (push_head < 0 && push_head != GIT_EUNBORNBRANCH) {
        return std::unexpected(git_error("cannot read repository history"));
    }
    static_cast<void>(git_revwalk_push_glob(walk.get(), "refs/gsave/*"));

    std::vector<CommitInfo> result;
    result.reserve(std::min<std::size_t>(maximum_count, 200));
    git_oid oid{};
    while (result.size() < maximum_count) {
        const int next = git_revwalk_next(&oid, walk.get());
        if (next == GIT_ITEROVER) break;
        if (next < 0) {
            return std::unexpected(git_error("cannot walk repository history"));
        }
        git_commit* raw_commit = nullptr;
        if (git_commit_lookup(&raw_commit, repository->get(), &oid) < 0) {
            return std::unexpected(git_error("cannot load history commit"));
        }
        Commit commit{raw_commit};
        char id[GIT_OID_HEXSZ + 1]{};
        git_oid_tostr(id, sizeof(id), &oid);
        const char* summary = git_commit_summary(commit.get());
        const char* message = git_commit_message(commit.get());
        result.push_back(CommitInfo{
            .id = id,
            .summary = summary == nullptr ? std::string{} : std::string{summary},
            .message = message == nullptr ? std::string{} : std::string{message},
            .metadata_json = commit_metadata(message),
            .committed_at = static_cast<std::int64_t>(git_commit_time(commit.get())),
        });
    }
    return result;
}

Result<RepositoryInfo> inspect_repository(
    const std::filesystem::path& repository_path,
    const std::string_view remote_name) {
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(repository_path, false);
    if (!repository) return std::unexpected(repository.error());
    RepositoryInfo info;

    git_reference* raw_head = nullptr;
    if (git_repository_head(&raw_head, repository->get()) == 0) {
        Reference head{raw_head};
        const char* shorthand = git_reference_shorthand(head.get());
        if (shorthand != nullptr) info.branch = shorthand;
        const git_oid* local_oid = git_reference_target(head.get());
        if (local_oid != nullptr && !info.branch.empty() && !remote_name.empty()) {
            const std::string remote_ref = "refs/remotes/"
                + std::string(remote_name) + "/" + info.branch;
            git_oid remote_oid{};
            if (git_reference_name_to_id(
                    &remote_oid, repository->get(), remote_ref.c_str()) == 0) {
                static_cast<void>(git_graph_ahead_behind(
                    &info.ahead, &info.behind, repository->get(),
                    local_oid, &remote_oid));
            }
        }
    }

    if (!remote_name.empty()) {
        git_remote* raw_remote = nullptr;
        const std::string remote_text{remote_name};
        if (git_remote_lookup(
                &raw_remote, repository->get(), remote_text.c_str()) == 0) {
            Remote remote{raw_remote};
            const char* url = git_remote_url(remote.get());
            if (url != nullptr) info.remote_url = std::string{url};
        }
    }

    git_status_options status_options = GIT_STATUS_OPTIONS_INIT;
    status_options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    status_options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
        | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
    git_status_list* raw_status = nullptr;
    if (git_status_list_new(
            &raw_status, repository->get(), &status_options) < 0) {
        return std::unexpected(git_error("cannot inspect repository status"));
    }
    StatusList status{raw_status};
    info.worktree_dirty = git_status_list_entrycount(status.get()) != 0;

    return info;
}

Status set_remote_url(
    const std::filesystem::path& repository_path,
    const std::string_view remote_name,
    const std::string_view url) {
    if (remote_name.empty() || url.empty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "remote name and URL must not be empty"));
    }
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(repository_path);
    if (!repository) return std::unexpected(repository.error());
    const std::string name{remote_name};
    const std::string url_text{url};
    git_remote* raw_remote = nullptr;
    const int lookup = git_remote_lookup(&raw_remote, repository->get(), name.c_str());
    if (lookup == 0) {
        Remote remote{raw_remote};
        if (git_remote_set_url(
                repository->get(), name.c_str(), url_text.c_str()) < 0) {
            return std::unexpected(git_error("cannot update repository remote"));
        }
    } else if (lookup == GIT_ENOTFOUND) {
        if (git_remote_create(
                &raw_remote, repository->get(), name.c_str(), url_text.c_str()) < 0) {
            return std::unexpected(git_error("cannot create repository remote"));
        }
        Remote remote{raw_remote};
    } else {
        return std::unexpected(git_error("cannot inspect repository remote"));
    }
    return {};
}

Result<RemoteTestResult> test_remote_connection(RemoteTestOptions options) {
    SecretWiper wipe_secret{options.secret};
    if (options.url.empty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "remote URL must not be empty"));
    }
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(options.repository, false);
    if (!repository) return std::unexpected(repository.error());

    git_remote* raw_remote = nullptr;
    if (git_remote_create_anonymous(
            &raw_remote, repository->get(), options.url.c_str()) < 0) {
        return std::unexpected(git_error("cannot prepare remote connection test"));
    }
    Remote remote{raw_remote};
    CredentialPayload credentials{
        .reference = options.credential_reference,
        .username = &options.username,
        .secret = &options.secret,
    };
    git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
    callbacks.credentials = credential_callback;
    callbacks.payload = &credentials;
    git_proxy_options proxy = GIT_PROXY_OPTIONS_INIT;
    const int connected = git_remote_connect(
        remote.get(), GIT_DIRECTION_PUSH, &callbacks, &proxy, nullptr);
    if (connected < 0) {
        return std::unexpected(git_push_error(
            "cannot connect to remote upload service", connected));
    }

    const git_remote_head** heads = nullptr;
    std::size_t count = 0;
    if (git_remote_ls(&heads, &count, remote.get()) < 0) {
        git_remote_disconnect(remote.get());
        return std::unexpected(git_error("cannot read remote references"));
    }
    git_remote_disconnect(remote.get());
    return RemoteTestResult{.advertised_references = count};
}

Result<CommitOutcome> restore_repository(const RestoreOptions& options) {
    if (options.commit_id.empty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "restore commit ID must not be empty"));
    }
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(options.repository);
    if (!repository) return std::unexpected(repository.error());

    auto recovery = create_commit(repository->get(), CommitOptions{
        .repository = options.repository,
        .game_id = options.game_id,
        .parser = options.parser,
        .reason = "pre-restore-recovery",
    });
    if (!recovery) return std::unexpected(recovery.error());
    if (*recovery == CommitOutcome::worktree_unstable) return *recovery;

    git_object* raw_object = nullptr;
    if (git_revparse_single(
            &raw_object, repository->get(), options.commit_id.c_str()) < 0) {
        return std::unexpected(git_error("cannot resolve restore commit"));
    }
    Object object{raw_object};
    git_object* raw_commit_object = nullptr;
    if (git_object_peel(&raw_commit_object, object.get(), GIT_OBJECT_COMMIT) < 0) {
        return std::unexpected(git_error("restore target is not a commit"));
    }
    Object commit_object{raw_commit_object};
    git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
    checkout.checkout_strategy = GIT_CHECKOUT_FORCE
        | GIT_CHECKOUT_RECREATE_MISSING
        | GIT_CHECKOUT_REMOVE_UNTRACKED;
    if (git_checkout_tree(repository->get(), commit_object.get(), &checkout) < 0) {
        static_cast<void>(checkout_head(repository->get()));
        return std::unexpected(git_error("cannot restore selected save files"));
    }
    auto outcome = create_commit(repository->get(), CommitOptions{
        .repository = options.repository,
        .game_id = options.game_id,
        .parser = options.parser,
        .reason = "restore-" + options.commit_id.substr(0, 12),
    });
    if (!outcome || *outcome == CommitOutcome::worktree_unstable) {
        static_cast<void>(checkout_head(repository->get()));
        if (!outcome) return std::unexpected(outcome.error());
    }
    return outcome;
}

Result<IntegrateOutcome> integrate_repository(const IntegrateOptions& options) {
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(options.repository);
    if (!repository) return std::unexpected(repository.error());
    if (auto clean = ensure_clean_worktree(repository->get()); !clean) {
        return std::unexpected(clean.error());
    }

    git_reference* raw_head = nullptr;
    if (git_repository_head(&raw_head, repository->get()) < 0) {
        return std::unexpected(git_error("cannot resolve branch for synchronization"));
    }
    Reference head{raw_head};
    if (!git_reference_is_branch(head.get())) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "cannot synchronize a detached repository HEAD"));
    }
    const char* branch = git_reference_shorthand(head.get());
    if (branch == nullptr) {
        return std::unexpected(git_error("cannot read synchronization branch"));
    }

    git_remote* raw_remote = nullptr;
    if (git_remote_lookup(
            &raw_remote, repository->get(), options.remote.c_str()) < 0) {
        return std::unexpected(git_error("cannot open configured remote"));
    }
    Remote remote{raw_remote};
    CredentialPayload credentials{.reference = options.credential_reference};
    git_fetch_options fetch = GIT_FETCH_OPTIONS_INIT;
    fetch.callbacks.credentials = credential_callback;
    fetch.callbacks.payload = &credentials;
    const int fetched = git_remote_fetch(remote.get(), nullptr, &fetch, nullptr);
    if (fetched < 0) {
        return std::unexpected(git_push_error("cannot fetch save history", fetched));
    }

    const std::string remote_reference = "refs/remotes/" + options.remote
        + "/" + branch;
    git_reference* raw_remote_reference = nullptr;
    if (git_reference_lookup(
            &raw_remote_reference, repository->get(), remote_reference.c_str()) < 0) {
        return std::unexpected(git_error("cannot find fetched save branch"));
    }
    Reference remote_branch{raw_remote_reference};
    git_annotated_commit* raw_annotated = nullptr;
    if (git_annotated_commit_from_ref(
            &raw_annotated, repository->get(), remote_branch.get()) < 0) {
        return std::unexpected(git_error("cannot inspect fetched save branch"));
    }
    AnnotatedCommit annotated{raw_annotated};
    const git_annotated_commit* heads[] = {annotated.get()};
    git_merge_analysis_t analysis{};
    git_merge_preference_t preference{};
    if (git_merge_analysis(
            &analysis, &preference, repository->get(), heads, 1) < 0) {
        return std::unexpected(git_error("cannot analyze save history integration"));
    }
    if ((analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) != 0) {
        return IntegrateOutcome::up_to_date;
    }
    if ((analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) != 0) {
        git_reference* raw_updated = nullptr;
        if (git_reference_set_target(
                &raw_updated, head.get(), git_annotated_commit_id(annotated.get()),
                "G-SAVE fast-forward") < 0) {
            return std::unexpected(git_error("cannot fast-forward save branch"));
        }
        Reference updated{raw_updated};
        if (auto checked_out = checkout_head(repository->get()); !checked_out) {
            return std::unexpected(checked_out.error());
        }
        return IntegrateOutcome::fast_forward;
    }
    if ((analysis & GIT_MERGE_ANALYSIS_NORMAL) == 0) {
        return std::unexpected(make_error(
            std::errc::not_supported, "save history cannot be integrated automatically"));
    }
    return IntegrateOutcome::diverged;
}

Result<TimelineResolution> resolve_divergence(
    const IntegrateOptions& options,
    const TimelineChoice choice) {
    GitRuntime runtime;
    if (!runtime.initialized()) {
        return std::unexpected(git_error("cannot initialize libgit2"));
    }
    auto repository = open_repository(options.repository);
    if (!repository) return std::unexpected(repository.error());
    if (auto clean = ensure_clean_worktree(repository->get()); !clean) {
        return std::unexpected(clean.error());
    }

    git_reference* raw_head = nullptr;
    if (git_repository_head(&raw_head, repository->get()) < 0) {
        return std::unexpected(git_error("cannot resolve local timeline"));
    }
    Reference head{raw_head};
    if (!git_reference_is_branch(head.get())) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "cannot resolve divergence from detached HEAD"));
    }
    const char* branch = git_reference_shorthand(head.get());
    const char* local_reference_name = git_reference_name(head.get());
    const git_oid* local_target = git_reference_target(head.get());
    if (branch == nullptr || local_reference_name == nullptr || local_target == nullptr) {
        return std::unexpected(git_error("cannot inspect local timeline"));
    }
    const git_oid local_oid = *local_target;
    const std::string remote_reference = "refs/remotes/" + options.remote
        + "/" + branch;
    git_reference* raw_remote_branch = nullptr;
    if (git_reference_lookup(
            &raw_remote_branch, repository->get(), remote_reference.c_str()) < 0) {
        return std::unexpected(git_error("cannot find fetched remote timeline"));
    }
    Reference remote_branch{raw_remote_branch};
    const git_oid* remote_target = git_reference_target(remote_branch.get());
    if (remote_target == nullptr || git_oid_equal(&local_oid, remote_target) != 0) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "save timelines are not divergent"));
    }
    const git_oid remote_oid = *remote_target;
    const int local_contains_remote = git_graph_descendant_of(
        repository->get(), &local_oid, &remote_oid);
    const int remote_contains_local = git_graph_descendant_of(
        repository->get(), &remote_oid, &local_oid);
    if (local_contains_remote < 0 || remote_contains_local < 0) {
        return std::unexpected(git_error("cannot compare divergent timelines"));
    }
    if (local_contains_remote != 0 || remote_contains_local != 0) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "save timelines no longer require a divergence choice"));
    }

    const bool local_main = choice == TimelineChoice::local_as_main;
    const git_oid& preserved_oid = local_main ? remote_oid : local_oid;
    const std::string preserved_role = local_main ? "remote" : "local";
    const std::string preserved_branch = "refs/heads/gsave/conflicts/"
        + std::string{branch} + "/" + preserved_role + "-"
        + oid_text(preserved_oid).substr(0, 12);
    if (auto preserved = create_preserved_branch(
            repository->get(), preserved_branch, preserved_oid); !preserved) {
        return std::unexpected(preserved.error());
    }

    git_remote* raw_remote = nullptr;
    if (git_remote_lookup(
            &raw_remote, repository->get(), options.remote.c_str()) < 0) {
        return std::unexpected(git_error("cannot open configured remote"));
    }
    Remote remote{raw_remote};
    CredentialPayload credentials{.reference = options.credential_reference};
    if (auto published = push_refspec(
            remote.get(), preserved_branch + ":" + preserved_branch,
            credentials); !published) {
        return std::unexpected(published.error());
    }

    if (local_main) {
        const std::string main_refspec = "+" + std::string{local_reference_name}
            + ":" + std::string{local_reference_name};
        if (auto published = push_refspec(
                remote.get(), main_refspec, credentials); !published) {
            return std::unexpected(published.error());
        }
    } else {
        git_reference* raw_updated = nullptr;
        if (git_reference_set_target(
                &raw_updated, head.get(), &remote_oid,
                "G-SAVE choose remote timeline") < 0) {
            return std::unexpected(git_error("cannot select remote timeline"));
        }
        Reference updated{raw_updated};
        if (auto checked_out = checkout_head(repository->get()); !checked_out) {
            git_reference* raw_rollback = nullptr;
            static_cast<void>(git_reference_set_target(
                &raw_rollback, updated.get(), &local_oid,
                "G-SAVE rollback failed timeline checkout"));
            if (raw_rollback != nullptr) git_reference_free(raw_rollback);
            static_cast<void>(checkout_head(repository->get()));
            return std::unexpected(checked_out.error());
        }
    }

    return TimelineResolution{
        .main_commit = oid_text(local_main ? local_oid : remote_oid),
        .preserved_commit = oid_text(preserved_oid),
        .preserved_branch = preserved_branch,
    };
}
#endif

}  // namespace gsave::repository
