#include "gsave/repository/repository_engine.hpp"

#include <git2.h>
#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace gsave::repository {
namespace {

class RepositoryLayout final {
public:
    RepositoryLayout() {
        static std::atomic_uint64_t sequence{};
        root = std::filesystem::temp_directory_path()
            / (L"gsave-repository-test-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(sequence.fetch_add(1)));
        repository = root / L"save";
        remote = root / L"remote.git";
        parser = root / L"adapter.lua";
        std::error_code stale;
        std::filesystem::remove_all(root, stale);
        std::filesystem::create_directories(repository);
        write(repository / L"slot.sav", "round-0");
        write(repository / L"secondary.sav", "secondary");
        write(parser, R"lua(
function parse(repository, changed_files)
    return {
        repository = repository.path,
        changed_files = changed_files,
        files = repository:files(),
        os_visible = os ~= nil,
        io_visible = io ~= nil,
    }
end
)lua");
    }

    ~RepositoryLayout() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    static void write(const std::filesystem::path& path, const std::string& content) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output) << path.string();
        output << content;
        ASSERT_TRUE(output);
    }

    std::filesystem::path root;
    std::filesystem::path repository;
    std::filesystem::path remote;
    std::filesystem::path parser;
};

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::size_t commit_count(const std::filesystem::path& repository_path) {
    EXPECT_GE(git_libgit2_init(), 1);
    git_repository* repository = nullptr;
    EXPECT_EQ(git_repository_open(&repository, path_utf8(repository_path).c_str()), 0);
    if (repository == nullptr) return 0;
    git_revwalk* walk = nullptr;
    EXPECT_EQ(git_revwalk_new(&walk, repository), 0);
    EXPECT_EQ(git_revwalk_push_head(walk), 0);
    std::size_t count = 0;
    git_oid id{};
    while (git_revwalk_next(&id, walk) == 0) ++count;
    git_revwalk_free(walk);
    git_repository_free(repository);
    git_libgit2_shutdown();
    return count;
}

[[nodiscard]] std::string head_message(const std::filesystem::path& repository_path) {
    git_libgit2_init();
    git_repository* repository = nullptr;
    EXPECT_EQ(git_repository_open(&repository, path_utf8(repository_path).c_str()), 0);
    git_oid head{};
    EXPECT_EQ(git_reference_name_to_id(&head, repository, "HEAD"), 0);
    git_commit* commit = nullptr;
    EXPECT_EQ(git_commit_lookup(&commit, repository, &head), 0);
    std::string message = commit == nullptr ? "" : git_commit_message(commit);
    git_commit_free(commit);
    git_repository_free(repository);
    git_libgit2_shutdown();
    return message;
}

[[nodiscard]] std::string reference_oid(
    const std::filesystem::path& repository_path,
    const std::string_view reference_name) {
    git_libgit2_init();
    git_repository* repository = nullptr;
    EXPECT_EQ(git_repository_open(&repository, path_utf8(repository_path).c_str()), 0);
    if (repository == nullptr) {
        git_libgit2_shutdown();
        return {};
    }
    git_oid oid{};
    EXPECT_EQ(git_reference_name_to_id(
        &oid, repository, std::string{reference_name}.c_str()), 0);
    char text[GIT_OID_HEXSZ + 1]{};
    git_oid_tostr(text, sizeof(text), &oid);
    git_repository_free(repository);
    git_libgit2_shutdown();
    return text;
}

[[nodiscard]] std::string head_reference_name(
    const std::filesystem::path& repository_path) {
    git_libgit2_init();
    git_repository* repository = nullptr;
    EXPECT_EQ(git_repository_open(&repository, path_utf8(repository_path).c_str()), 0);
    if (repository == nullptr) {
        git_libgit2_shutdown();
        return {};
    }
    git_reference* head = nullptr;
    EXPECT_EQ(git_repository_head(&head, repository), 0);
    std::string name;
    if (head != nullptr && git_reference_name(head) != nullptr) {
        name = git_reference_name(head);
    }
    git_reference_free(head);
    git_repository_free(repository);
    git_libgit2_shutdown();
    return name;
}

