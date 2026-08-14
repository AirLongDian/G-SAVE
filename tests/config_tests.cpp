#include "gsave/config/config.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace gsave::config {
namespace {

using namespace std::chrono_literals;

constexpr std::string_view valid_config = R"toml(
[[games]]
id = "game-id"
enabled = true
process_name = "game.exe"
process_path = 'C:\Games\GameName\game.exe'
parser = "packages/game-id/adapter.lua"

[[games.saves]]
path = 'C:\Users\Player\Saves\..\Saves'
include_globs = ["*/DS30000.sl2"]
exclude_globs = [".git/**", "cache/**"]

[games.commit]
strategy = "hybrid"
quiet_seconds = 5
max_interval_seconds = 300
commit_on_exit = true

[games.sync]
backend = "git"
trigger = "on_exit"
)toml";

[[nodiscard]] std::string replace_once(
    std::string input,
    const std::string_view from,
    const std::string_view to) {
    const auto offset = input.find(from);
    EXPECT_NE(offset, std::string::npos);
    if (offset != std::string::npos) {
        input.replace(offset, from.size(), to);
    }
    return input;
}

[[nodiscard]] std::string append_game_with_save_path(
    std::string input,
    const std::string_view save_path) {
    input += R"toml(
[[games]]
id = "other-game"
enabled = true
process_name = "other.exe"
process_path = 'D:\Games\Other\other.exe'
parser = "packages/other/adapter.lua"
[[games.saves]]
path = ')toml";
    input += save_path;
    input += R"toml('
[games.commit]
strategy = "on_exit"
[games.sync]
backend = "onedrive"
trigger = "manual"
)toml";
    return input;
}

TEST(Config, ParsesAndNormalizesTheDocumentedShape) {
    const auto result = parse_config(valid_config);

    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result->games.size(), 1U);
    const auto& game = result->games.front();
    EXPECT_EQ(game.id, "game-id");
    EXPECT_TRUE(game.enabled);
    EXPECT_EQ(game.process_name, "game.exe");
    EXPECT_EQ(game.process_path.filename(), "game.exe");
    EXPECT_EQ(game.parser.generic_string(), "packages/game-id/adapter.lua");
    ASSERT_EQ(game.saves.size(), 1U);
    EXPECT_EQ(game.saves.front().path.filename(), "Saves");
    EXPECT_EQ(game.saves.front().include_globs,
              std::vector<std::string>{"*/DS30000.sl2"});
    EXPECT_EQ(game.saves.front().exclude_globs,
              (std::vector<std::string>{".git/**", "cache/**"}));
    EXPECT_EQ(game.commit.strategy, core::CommitStrategy::hybrid);
    EXPECT_EQ(game.commit.quiet_interval, 5s);
    EXPECT_EQ(game.commit.max_interval, 300s);
    EXPECT_TRUE(game.commit.commit_on_exit);
    EXPECT_EQ(game.sync.backend, SyncBackend::git);
    EXPECT_EQ(game.sync.trigger, SyncTrigger::on_exit);
}

TEST(Config, RejectsAMissingRequiredField) {
    const auto source = replace_once(std::string(valid_config), "enabled = true\n", "");
    const auto result = parse_config(source);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().context.find("enabled"), std::string::npos);
}

TEST(Config, RejectsDuplicateGameIds) {
    auto source = std::string(valid_config);
    source += R"toml(
[[games]]
id = "game-id"
enabled = false
process_name = "other.exe"
process_path = 'D:\Games\Other\other.exe'
parser = "packages/other/adapter.lua"
[[games.saves]]
path = 'D:\Saves\Other'
[games.commit]
strategy = "on_exit"
commit_on_exit = true
[games.sync]
backend = "webdav"
trigger = "manual"
)toml";

    const auto result = parse_config(source);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().context.find("duplicates another game id"), std::string::npos);
}

TEST(Config, RejectsDuplicateRepositoryPathsAcrossGames) {
    for (const auto duplicate : {
             std::string_view{R"(C:\Users\Player\Saves)"},
             std::string_view{R"(c:/users\PLAYER/Saves/.)"},
             std::string_view{R"(C:\Users\Player\Other\..\Saves)"},
         }) {
        const auto result = parse_config(append_game_with_save_path(
            std::string(valid_config), duplicate));
        ASSERT_FALSE(result) << duplicate;
        EXPECT_NE(result.error().context.find("duplicates another repository"), std::string::npos);
    }
}

