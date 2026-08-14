#include "gsave/core/app.hpp"

#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace gsave::core {
namespace {

using namespace std::chrono_literals;

class TemporaryLayout final {
public:
    explicit TemporaryLayout(const std::size_t repository_count = 1) {
        static std::atomic_uint64_t sequence{};
        root = std::filesystem::temp_directory_path()
            / (L"gsave-core-test-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(sequence.fetch_add(1)));
        std::error_code stale;
        std::filesystem::remove_all(root, stale);
        std::filesystem::create_directories(root / L"packages" / L"test-game");
        touch(root / L"game.exe");
        touch(root / L"packages" / L"test-game" / L"adapter.lua");
        for (std::size_t index = 0; index < repository_count; ++index) {
            auto repository = root / (L"save-" + std::to_wstring(index));
            std::filesystem::create_directories(repository / L".git");
            std::ofstream(repository / L".git" / L"HEAD", std::ios::binary)
                << "ref: refs/heads/main\n";
            repositories.push_back(std::move(repository));
        }
    }

    ~TemporaryLayout() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    static void touch(const std::filesystem::path& path) {
        std::ofstream(path, std::ios::binary) << "test";
    }

    std::filesystem::path root;
    std::vector<std::filesystem::path> repositories;
};

class FakeProcessEvents final : public platform::ProcessEventSource {
public:
    Status start(platform::ProcessEventSink sink) override {
        sink_ = std::move(sink);
        for (const auto& event : initial) sink_(event);
        return {};
    }
    void stop() noexcept override { sink_ = {}; }
    void emit(const platform::ProcessEvent& event) const { sink_(event); }

    std::vector<platform::ProcessEvent> initial;

private:
    platform::ProcessEventSink sink_;
};

class FakeWatcher final : public platform::DirectoryWatcher {
public:
    Status add(const platform::DirectoryWatchRequest& request) override {
        active.insert(request.repository);
        added.push_back(request);
        return {};
    }
    Status remove(const platform::RepositoryKey repository) override {
        active.erase(repository);
        removed.push_back(repository);
        return {};
    }
    Result<std::optional<platform::DirectoryEvent>> poll(
        std::chrono::milliseconds) override {
        if (events.empty()) return std::optional<platform::DirectoryEvent>{};
        auto event = std::move(events.front());
        events.pop_front();
        return std::optional<platform::DirectoryEvent>{std::move(event)};
    }
    Status wake() override {
        wake_count.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    void stop() noexcept override { active.clear(); }

    void emit(platform::DirectoryEvent event) { events.push_back(std::move(event)); }

    std::vector<platform::DirectoryWatchRequest> added;
    std::vector<platform::RepositoryKey> removed;
    std::unordered_set<platform::RepositoryKey> active;
    std::deque<platform::DirectoryEvent> events;
    std::atomic_int wake_count{};
};

class RepositoryProbe final {
public:
    static Result<repository::CommitOutcome> commit(
        void* context,
        const repository::CommitOptions& request) {
        auto& self = *static_cast<RepositoryProbe*>(context);
        std::unique_lock lock(self.mutex);
        self.commits.push_back(request);
        self.changed.notify_all();
        self.changed.wait(lock, [&self] {
            return !self.block_commits || self.commit_permits != 0;
        });
        if (self.block_commits) --self.commit_permits;
        if (!self.commit_errors.empty()) {
            auto error = std::move(self.commit_errors.front());
            self.commit_errors.pop_front();
            return std::unexpected(std::move(error));
        }
        return self.commit_result;
    }

    static Status push(
        void* context,
        const repository::PushOptions& request) {
        auto& self = *static_cast<RepositoryProbe*>(context);
        std::unique_lock lock(self.mutex);
        self.pushes.push_back(request);
        self.changed.notify_all();
        self.changed.wait(lock, [&self] {
            return !self.block_pushes || self.push_permits != 0;
        });
        if (self.block_pushes) --self.push_permits;
        if (!self.push_errors.empty()) {
            auto error = std::move(self.push_errors.front());
            self.push_errors.pop_front();
            return std::unexpected(std::move(error));
        }
        return {};
    }

    static void warning(void* context, const Error&) {
        auto& self = *static_cast<RepositoryProbe*>(context);
        std::scoped_lock lock(self.mutex);
        ++self.warning_count_value;
        self.changed.notify_all();
    }

    void fail_commits(const std::size_t count) {
        std::scoped_lock lock(mutex);
        for (std::size_t index = 0; index < count; ++index) {
            commit_errors.push_back(make_error(
                std::errc::device_or_resource_busy, "save file is temporarily busy"));
        }
    }

    void fail_pushes(const std::size_t count) {
        std::scoped_lock lock(mutex);
        for (std::size_t index = 0; index < count; ++index) {
            push_errors.push_back(make_error(
                std::errc::network_unreachable, "temporary network failure"));
        }
    }

