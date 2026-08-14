#include "gsave/core/app.hpp"
#include "gsave/repository/repository_engine.hpp"

#include <git2.h>
#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gsave::core {
namespace {

using namespace std::chrono_literals;

class ManualProcessEvents final : public platform::ProcessEventSource {
public:
    Status start(platform::ProcessEventSink sink) override {
        sink_ = std::move(sink);
        return {};
    }

    void stop() noexcept override { sink_ = {}; }

    void emit(const platform::ProcessEvent& event) const { sink_(event); }

private:
    platform::ProcessEventSink sink_;
};

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path sample_path() {
    const DWORD size = GetEnvironmentVariableW(L"GSAVE_DS3_SAMPLE", nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        L"GSAVE_DS3_SAMPLE", value.data(), size);
    if (written == 0 || written >= size) return {};
    value.resize(written);
    return value;
}

[[nodiscard]] std::filesystem::path environment_path(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
    if (written == 0 || written >= size) return {};
    value.resize(written);
    return value;
}

class RealDs3Layout final {
public:
    explicit RealDs3Layout(const std::filesystem::path& sample) {
        static std::atomic_uint64_t sequence{};
        root = std::filesystem::temp_directory_path()
            / (L"gsave-real-ds3-core-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(sequence.fetch_add(1)));
        repository = root / L"DarkSoulsIII";
        save = repository / L"0110000139dd872d" / L"DS30000.sl2";
        remote = root / L"remote.git";
        executable = root / L"DarkSoulsIII.exe";
        parser = std::filesystem::path{GSAVE_SOURCE_DIR}
            / L"packages" / L"dark-souls-iii" / L"adapter.lua";
        std::filesystem::create_directories(save.parent_path());
        std::filesystem::copy_file(sample, save);
        std::ofstream(executable, std::ios::binary) << "test executable";
    }

    ~RealDs3Layout() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    void change_save(const std::uintmax_t distance_from_end) const {
        std::fstream file(save, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file);
        file.seekg(-static_cast<std::streamoff>(distance_from_end), std::ios::end);
        char byte{};
        file.read(&byte, 1);
        ASSERT_TRUE(file);
        byte ^= static_cast<char>(0x5a);
        file.seekp(-1, std::ios::cur);
        file.write(&byte, 1);
        file.flush();
        ASSERT_TRUE(file);
    }

    std::filesystem::path root;
    std::filesystem::path repository;
    std::filesystem::path save;
    std::filesystem::path remote;
    std::filesystem::path executable;
    std::filesystem::path parser;
};

void add_local_remote(const RealDs3Layout& layout) {
    ASSERT_GE(git_libgit2_init(), 1);
    git_repository* bare = nullptr;
    ASSERT_EQ(git_repository_init(&bare, path_utf8(layout.remote).c_str(), 1), 0);
    git_repository_free(bare);
    git_repository* source = nullptr;
    ASSERT_EQ(git_repository_open(
        &source, path_utf8(layout.repository).c_str()), 0);
    git_remote* remote = nullptr;
    ASSERT_EQ(git_remote_create(
        &remote, source, "origin", path_utf8(layout.remote).c_str()), 0);
    git_remote_free(remote);
    git_repository_free(source);
    git_libgit2_shutdown();
}

[[nodiscard]] std::size_t commit_count(
    const std::filesystem::path& repository_path) {
    EXPECT_GE(git_libgit2_init(), 1);
    git_repository* repository = nullptr;
    EXPECT_EQ(git_repository_open(
        &repository, path_utf8(repository_path).c_str()), 0);
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

[[nodiscard]] std::string head_message(
    const std::filesystem::path& repository_path) {
    git_libgit2_init();
    git_repository* repository = nullptr;
    EXPECT_EQ(git_repository_open(
        &repository, path_utf8(repository_path).c_str()), 0);
    git_oid head{};
    EXPECT_EQ(git_reference_name_to_id(&head, repository, "HEAD"), 0);
    git_commit* commit = nullptr;
    EXPECT_EQ(git_commit_lookup(&commit, repository, &head), 0);
    const std::string message = commit == nullptr ? "" : git_commit_message(commit);
    git_commit_free(commit);
    git_repository_free(repository);
    git_libgit2_shutdown();
    return message;
}

TEST(CoreRealIntegration, Ds3ChangesTriggerThreeCommitsAndThreePushes) {
    const auto sample = sample_path();
    if (sample.empty() || !std::filesystem::is_regular_file(sample)) {
        GTEST_SKIP() << "set GSAVE_DS3_SAMPLE to a copied DS30000.sl2";
    }

    RealDs3Layout layout(sample);
    auto initialized = repository::initialize_repository({
        .repository = layout.repository,
        .game_id = "dark-souls-iii",
        .parser = layout.parser,
    });
    ASSERT_TRUE(initialized) << initialized.error().message();
    ASSERT_EQ(*initialized, repository::CommitOutcome::created);
    add_local_remote(layout);
    auto baseline_push = repository::push_repository({layout.repository, "origin", {}});
    ASSERT_TRUE(baseline_push) << baseline_push.error().message();

    config::GameConfig game{
        .id = "dark-souls-iii",
        .enabled = true,
        .process_name = "DarkSoulsIII.exe",
        .process_path = layout.executable,
        .parser = layout.parser,
        .saves = {{layout.repository}},
        .commit = CommitPolicy{
            .strategy = CommitStrategy::quiet,
            .quiet_interval = 1s,
            .commit_on_exit = false,
        },
        .sync = config::SyncPolicy{
            .backend = config::SyncBackend::git,
            .trigger = config::SyncTrigger::on_commit,
            .remote = "origin",
            .credential_reference = "G-SAVE/test-local-remote",
        },
    };
    auto process_owner = std::make_unique<ManualProcessEvents>();
    auto* process = process_owner.get();
    auto watcher = platform::make_iocp_directory_watcher();
    ASSERT_TRUE(watcher) << watcher.error().message();
    CoreApp app(
        CoreAppOptions{
            .config = config::Config{{std::move(game)}},
            .config_directory = layout.root,
        },
        std::move(process_owner), std::move(*watcher));
    auto started = app.start();
    ASSERT_TRUE(started) << started.error().message();
    process->emit({
        .kind = platform::ProcessEventKind::started,
        .process_id = 73001,
        .process_name = L"DarkSoulsIII.exe",
        .image_path = layout.executable,
    });
    ASSERT_TRUE(app.run_iteration(0ms));

    for (std::size_t round = 1; round <= 3; ++round) {
        layout.change_save(round);
        const auto expected = round + 1;
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        while (commit_count(layout.remote) < expected
               && std::chrono::steady_clock::now() < deadline) {
            const auto iteration = app.run_iteration(100ms);
            ASSERT_TRUE(iteration) << iteration.error().message();
        }
        ASSERT_EQ(commit_count(layout.repository), expected);
        ASSERT_EQ(commit_count(layout.remote), expected);
    }

    const auto message = head_message(layout.repository);
    EXPECT_NE(message.find("\"character_name\":\"AirKL\""), std::string::npos);
    EXPECT_NE(message.find("\"character_name\":\"real-msj\""), std::string::npos);
    EXPECT_NE(message.find("\"10\":"), std::string::npos);

    process->emit({
        .kind = platform::ProcessEventKind::stopped,
        .process_id = 73001,
    });
    ASSERT_TRUE(app.run_iteration(0ms));
    app.shutdown();
}

struct LocalPackageCase final {
    const char* test_name;
    const wchar_t* environment;
    const wchar_t* package;
    const wchar_t* process_name;
    const char* process_name_utf8;
    const wchar_t* save_name;
    const char* game_id;
    std::vector<std::string> include_globs;
    std::vector<std::string> exclude_globs;
};

class RealPackageLayout final {
public:
    RealPackageLayout(
        const std::filesystem::path& sample_directory,
        const LocalPackageCase& test_case) {
        static std::atomic_uint64_t sequence{};
        root = std::filesystem::temp_directory_path()
            / (L"gsave-real-package-core-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(sequence.fetch_add(1)));
        repository = root / L"save";
        executable = root / test_case.process_name;
        parser = std::filesystem::path{GSAVE_SOURCE_DIR}
            / L"packages" / test_case.package / L"adapter.lua";
        std::filesystem::create_directories(root);
        std::filesystem::copy(
            sample_directory,
            repository,
            std::filesystem::copy_options::recursive
                | std::filesystem::copy_options::overwrite_existing);
        std::error_code ignored;
        std::filesystem::remove_all(repository / L".git", ignored);
        std::ofstream(executable, std::ios::binary) << "test executable";

        for (std::filesystem::recursive_directory_iterator iterator{repository}, end;
             iterator != end; ++iterator) {
            if (iterator->is_regular_file()
                && iterator->path().filename() == test_case.save_name) {
                save = iterator->path();
                break;
            }
        }
    }

    ~RealPackageLayout() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    void change_save(const std::uintmax_t distance_from_end) const {
        std::fstream file(save, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file);
        file.seekg(-static_cast<std::streamoff>(distance_from_end), std::ios::end);
        char byte{};
        file.read(&byte, 1);
        ASSERT_TRUE(file);
        byte ^= static_cast<char>(0x5a);
        file.seekp(-1, std::ios::cur);
        file.write(&byte, 1);
        file.flush();
        ASSERT_TRUE(file);
    }

    std::filesystem::path root;
    std::filesystem::path repository;
    std::filesystem::path save;
    std::filesystem::path executable;
    std::filesystem::path parser;
};

class RealPackageCoreTest : public testing::TestWithParam<LocalPackageCase> {};

TEST_P(RealPackageCoreTest, TwoRealSaveChangesCreateTwoLocalCommits) {
    const auto& test_case = GetParam();
    const auto sample_directory = environment_path(test_case.environment);
    if (!std::filesystem::is_directory(sample_directory)) {
        GTEST_SKIP() << "real package sample directory is not configured";
    }

    RealPackageLayout layout(sample_directory, test_case);
    ASSERT_TRUE(std::filesystem::is_regular_file(layout.save));
    auto initialized = repository::initialize_repository({
        .repository = layout.repository,
        .game_id = test_case.game_id,
        .parser = layout.parser,
    });
    ASSERT_TRUE(initialized) << initialized.error().message();
    ASSERT_EQ(*initialized, repository::CommitOutcome::created);

    config::GameConfig game{
        .id = test_case.game_id,
        .enabled = true,
        .process_name = test_case.process_name_utf8,
        .process_path = layout.executable,
        .parser = layout.parser,
        .saves = {{
            .path = layout.repository,
            .include_globs = test_case.include_globs,
            .exclude_globs = test_case.exclude_globs,
        }},
        .commit = CommitPolicy{
            .strategy = CommitStrategy::quiet,
            .quiet_interval = 1s,
            .commit_on_exit = false,
        },
        .sync = config::SyncPolicy{
            .backend = config::SyncBackend::git,
            .trigger = config::SyncTrigger::manual,
        },
    };
    auto process_owner = std::make_unique<ManualProcessEvents>();
    auto* process = process_owner.get();
    auto watcher = platform::make_iocp_directory_watcher();
    ASSERT_TRUE(watcher) << watcher.error().message();
    CoreApp app(
        CoreAppOptions{
            .config = config::Config{{std::move(game)}},
            .config_directory = layout.root,
        },
        std::move(process_owner), std::move(*watcher));
    auto started = app.start();
    ASSERT_TRUE(started) << started.error().message();
    process->emit({
        .kind = platform::ProcessEventKind::started,
        .process_id = 74001,
        .process_name = test_case.process_name,
        .image_path = layout.executable,
    });
    ASSERT_TRUE(app.run_iteration(0ms));

    for (std::size_t round = 1; round <= 2; ++round) {
        layout.change_save(round);
        const auto expected = round + 1;
        // This opt-in real-save test is also a regression guard against
        // accidentally re-enabling Windows' severe background I/O throttling.
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        while (commit_count(layout.repository) < expected
               && std::chrono::steady_clock::now() < deadline) {
            const auto iteration = app.run_iteration(100ms);
            ASSERT_TRUE(iteration) << iteration.error().message();
        }
        ASSERT_EQ(commit_count(layout.repository), expected);
    }

    const auto message = head_message(layout.repository);
    EXPECT_NE(message.find(std::string{"\"game_id\":\""}
                           + test_case.game_id + "\""), std::string::npos);
    EXPECT_EQ(message.find("parser_error"), std::string::npos);
    process->emit({
        .kind = platform::ProcessEventKind::stopped,
        .process_id = 74001,
    });
    ASSERT_TRUE(app.run_iteration(0ms));
    app.shutdown();
}

INSTANTIATE_TEST_SUITE_P(
    RealSavePackages,
    RealPackageCoreTest,
    testing::Values(
        LocalPackageCase{
            "DragonsDogmaDarkArisen",
            L"GSAVE_DDDA_SAMPLE_DIR",
            L"dragons-dogma-dark-arisen",
            L"DDDA.exe",
            "DDDA.exe",
            L"DDDA.sav",
            "dragons-dogma-dark-arisen",
            {"DDDA.sav", "0", "1", "2", "3", "4", "5", "6", "7"},
            {".git/**"},
        },
        LocalPackageCase{
            "DragonsDogma2",
            L"GSAVE_DD2_SAMPLE_DIR",
            L"dragons-dogma-2",
            L"DD2.exe",
            "DD2.exe",
            L"data000.bin",
            "dragons-dogma-2",
            {"*.bin"},
            {".git/**"},
        },
        LocalPackageCase{
            "EldenRing",
            L"GSAVE_ER_SAMPLE_DIR",
            L"elden-ring",
            L"eldenring.exe",
            "eldenring.exe",
            L"ER0000.co2",
            "elden-ring",
            {"*/ER0000.sl2", "*/ER0000.co2"},
            {".git/**", "GraphicsConfig.xml", "**/*.bak"},
        }),
    [](const testing::TestParamInfo<LocalPackageCase>& info) {
        return info.param.test_name;
    });

}  // namespace
}  // namespace gsave::core