TEST(Config, PreservesDriveAndUncRootsDuringLexicalNormalization) {
    const auto drive_source = replace_once(
        std::string(valid_config),
        R"(C:\Users\Player\Saves\..\Saves)",
        R"(C:\.)");
    const auto drive = parse_config(drive_source);
    ASSERT_TRUE(drive) << drive.error().message();
    EXPECT_EQ(drive->games.front().saves.front().path.string(), R"(C:\)");

    const auto unc_source = replace_once(
        std::string(valid_config),
        R"(C:\Users\Player\Saves\..\Saves)",
        R"(\\Server\Share\Folder\..\.)");
    const auto unc = parse_config(unc_source);
    ASSERT_TRUE(unc) << unc.error().message();
    EXPECT_EQ(unc->games.front().saves.front().path.string(), R"(\\Server\Share)");
}

TEST(Config, RejectsUnsupportedCommitStrategy) {
    const auto source = replace_once(std::string(valid_config), "strategy = \"hybrid\"", "strategy = \"always\"");
    const auto result = parse_config(source);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().context.find("strategy"), std::string::npos);
}

TEST(Config, RejectsZeroAndNegativeCommitIntervals) {
    for (const auto invalid_value : {0, -1}) {
        const auto source = replace_once(
            std::string(valid_config),
            "quiet_seconds = 5",
            "quiet_seconds = " + std::to_string(invalid_value));
        const auto result = parse_config(source);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().context.find("quiet_seconds"), std::string::npos);
    }
}

TEST(Config, RejectsIncompletePeriodicSync) {
    const auto source = replace_once(std::string(valid_config), "trigger = \"on_exit\"", "trigger = \"periodic\"");
    const auto result = parse_config(source);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().context.find("interval_seconds"), std::string::npos);
}

TEST(Config, RejectsRelativeRepositoryAndProcessPaths) {
    auto relative_process = replace_once(
        std::string(valid_config),
        "process_path = 'C:\\Games\\GameName\\game.exe'",
        "process_path = 'GameName/game.exe'");
    EXPECT_FALSE(parse_config(relative_process));

    auto relative_save = replace_once(
        std::string(valid_config),
        "path = 'C:\\Users\\Player\\Saves\\..\\Saves'",
        "path = 'Saves'");
    EXPECT_FALSE(parse_config(relative_save));
}

TEST(Config, MatchesSupportPackagePathGlobsWithoutCrossingSegmentsForSingleStar) {
    EXPECT_TRUE(core::path_glob_matches(
        "*/DS30000.sl2", "0110000100000001/DS30000.sl2"));
    EXPECT_TRUE(core::path_glob_matches("cache/**", "cache/a/b.tmp"));
    EXPECT_TRUE(core::path_glob_matches("*.SAV", "slot.sav"));
    EXPECT_FALSE(core::path_glob_matches("*.sav", "account/slot.sav"));
    EXPECT_FALSE(core::path_glob_matches(
        "*/DS30000.sl2", "account/nested/DS30000.sl2"));
}

TEST(Config, SerializesAndAtomicallyReplacesAValidatedConfiguration) {
    auto parsed = parse_config(valid_config);
    ASSERT_TRUE(parsed) << parsed.error().message();
    parsed->games.front().sync.trigger = SyncTrigger::periodic;
    parsed->games.front().sync.interval = std::chrono::seconds{47};
    parsed->games.front().sync.remote = "cloud";
    parsed->games.front().sync.credential_reference = "G-SAVE/git/game-id";

    auto serialized = serialize_config(*parsed);
    ASSERT_TRUE(serialized) << serialized.error().message();
    auto round_trip = parse_config(*serialized);
    ASSERT_TRUE(round_trip) << round_trip.error().message();
    ASSERT_EQ(round_trip->games.size(), 1U);
    EXPECT_EQ(round_trip->games.front().sync.interval, std::chrono::seconds{47});
    EXPECT_EQ(round_trip->games.front().sync.remote, "cloud");
    EXPECT_EQ(round_trip->games.front().sync.credential_reference,
              "G-SAVE/git/game-id");
    EXPECT_EQ(round_trip->games.front().saves.front().include_globs,
              std::vector<std::string>{"*/DS30000.sl2"});

    const auto root = std::filesystem::temp_directory_path()
        / "gsave-config-atomic-test";
    const auto path = root / "config.toml";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    auto saved = save_config_atomic(path, *parsed);
    ASSERT_TRUE(saved) << saved.error().message();
    auto loaded = load_config(path);
    ASSERT_TRUE(loaded) << loaded.error().message();
    EXPECT_EQ(loaded->games.front().sync.remote, "cloud");
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        EXPECT_EQ(entry.path().filename(), "config.toml");
    }
    std::filesystem::remove_all(root, ignored);
}

}  // namespace
}  // namespace gsave::config