    void block_commit_tasks() {
        std::scoped_lock lock(mutex);
        block_commits = true;
    }
    void block_push_tasks() {
        std::scoped_lock lock(mutex);
        block_pushes = true;
    }
    void release_commits(const std::size_t count) {
        {
            std::scoped_lock lock(mutex);
            commit_permits += count;
        }
        changed.notify_all();
    }
    void release_pushes(const std::size_t count) {
        {
            std::scoped_lock lock(mutex);
            push_permits += count;
        }
        changed.notify_all();
    }
    [[nodiscard]] bool wait_for_commits(
        const std::size_t count,
        const std::chrono::milliseconds timeout = 2s) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, timeout, [&] { return commits.size() >= count; });
    }
    [[nodiscard]] std::size_t commit_count() const {
        std::scoped_lock lock(mutex);
        return commits.size();
    }
    [[nodiscard]] std::size_t push_count() const {
        std::scoped_lock lock(mutex);
        return pushes.size();
    }
    [[nodiscard]] std::size_t warning_count() const {
        std::scoped_lock lock(mutex);
        return warning_count_value;
    }
    [[nodiscard]] std::vector<repository::CommitOptions> commit_requests() const {
        std::scoped_lock lock(mutex);
        return commits;
    }
    repository::CommitOutcome commit_result{repository::CommitOutcome::created};
private:
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::vector<repository::CommitOptions> commits;
    std::vector<repository::PushOptions> pushes;
    std::deque<Error> commit_errors;
    std::deque<Error> push_errors;
    std::size_t warning_count_value{};
    std::size_t commit_permits{};
    std::size_t push_permits{};
    bool block_commits{};
    bool block_pushes{};
};

struct AppHarness final {
    explicit AppHarness(
        TemporaryLayout& layout,
        CommitPolicy commit = CommitPolicy{
            .strategy = CommitStrategy::on_exit,
            .commit_on_exit = true,
        },
        config::SyncPolicy sync = config::SyncPolicy{
            .backend = config::SyncBackend::git,
            .trigger = config::SyncTrigger::manual,
        },
        std::vector<std::string> include_globs = {},
        std::vector<std::string> exclude_globs = {}) {
        config::GameConfig game{
            .id = "test-game",
            .enabled = true,
            .process_name = "game.exe",
            .process_path = layout.root / L"game.exe",
            .parser = std::filesystem::path{L"packages"} / L"test-game" / L"adapter.lua",
            .commit = commit,
            .sync = std::move(sync),
        };
        for (const auto& repository : layout.repositories) {
            game.saves.push_back(config::SaveConfig{
                .path = repository,
                .include_globs = include_globs,
                .exclude_globs = exclude_globs,
            });
        }
        auto process_owner = std::make_unique<FakeProcessEvents>();
        process = process_owner.get();
        auto watcher_owner = std::make_unique<FakeWatcher>();
        watcher = watcher_owner.get();
        app = std::make_unique<CoreApp>(
            CoreAppOptions{
                .config = config::Config{{std::move(game)}},
                .config_directory = layout.root,
                .repository_context = &repository,
                .commit_callback = &RepositoryProbe::commit,
                .push_callback = &RepositoryProbe::push,
                .network_warning_callback = &RepositoryProbe::warning,
            },
            std::move(process_owner), std::move(watcher_owner));
    }

    ~AppHarness() {
        repository.release_commits(64);
        repository.release_pushes(64);
    }

    [[nodiscard]] platform::ProcessEvent started(
        const TemporaryLayout& layout,
        const std::uint32_t pid) const {
        return {
            .kind = platform::ProcessEventKind::started,
            .process_id = pid,
            .process_name = L"game.exe",
            .image_path = layout.root / L"game.exe",
        };
    }
    [[nodiscard]] static platform::ProcessEvent stopped(const std::uint32_t pid) {
        return {
            .kind = platform::ProcessEventKind::stopped,
            .process_id = pid,
        };
    }

    template <typename Predicate>
    bool pump_until(Predicate predicate, const std::chrono::milliseconds timeout = 2s) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            if (!app->run_iteration(0ms)) return false;
            std::this_thread::sleep_for(1ms);
        }
        return predicate();
    }

    RepositoryProbe repository;
    FakeProcessEvents* process{};
    FakeWatcher* watcher{};
    std::unique_ptr<CoreApp> app;
};

TEST(CoreApp, AGameThatIsNotRunningIsNormalAndOpensNoSaveHandles) {
    TemporaryLayout layout;
    AppHarness harness(layout);
    const auto started = harness.app->start();
    ASSERT_TRUE(started) << started.error().message();
    EXPECT_TRUE(harness.watcher->added.empty());
    EXPECT_EQ(harness.repository.commit_count(), 0U);
}

