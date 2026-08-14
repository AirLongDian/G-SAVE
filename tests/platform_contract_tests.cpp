#include <gsave/platform/directory_watcher.hpp>
#include <gsave/platform/process_event_source.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace gsave::platform {
namespace {

TEST(DirectoryFiltering, RejectsGitMetadataAtAnyPathDepthIgnoringCase) {
    EXPECT_TRUE(is_git_metadata_path(L".git"));
    EXPECT_TRUE(is_git_metadata_path(LR"(.git\objects\ab\object)"));
    EXPECT_TRUE(is_git_metadata_path(L"slot/.GIT/index"));
    EXPECT_TRUE(is_git_metadata_path(L"account\\.gIt\\HEAD"));
}

TEST(DirectoryFiltering, PreservesOrdinarySavePaths) {
    EXPECT_FALSE(is_git_metadata_path(L""));
    EXPECT_FALSE(is_git_metadata_path(L"git"));
    EXPECT_FALSE(is_git_metadata_path(L".github\\settings"));
    EXPECT_FALSE(is_git_metadata_path(L"slot.git\\save.dat"));
    EXPECT_FALSE(is_git_metadata_path(L"saves\\legitimate.dat"));
}

class FakeProcessEventSource final : public ProcessEventSource {
public:
    std::expected<void, Error> start(ProcessEventSink sink) override {
        sink_ = std::move(sink);
        return {};
    }

    void stop() noexcept override {
        sink_ = {};
        ++stop_count;
    }

    int stop_count{};

private:
    ProcessEventSink sink_;
};

class FakeDirectoryWatcher final : public DirectoryWatcher {
public:
    std::expected<void, Error> add(
        const DirectoryWatchRequest&) override {
        return {};
    }

    std::expected<void, Error> remove(RepositoryKey) override { return {}; }

    std::expected<std::optional<DirectoryEvent>, Error> poll(
        std::chrono::milliseconds) override {
        return std::optional<DirectoryEvent>{};
    }

    Status wake() override {
        ++wake_count;
        return {};
    }

    void stop() noexcept override { ++stop_count; }

    int wake_count{};
    int stop_count{};
};

TEST(PlatformInterfaces, SupportIdempotentOwnerDrivenLifecycleWithoutWmi) {
    static_assert(std::has_virtual_destructor_v<ProcessEventSource>);
    static_assert(std::has_virtual_destructor_v<DirectoryWatcher>);

    auto process_source = std::make_unique<FakeProcessEventSource>();
    EXPECT_TRUE(process_source->start([](const ProcessEvent&) {}));
    process_source->stop();
    process_source->stop();
    EXPECT_EQ(process_source->stop_count, 2);

    auto directory_watcher = std::make_unique<FakeDirectoryWatcher>();
    EXPECT_TRUE(directory_watcher->wake());
    EXPECT_TRUE(directory_watcher->wake());
    EXPECT_EQ(directory_watcher->wake_count, 2);
    directory_watcher->stop();
    directory_watcher->stop();
    EXPECT_EQ(directory_watcher->stop_count, 2);

}

#ifdef _WIN32
class ScopedEmptyTestDirectory {
public:
    explicit ScopedEmptyTestDirectory(std::filesystem::path path)
        : path_(std::move(path)) {}

    ~ScopedEmptyTestDirectory() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

TEST(IocpDirectoryWatcher, WakeInterruptsPollWithoutFilesystemEvent) {
    auto watcher_result = make_iocp_directory_watcher();
    ASSERT_TRUE(watcher_result) << watcher_result.error().message();
    auto& watcher = *watcher_result.value();

    ASSERT_TRUE(watcher.wake());
    const auto event = watcher.poll(std::chrono::seconds{5});
    ASSERT_TRUE(event) << event.error().message();
    EXPECT_FALSE(event->has_value());

    watcher.stop();
    watcher.stop();
}

TEST(IocpDirectoryWatcher, RemoveAndStopDrainPendingOverlappedStorage) {
    const auto root = std::filesystem::temp_directory_path()
        / (L"gsave-iocp-contract-"
           + std::to_wstring(GetCurrentProcessId()) + L"-"
           + std::to_wstring(GetTickCount64()));
    ASSERT_TRUE(std::filesystem::create_directory(root));
    const ScopedEmptyTestDirectory cleanup{root};

    auto watcher_result = make_iocp_directory_watcher();
    ASSERT_TRUE(watcher_result) << watcher_result.error().message();
    auto& watcher = *watcher_result.value();

    constexpr RepositoryKey repository = 42;
    ASSERT_TRUE(watcher.add({repository, cleanup.path(), true}));
    ASSERT_TRUE(watcher.wake());
    ASSERT_TRUE(watcher.remove(repository));

    // Reusing the key proves remove() consumed the old completion before it
    // released the Watch and its embedded OVERLAPPED.
    ASSERT_TRUE(watcher.add({repository, cleanup.path(), true}));
    watcher.stop();
    watcher.stop();
    EXPECT_FALSE(watcher.wake());
}

TEST(IocpDirectoryWatcher, RejectsKeysReservedForWakeAndStopPackets) {
    auto watcher_result = make_iocp_directory_watcher();
    ASSERT_TRUE(watcher_result) << watcher_result.error().message();
    auto& watcher = *watcher_result.value();

    const auto reserved = (std::numeric_limits<RepositoryKey>::max)();
    EXPECT_FALSE(watcher.add({reserved, L"unused", true}));
    EXPECT_FALSE(watcher.add({reserved - 1, L"unused", true}));
}

#endif

} // namespace
} // namespace gsave::platform