[[nodiscard]] std::string slot_contents(const std::filesystem::path& repository_path) {
    std::ifstream input(repository_path / L"slot.sav", std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void add_local_remote(RepositoryLayout& layout) {
    git_libgit2_init();
    git_repository* bare = nullptr;
    ASSERT_EQ(git_repository_init(&bare, path_utf8(layout.remote).c_str(), 1), 0);
    git_repository_free(bare);
    git_repository* source = nullptr;
    ASSERT_EQ(git_repository_open(&source, path_utf8(layout.repository).c_str()), 0);
    git_remote* remote = nullptr;
    ASSERT_EQ(git_remote_create(
        &remote, source, "origin", path_utf8(layout.remote).c_str()), 0);
    git_remote_free(remote);
    git_repository_free(source);
    git_libgit2_shutdown();
}

TEST(RepositoryEngine, InitializesInPlaceAndCreatesMetadataBaseline) {
    RepositoryLayout layout;
    const auto initialized = initialize_repository({
        .repository = layout.repository,
        .game_id = "test-game",
        .parser = layout.parser,
    });

    ASSERT_TRUE(initialized) << initialized.error().message();
    EXPECT_EQ(*initialized, CommitOutcome::created);
    EXPECT_TRUE(std::filesystem::is_directory(layout.repository / L".git"));
    EXPECT_EQ(commit_count(layout.repository), 1U);
    const auto message = head_message(layout.repository);
    EXPECT_NE(message.find("initial-baseline"), std::string::npos);
    EXPECT_NE(message.find("\"os_visible\":false"), std::string::npos);
    EXPECT_NE(message.find("\"io_visible\":false"), std::string::npos);
}

TEST(RepositoryEngine, RepeatedWritesCreateExactlyOneCommitPerStableRound) {
    RepositoryLayout layout;
    ASSERT_EQ(*initialize_repository({layout.repository, "test-game", layout.parser}),
              CommitOutcome::created);

    for (int round = 1; round <= 4; ++round) {
        RepositoryLayout::write(
            layout.repository / L"slot.sav", "round-" + std::to_string(round));
        const auto committed = commit_repository({
            .repository = layout.repository,
            .game_id = "test-game",
            .parser = layout.parser,
            .reason = "quiet",
        });
        ASSERT_TRUE(committed) << committed.error().message();
        EXPECT_EQ(*committed, CommitOutcome::created);
        const auto duplicate = commit_repository({
            layout.repository, "test-game", layout.parser, "quiet"});
        ASSERT_TRUE(duplicate) << duplicate.error().message();
        EXPECT_EQ(*duplicate, CommitOutcome::no_changes);
    }
    EXPECT_EQ(commit_count(layout.repository), 5U);
}

TEST(RepositoryEngine, ReinstallingSupportRefreshesMetadataWithoutFileChanges) {
    RepositoryLayout layout;
    ASSERT_EQ(*initialize_repository({layout.repository, "generic", layout.parser}),
              CommitOutcome::created);
    const auto original_count = commit_count(layout.repository);
    RepositoryLayout::write(layout.parser, R"lua(
function parse(repository, changed_files)
    return { game_id = "specialized", slots = {
        { index = 1, occupied = true, character_name = "Knight" }
    } }
end
)lua");

    auto refreshed = initialize_repository({
        .repository = layout.repository,
        .game_id = "specialized",
        .parser = layout.parser,
    });

    ASSERT_TRUE(refreshed) << refreshed.error().message();
    EXPECT_EQ(*refreshed, CommitOutcome::created);
    EXPECT_EQ(commit_count(layout.repository), original_count + 1U);
    const auto message = head_message(layout.repository);
    EXPECT_NE(message.find("\"game_id\":\"specialized\""), std::string::npos);
    EXPECT_NE(message.find("\"character_name\":\"Knight\""), std::string::npos);

    auto ordinary = commit_repository({
        .repository = layout.repository,
        .game_id = "specialized",
        .parser = layout.parser,
        .reason = "quiet",
    });
    ASSERT_TRUE(ordinary) << ordinary.error().message();
    EXPECT_EQ(*ordinary, CommitOutcome::no_changes);
    EXPECT_EQ(commit_count(layout.repository), original_count + 1U);
}

TEST(RepositoryEngine, TracksDeletionAndHonorsExistingIndexLock) {
    RepositoryLayout layout;
    ASSERT_TRUE(initialize_repository({layout.repository, "test-game", layout.parser}));
    ASSERT_TRUE(std::filesystem::remove(layout.repository / L"secondary.sav"));
    auto deletion = commit_repository({layout.repository, "test-game", layout.parser, "quiet"});
    ASSERT_TRUE(deletion) << deletion.error().message();
    EXPECT_EQ(*deletion, CommitOutcome::created);

    RepositoryLayout::write(layout.repository / L"slot.sav", "locked-change");
    RepositoryLayout::write(layout.repository / L".git" / L"index.lock", "owned elsewhere");
    const auto locked = commit_repository({layout.repository, "test-game", layout.parser, "quiet"});
    EXPECT_FALSE(locked);
    EXPECT_TRUE(std::filesystem::exists(layout.repository / L".git" / L"index.lock"));
}

TEST(RepositoryEngine, PushesEveryCommittedRoundToLocalBareRemote) {
    RepositoryLayout layout;
    ASSERT_TRUE(initialize_repository({layout.repository, "test-game", layout.parser}));
    add_local_remote(layout);
    auto first_push = push_repository({layout.repository, "origin", {}});
    ASSERT_TRUE(first_push) << first_push.error().message();

    for (int round = 1; round <= 3; ++round) {
        RepositoryLayout::write(
            layout.repository / L"slot.sav", "pushed-round-" + std::to_string(round));
        ASSERT_EQ(*commit_repository({layout.repository, "test-game", layout.parser, "quiet"}),
                  CommitOutcome::created);
        auto pushed = push_repository({layout.repository, "origin", {}});
        ASSERT_TRUE(pushed) << pushed.error().message();
    }
    EXPECT_EQ(commit_count(layout.repository), 4U);
    EXPECT_EQ(commit_count(layout.remote), 4U);
}

TEST(RepositoryEngine, TestsPushDirectionConnectionWithoutChangingRemote) {
    RepositoryLayout layout;
    ASSERT_TRUE(initialize_repository({
        .repository = layout.repository,
        .game_id = "test-game",
        .parser = layout.parser,
    }));
    add_local_remote(layout);
    ASSERT_TRUE(push_repository({layout.repository, "origin", {}}));
    const auto before = commit_count(layout.remote);

    auto tested = test_remote_connection({
        .repository = layout.repository,
        .url = path_utf8(layout.remote),
    });

    ASSERT_TRUE(tested) << tested.error().message();
    EXPECT_GT(tested->advertised_references, 0U);
    EXPECT_EQ(commit_count(layout.remote), before);
}

TEST(RepositoryEngine, ListsMetadataHistoryAndRestoresWithoutRewritingHistory) {
    RepositoryLayout layout;
    ASSERT_EQ(*initialize_repository({
        .repository = layout.repository,
        .game_id = "test-game",
        .parser = layout.parser,
        .exclude_patterns = {"cache.tmp"},
    }), CommitOutcome::created);
    RepositoryLayout::write(layout.repository / L"cache.tmp", "ignored");
    auto info = inspect_repository(layout.repository);
    ASSERT_TRUE(info) << info.error().message();
    EXPECT_FALSE(info->worktree_dirty);

    RepositoryLayout::write(layout.repository / L"slot.sav", "round-1");
    ASSERT_EQ(*commit_repository({layout.repository, "test-game", layout.parser, "quiet"}),
              CommitOutcome::created);
    RepositoryLayout::write(layout.repository / L"slot.sav", "round-2");
    ASSERT_EQ(*commit_repository({layout.repository, "test-game", layout.parser, "quiet"}),
              CommitOutcome::created);

    auto history = list_history(layout.repository);
    ASSERT_TRUE(history) << history.error().message();
    ASSERT_EQ(history->size(), 3U);
    EXPECT_FALSE(history->front().metadata_json.empty());
    auto restored = restore_repository({
        .repository = layout.repository,
        .commit_id = history->back().id,
        .game_id = "test-game",
        .parser = layout.parser,
    });
    ASSERT_TRUE(restored) << restored.error().message();
    EXPECT_EQ(*restored, CommitOutcome::created);
    std::ifstream input(layout.repository / L"slot.sav", std::ios::binary);
    std::string content;
    input >> content;
    EXPECT_EQ(content, "round-0");
    EXPECT_EQ(commit_count(layout.repository), 4U);
}

struct DivergenceState final {
    std::filesystem::path other;
    std::string main_reference;
    std::string local_oid;
    std::string remote_oid;
};

void create_divergent_timelines(
    RepositoryLayout& layout,
    DivergenceState& state) {
    ASSERT_TRUE(initialize_repository({layout.repository, "test-game", layout.parser}));
    add_local_remote(layout);
    ASSERT_TRUE(push_repository({layout.repository, "origin", {}}));

    state.other = layout.root / L"other";
    git_libgit2_init();
    git_repository* clone = nullptr;
    ASSERT_EQ(git_clone(
        &clone, path_utf8(layout.remote).c_str(), path_utf8(state.other).c_str(), nullptr), 0);
    git_repository_free(clone);
    git_libgit2_shutdown();
    RepositoryLayout::write(state.other / L"slot.sav", "remote-1");
    ASSERT_EQ(*commit_repository({state.other, "test-game", layout.parser, "remote"}),
              CommitOutcome::created);
    ASSERT_TRUE(push_repository({state.other, "origin", {}}));

    auto fast_forward = integrate_repository({layout.repository, "origin", {}});
    ASSERT_TRUE(fast_forward) << fast_forward.error().message();
    ASSERT_EQ(*fast_forward, IntegrateOutcome::fast_forward);

    RepositoryLayout::write(layout.repository / L"slot.sav", "local-choice");
    ASSERT_EQ(*commit_repository({layout.repository, "test-game", layout.parser, "local"}),
              CommitOutcome::created);
    RepositoryLayout::write(state.other / L"slot.sav", "remote-choice");
    ASSERT_EQ(*commit_repository({state.other, "test-game", layout.parser, "remote"}),
              CommitOutcome::created);
    ASSERT_TRUE(push_repository({state.other, "origin", {}}));

    state.main_reference = head_reference_name(layout.repository);
    state.local_oid = reference_oid(layout.repository, "HEAD");
    state.remote_oid = reference_oid(layout.remote, state.main_reference);
    ASSERT_FALSE(state.main_reference.empty());
    ASSERT_FALSE(state.local_oid.empty());
    ASSERT_FALSE(state.remote_oid.empty());
    ASSERT_NE(state.local_oid, state.remote_oid);
}

TEST(RepositoryEngine, ReportsDivergenceWithoutMergingThenKeepsLocalTimeline) {
    RepositoryLayout layout;
    DivergenceState state;
    create_divergent_timelines(layout, state);

    auto diverged = integrate_repository({layout.repository, "origin", {}});
    ASSERT_TRUE(diverged) << diverged.error().message();
    EXPECT_EQ(*diverged, IntegrateOutcome::diverged);
    EXPECT_EQ(reference_oid(layout.repository, "HEAD"), state.local_oid);
    EXPECT_EQ(slot_contents(layout.repository), "local-choice");

    auto resolved = resolve_divergence(
        {layout.repository, "origin", {}}, TimelineChoice::local_as_main);
    ASSERT_TRUE(resolved) << resolved.error().message();
    EXPECT_EQ(resolved->main_commit, state.local_oid);
    EXPECT_EQ(resolved->preserved_commit, state.remote_oid);
    EXPECT_EQ(reference_oid(layout.repository, "HEAD"), state.local_oid);
    EXPECT_EQ(reference_oid(layout.repository, resolved->preserved_branch), state.remote_oid);
    EXPECT_EQ(reference_oid(layout.remote, state.main_reference), state.local_oid);
    EXPECT_EQ(reference_oid(layout.remote, resolved->preserved_branch), state.remote_oid);
    EXPECT_EQ(slot_contents(layout.repository), "local-choice");
    auto clean = inspect_repository(layout.repository);
    ASSERT_TRUE(clean) << clean.error().message();
    EXPECT_EQ(clean->ahead, 0U);
    EXPECT_EQ(clean->behind, 0U);
}

TEST(RepositoryEngine, KeepsRemoteTimelineAndPreservesLocalTimelineAsBranch) {
    RepositoryLayout layout;
    DivergenceState state;
    create_divergent_timelines(layout, state);

    auto diverged = integrate_repository({layout.repository, "origin", {}});
    ASSERT_TRUE(diverged) << diverged.error().message();
    ASSERT_EQ(*diverged, IntegrateOutcome::diverged);
    auto resolved = resolve_divergence(
        {layout.repository, "origin", {}}, TimelineChoice::remote_as_main);
    ASSERT_TRUE(resolved) << resolved.error().message();
    EXPECT_EQ(resolved->main_commit, state.remote_oid);
    EXPECT_EQ(resolved->preserved_commit, state.local_oid);
    EXPECT_EQ(reference_oid(layout.repository, "HEAD"), state.remote_oid);
    EXPECT_EQ(reference_oid(layout.repository, resolved->preserved_branch), state.local_oid);
    EXPECT_EQ(reference_oid(layout.remote, state.main_reference), state.remote_oid);
    EXPECT_EQ(reference_oid(layout.remote, resolved->preserved_branch), state.local_oid);
    EXPECT_EQ(slot_contents(layout.repository), "remote-choice");

    auto clean = inspect_repository(layout.repository);
    ASSERT_TRUE(clean) << clean.error().message();
    EXPECT_EQ(clean->ahead, 0U);
    EXPECT_EQ(clean->behind, 0U);
}

TEST(RepositoryEngine, OpensAnOlderSaveAsANamedBranchAndNeverDetachesHead) {
    RepositoryLayout layout;
    ASSERT_EQ(*initialize_repository({layout.repository, "test-game", layout.parser}),
              CommitOutcome::created);
    const auto baseline = reference_oid(layout.repository, "HEAD");
    const auto original_branch = head_reference_name(layout.repository);

    RepositoryLayout::write(layout.repository / L"slot.sav", "later-progress");
    ASSERT_TRUE(commit_repository({
        layout.repository, "test-game", layout.parser, "quiet"}));
    const auto latest = reference_oid(layout.repository, "HEAD");
    ASSERT_NE(baseline, latest);

    auto suggested = suggest_branch_name(layout.repository, baseline);
    ASSERT_TRUE(suggested) << suggested.error().message();
    EXPECT_TRUE(suggested->starts_with("save/"));
    EXPECT_NE(suggested->find(baseline.substr(0, 8)), std::string::npos);

    auto created = create_branch_from_commit({
        .repository = layout.repository,
        .commit_id = baseline,
        .branch = *suggested,
    });
    ASSERT_TRUE(created) << created.error().message();

    // The worktree holds the older save, HEAD names the new branch and the
    // repository is not detached, so commit, push and integrate all keep working.
    EXPECT_EQ(slot_contents(layout.repository), "round-0");
    EXPECT_EQ(head_reference_name(layout.repository), "refs/heads/" + *suggested);
    EXPECT_EQ(reference_oid(layout.repository, "HEAD"), baseline);
    EXPECT_EQ(reference_oid(layout.repository, "refs/heads/" + *suggested), baseline);
    EXPECT_EQ(reference_oid(layout.repository, original_branch), latest);

    // Playing on the new timeline must produce commits, which is exactly what a
    // detached HEAD would have made unpushable.
    RepositoryLayout::write(layout.repository / L"slot.sav", "branch-progress");
    auto committed = commit_repository({
        layout.repository, "test-game", layout.parser, "quiet"});
    ASSERT_TRUE(committed) << committed.error().message();
    EXPECT_EQ(*committed, CommitOutcome::created);
    EXPECT_EQ(head_reference_name(layout.repository), "refs/heads/" + *suggested);

    auto branches = list_branches(layout.repository);
    ASSERT_TRUE(branches) << branches.error().message();
    ASSERT_EQ(branches->size(), 2U);
    const auto current = std::ranges::find_if(
        *branches, [](const BranchInfo& branch) { return branch.current; });
    ASSERT_NE(current, branches->end());
    EXPECT_EQ(current->name, *suggested);
}

TEST(RepositoryEngine, SwitchesBetweenSaveTimelinesAndRestoresEachWorktree) {
    RepositoryLayout layout;
    ASSERT_EQ(*initialize_repository({layout.repository, "test-game", layout.parser}),
              CommitOutcome::created);
    const auto baseline = reference_oid(layout.repository, "HEAD");
    const auto main_branch = head_reference_name(layout.repository);
    const auto main_name = main_branch.substr(std::string_view{"refs/heads/"}.size());

    RepositoryLayout::write(layout.repository / L"slot.sav", "main-progress");
    ASSERT_TRUE(commit_repository({
        layout.repository, "test-game", layout.parser, "quiet"}));

    auto suggested = suggest_branch_name(layout.repository, baseline);
    ASSERT_TRUE(suggested) << suggested.error().message();
    ASSERT_TRUE(create_branch_from_commit({
        layout.repository, baseline, *suggested}));
    RepositoryLayout::write(layout.repository / L"slot.sav", "side-progress");
    ASSERT_TRUE(commit_repository({
        layout.repository, "test-game", layout.parser, "quiet"}));
    EXPECT_EQ(slot_contents(layout.repository), "side-progress");

    auto back = switch_branch(layout.repository, main_name);
    ASSERT_TRUE(back) << back.error().message();
    EXPECT_EQ(head_reference_name(layout.repository), main_branch);
    EXPECT_EQ(slot_contents(layout.repository), "main-progress");

    auto forward = switch_branch(layout.repository, *suggested);
    ASSERT_TRUE(forward) << forward.error().message();
    EXPECT_EQ(head_reference_name(layout.repository), "refs/heads/" + *suggested);
    EXPECT_EQ(slot_contents(layout.repository), "side-progress");
}

TEST(RepositoryEngine, RefusesTimelineOperationsThatWouldLoseUncommittedSaves) {
    RepositoryLayout layout;
    ASSERT_EQ(*initialize_repository({layout.repository, "test-game", layout.parser}),
              CommitOutcome::created);
    const auto baseline = reference_oid(layout.repository, "HEAD");
    const auto branch = head_reference_name(layout.repository);
    RepositoryLayout::write(layout.repository / L"slot.sav", "unsaved-progress");

    auto created = create_branch_from_commit({
        layout.repository, baseline, "save/manual"});
    EXPECT_FALSE(created);
    auto switched = switch_branch(
        layout.repository, branch.substr(std::string_view{"refs/heads/"}.size()));
    EXPECT_FALSE(switched);
    EXPECT_EQ(slot_contents(layout.repository), "unsaved-progress");
    EXPECT_EQ(head_reference_name(layout.repository), branch);
}

TEST(RepositoryEngine, RejectsInvalidTimelineNamesAndMissingTimelines) {
    RepositoryLayout layout;
    ASSERT_EQ(*initialize_repository({layout.repository, "test-game", layout.parser}),
              CommitOutcome::created);
    const auto baseline = reference_oid(layout.repository, "HEAD");

    EXPECT_FALSE(create_branch_from_commit({layout.repository, baseline, ""}));
    EXPECT_FALSE(create_branch_from_commit({layout.repository, baseline, "bad name"}));
    EXPECT_FALSE(create_branch_from_commit({layout.repository, baseline, "bad..name"}));
    EXPECT_FALSE(create_branch_from_commit({layout.repository, "", "save/ok"}));
    EXPECT_FALSE(switch_branch(layout.repository, "save/never-created"));
}

}  // namespace
}  // namespace gsave::repository