TEST(CoreApp, ATransientControlSignalWakesTheOwnerLoopAndStopsCleanly) {
    TemporaryLayout layout;
    AppHarness harness(layout);
    ASSERT_TRUE(harness.app->start());
    const auto wakes_before = harness.watcher->wake_count.load();
    std::thread signal([&] { harness.app->request_stop(); });
    const auto stopped = harness.app->run();
    signal.join();
    ASSERT_TRUE(stopped) << stopped.error().message();
    EXPECT_GT(harness.watcher->wake_count.load(), wakes_before);
    EXPECT_TRUE(harness.watcher->active.empty());
}

TEST(CoreApp, MultipleProcessesShareOneWatchAndOnlyLastExitCommits) {
    TemporaryLayout layout;
    AppHarness harness(layout);
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 10));
    harness.process->emit(harness.started(layout, 11));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_EQ(harness.watcher->added.size(), 1U);

    harness.process->emit(AppHarness::stopped(10));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.commit_count(), 0U);
    harness.process->emit(AppHarness::stopped(11));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(1));
    EXPECT_EQ(harness.watcher->removed.size(), 1U);
}

TEST(CoreApp, DifferentRepositoriesCommitConcurrentlyWithoutResidentThreads) {
    TemporaryLayout layout(2);
    AppHarness harness(layout);
    harness.repository.block_commit_tasks();
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 20));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    harness.process->emit(AppHarness::stopped(20));
    ASSERT_TRUE(harness.app->run_iteration(0ms));

    ASSERT_TRUE(harness.repository.wait_for_commits(2));
    const auto requests = harness.repository.commit_requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_NE(requests[0].repository, requests[1].repository);
    harness.repository.release_commits(2);
}

TEST(CoreApp, FileEventsDuringACommitCollapseIntoOnePendingCommit) {
    TemporaryLayout layout;
    const CommitPolicy quiet{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    };
    AppHarness harness(layout, quiet);
    harness.repository.block_commit_tasks();
    const auto started = harness.app->start();
    ASSERT_TRUE(started) << started.error().message();
    harness.process->emit(harness.started(layout, 30));
    ASSERT_TRUE(harness.app->run_iteration(0ms));

    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"slot.sav"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(1));

    for (int index = 0; index < 4; ++index) {
        harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"slot.sav"});
        ASSERT_TRUE(harness.app->run_iteration(0ms));
    }
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.commit_count(), 1U);

    harness.repository.release_commits(1);
    ASSERT_TRUE(harness.pump_until([&] {
        return harness.repository.commit_count() == 2U;
    }));
    EXPECT_EQ(harness.repository.commit_count(), 2U);
    harness.repository.release_commits(1);
}

TEST(CoreApp, SupportPackageGlobsDiscardUnrelatedFileEventsBeforeScheduling) {
    TemporaryLayout layout;
    const CommitPolicy quiet{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    };
    AppHarness harness(
        layout, quiet, {}, {"*/DS30000.sl2"}, {"cache/**"});
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 31));
    ASSERT_TRUE(harness.app->run_iteration(0ms));

    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.pump_until([&] { return harness.repository.commit_count() == 1U; }));

    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"GraphicsConfig.xml"});
    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"cache/temp.bin"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.commit_count(), 1U);

    harness.watcher->emit({
        1, platform::DirectoryEventKind::modified,
        L"0110000100000001/DS30000.sl2"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(2));
}

TEST(CoreApp, HybridMaximumIntervalBoundsContinuousChanges) {
    TemporaryLayout layout;
    const CommitPolicy hybrid{
        .strategy = CommitStrategy::hybrid,
        .quiet_interval = 5s,
        .max_interval = 1s,
        .commit_on_exit = true,
    };
    AppHarness harness(layout, hybrid);
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 35));
    ASSERT_TRUE(harness.app->run_iteration(0ms));

    for (int event = 0; event < 3; ++event) {
        std::this_thread::sleep_for(300ms);
        harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"slot.sav"});
        ASSERT_TRUE(harness.app->run_iteration(0ms));
        EXPECT_EQ(harness.repository.commit_count(), 0U);
    }
    std::this_thread::sleep_for(150ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(1));
    const auto requests = harness.repository.commit_requests();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests.front().reason, "periodic");
}

TEST(CoreApp, ExitPushWaitsForTheRunningFinalCommit) {
    TemporaryLayout layout;
    const CommitPolicy quiet{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    };
    const config::SyncPolicy on_exit{
        .backend = config::SyncBackend::git,
        .trigger = config::SyncTrigger::on_exit,
        .credential_reference = "G-SAVE/test",
    };
    AppHarness harness(layout, quiet, on_exit);
    harness.repository.block_commit_tasks();
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 36));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(1));

    harness.process->emit(AppHarness::stopped(36));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.push_count(), 0U);
    harness.repository.release_commits(1);
    ASSERT_TRUE(harness.pump_until([&] {
        return harness.repository.push_count() == 1U;
    }));
}

TEST(CoreApp, DefaultExitSyncDoesNotPushBeforeCloudIsConnected) {
    TemporaryLayout layout;
    AppHarness harness(layout, CommitPolicy{
        .strategy = CommitStrategy::on_exit,
        .commit_on_exit = true,
    }, config::SyncPolicy{});
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 37));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    harness.process->emit(AppHarness::stopped(37));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(1));
    std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.push_count(), 0U);
}

TEST(CoreApp, ASlowPushDoesNotBlockANewerCommit) {
    TemporaryLayout layout;
    const CommitPolicy quiet{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    };
    const config::SyncPolicy on_commit{
        .backend = config::SyncBackend::git,
        .trigger = config::SyncTrigger::on_commit,
        .credential_reference = "G-SAVE/test",
    };
    AppHarness harness(layout, quiet, on_commit);
    harness.repository.block_push_tasks();
    const auto started = harness.app->start();
    ASSERT_TRUE(started) << started.error().message();
    harness.process->emit(harness.started(layout, 40));
    ASSERT_TRUE(harness.app->run_iteration(0ms));

    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"slot.sav"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(1));
    ASSERT_TRUE(harness.pump_until([&] { return harness.repository.push_count() == 1U; }));

    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"slot.sav"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.repository.wait_for_commits(2));
    EXPECT_EQ(harness.repository.push_count(), 1U);
    harness.repository.release_pushes(2);
}

TEST(CoreApp, GitMetadataNeverMarksTheSaveDirty) {
    TemporaryLayout layout;
    const CommitPolicy quiet{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    };
    AppHarness harness(layout, quiet);
    const auto started = harness.app->start();
    ASSERT_TRUE(started) << started.error().message();
    harness.process->emit(harness.started(layout, 50));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L".git/HEAD"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.commit_count(), 0U);
}

TEST(CoreApp, CommitFailureRetriesAndOnlyTheThirdConsecutiveFailureStopsCore) {
    TemporaryLayout layout;
    const CommitPolicy quiet{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    };
    AppHarness harness(layout, quiet);
    harness.repository.fail_commits(2);
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 60));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.pump_until(
        [&] { return harness.repository.commit_count() >= 3; }, 7s));
    EXPECT_TRUE(harness.app->run_iteration(0ms));

    harness.repository.fail_commits(3);
    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"slot.sav"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    std::this_thread::sleep_for(1050ms);
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.pump_until(
        [&] { return harness.repository.commit_count() >= 6; }, 7s));
    const auto stopped = harness.app->run_iteration(0ms);
    EXPECT_FALSE(stopped);
}

TEST(CoreApp, PushRetriesTransientNetworkErrorsAndDefersWarningUntilGameExit) {
    TemporaryLayout layout;
    const config::SyncPolicy on_commit{
        .backend = config::SyncBackend::git,
        .trigger = config::SyncTrigger::on_commit,
        .credential_reference = "G-SAVE/test",
    };
    AppHarness harness(layout, CommitPolicy{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    }, on_commit);
    harness.repository.fail_pushes(3);
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 61));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.pump_until(
        [&] { return harness.repository.push_count() == 3; }, 7s));
    EXPECT_EQ(harness.repository.warning_count(), 0U);
    harness.process->emit(AppHarness::stopped(61));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.warning_count(), 1U);
}

TEST(CoreApp, SuccessfulPushClearsADeferredNetworkWarning) {
    TemporaryLayout layout;
    const config::SyncPolicy on_commit{
        .backend = config::SyncBackend::git,
        .trigger = config::SyncTrigger::on_commit,
        .credential_reference = "G-SAVE/test",
    };
    AppHarness harness(layout, CommitPolicy{
        .strategy = CommitStrategy::quiet,
        .quiet_interval = 1s,
        .commit_on_exit = false,
    }, on_commit);
    harness.repository.fail_pushes(3);
    ASSERT_TRUE(harness.app->start());
    harness.process->emit(harness.started(layout, 62));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.pump_until(
        [&] { return harness.repository.push_count() == 3; }, 7s));
    EXPECT_EQ(harness.repository.warning_count(), 0U);

    harness.watcher->emit({1, platform::DirectoryEventKind::modified, L"slot.sav"});
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    ASSERT_TRUE(harness.pump_until(
        [&] { return harness.repository.push_count() == 4; }, 4s));

    harness.process->emit(AppHarness::stopped(62));
    ASSERT_TRUE(harness.app->run_iteration(0ms));
    EXPECT_EQ(harness.repository.warning_count(), 0U);
}

}  // namespace
}  // namespace gsave::core
