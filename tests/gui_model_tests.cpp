#include "gsave/gui/gui_model.hpp"

#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace gsave::gui {
namespace {

class GuiLayout final {
public:
    GuiLayout() {
        static std::atomic_uint64_t sequence{};
        root = std::filesystem::temp_directory_path()
            / (L"gsave-gui-model-test-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(sequence.fetch_add(1)));
        save = root / L"save";
        save2 = root / L"save-2";
        executable = root / L"game.exe";
        config = root / L"config.toml";
        std::error_code stale;
        std::filesystem::remove_all(root, stale);
        std::filesystem::create_directories(save);
        std::filesystem::create_directories(save2);
        write(save / L"slot.sav", "save");
        write(save2 / L"slot.sav", "save-2");
        write(executable, "not-a-real-pe-needed-for-model-test");
    }

    ~GuiLayout() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    static void write(const std::filesystem::path& path, std::string_view text) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output << text;
    }

    std::filesystem::path root;
    std::filesystem::path save;
    std::filesystem::path save2;
    std::filesystem::path executable;
    std::filesystem::path config;
};

TEST(GuiModel, DiscoversPackagesAndGenericInstallPersistsWithoutDeletingHistory) {
    GuiLayout layout;
    const auto package_root = std::filesystem::path{GSAVE_SOURCE_DIR} / "packages";
    GuiModel model(layout.config, package_root);
    auto loaded = model.reload();
    ASSERT_TRUE(loaded) << loaded.error().message();
    ASSERT_EQ(model.packages().size(), 1U);
    EXPECT_TRUE(model.packages().front().generic);
    const auto generic = model.packages().front();

    auto installed = model.install({
        .package = generic,
        .process_path = layout.executable,
        .repositories = {
            {.path = layout.save, .include_globs = {"*.sav"}},
            {.path = layout.save2, .include_globs = {"*.sav"}},
        },
    });
    ASSERT_TRUE(installed) << installed.error().message();
    ASSERT_EQ(model.configuration().games.size(), 1U);
    ASSERT_EQ(model.configuration().games.front().saves.size(), 2U);
    EXPECT_EQ(model.configuration().games.front().saves.front().include_globs,
              std::vector<std::string>{"*.sav"});
    EXPECT_TRUE(model.configuration().games.front().enabled);
    EXPECT_EQ(model.configuration().games.front().sync.trigger,
              config::SyncTrigger::on_exit);
    EXPECT_TRUE(std::filesystem::is_directory(layout.save / L".git"));
    EXPECT_TRUE(std::filesystem::is_directory(layout.save2 / L".git"));
    EXPECT_TRUE(std::filesystem::is_regular_file(layout.config));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        model.configuration().games.front().parser));
    EXPECT_EQ(
        model.configuration().games.front().parser.parent_path().parent_path(),
        layout.root / L"packages");

    ASSERT_TRUE(model.set_enabled(0, false));
    EXPECT_FALSE(model.configuration().games.front().enabled);
    ASSERT_TRUE(model.remove_game(0));
    EXPECT_TRUE(model.configuration().games.empty());
    EXPECT_TRUE(std::filesystem::is_directory(layout.save / L".git"));
    EXPECT_TRUE(std::filesystem::is_directory(layout.save2 / L".git"));
}

TEST(GuiModel, RunsPackageInstallInReadOnlySandboxAndReturnsEveryRepository) {
    GuiLayout layout;
    const auto package_root = layout.root / L"detector";
    const auto adapter = package_root / L"adapter.lua";
    std::filesystem::create_directories(package_root);
    const auto as_utf8 = [](const std::filesystem::path& path) {
        const auto text = path.generic_u8string();
        return std::string{reinterpret_cast<const char*>(text.data()), text.size()};
    };
    GuiLayout::write(adapter,
        "function install(context)\n"
        "  if os ~= nil or io ~= nil or package ~= nil or debug ~= nil then error('unsafe library') end\n"
        "  return { process_name = 'game.exe', process_path = '" + as_utf8(layout.executable)
        + "', repositories = {{path='" + as_utf8(layout.save)
        + "', include_globs={'*.sav'}}, {path='" + as_utf8(layout.save2)
        + "', exclude_globs={'cache/**'}}}, problems = {} }\n"
        "end\nfunction parse() return {} end\n");
    PackageManifest package{
        .root = package_root,
        .id = "detector",
        .name = "Detector",
        .version = "1",
        .process_name = "game.exe",
        .adapter = adapter,
    };
    auto detected = detect_package_install(package);
    ASSERT_TRUE(detected) << detected.error().message();
    EXPECT_EQ(detected->process_path, layout.executable);
    ASSERT_EQ(detected->repositories.size(), 2U);
    EXPECT_EQ(detected->repositories[0].path, layout.save);
    EXPECT_EQ(detected->repositories[1].path, layout.save2);
    EXPECT_EQ(detected->repositories[0].include_globs,
              std::vector<std::string>{"*.sav"});
    EXPECT_EQ(detected->repositories[1].exclude_globs,
              std::vector<std::string>{"cache/**"});
    EXPECT_TRUE(detected->problems.empty());
}

TEST(GuiModel, RejectsAnAdapterThatEscapesItsPackageDirectory) {
    GuiLayout layout;
    const auto package = layout.root / L"package";
    std::filesystem::create_directories(package);
    GuiLayout::write(layout.root / L"outside.lua", "function parse() return {} end");
    GuiLayout::write(package / L"manifest.toml", R"toml(
package_api = 1
id = "bad"
name = "Bad"
version = "1"
adapter = "../outside.lua"
[game]
process_name = "game.exe"
)toml");
    auto manifest = load_package_manifest(package / L"manifest.toml");
    EXPECT_FALSE(manifest);
}

TEST(GuiModel, AppliesOneCloudPolicyAtomicallyToEveryConfiguredGame) {
    GuiLayout layout;
    const auto parser = layout.root / L"adapter.lua";
    GuiLayout::write(parser, "function parse() return {} end");
    const core::CommitPolicy commit{
        .strategy = core::CommitStrategy::hybrid,
        .quiet_interval = std::chrono::seconds{5},
        .max_interval = std::chrono::seconds{300},
        .commit_on_exit = true,
    };
    config::Config initial{{
        config::GameConfig{
            .id = "game-one", .enabled = true,
            .process_name = "game.exe", .process_path = layout.executable,
            .parser = parser, .saves = {{.path = layout.save}},
            .commit = commit,
        },
        config::GameConfig{
            .id = "game-two", .enabled = false,
            .process_name = "game.exe", .process_path = layout.executable,
            .parser = parser, .saves = {{.path = layout.save2}},
            .commit = commit,
        },
    }};
    ASSERT_TRUE(config::save_config_atomic(layout.config, initial));
    GuiModel model{layout.config, layout.root / L"packages"};
    ASSERT_TRUE(model.reload());
    const config::SyncPolicy cloud{
        .backend = config::SyncBackend::git,
        .trigger = config::SyncTrigger::on_exit,
        .remote = "origin",
        .credential_reference = "G-SAVE/git-service/example/player",
    };
    ASSERT_TRUE(model.update_all_sync_policies(cloud));
    ASSERT_EQ(model.configuration().games.size(), 2U);
    EXPECT_EQ(model.configuration().games[0].sync, cloud);
    EXPECT_EQ(model.configuration().games[1].sync, cloud);
    EXPECT_FALSE(model.configuration().games[1].enabled);

    auto reloaded = config::load_config(layout.config);
    ASSERT_TRUE(reloaded) << reloaded.error().message();
    ASSERT_EQ(reloaded->games.size(), 2U);
    EXPECT_EQ(reloaded->games[0].sync, cloud);
    EXPECT_EQ(reloaded->games[1].sync, cloud);
}

TEST(GuiModel, ImportPersistsPackageWithoutConfiguringGame) {
    GuiLayout layout;
    const auto source = layout.root / L"import-src";
    std::filesystem::create_directories(source);
    GuiLayout::write(source / L"adapter.lua",
        "function install() return {} end\nfunction parse() return {} end\n");
    GuiLayout::write(source / L"manifest.toml", R"toml(
package_api = 1
id = "imported-game"
name = "Imported Game"
version = "1"
adapter = "adapter.lua"
[game]
process_name = "game.exe"
)toml");
    auto manifest = load_package_manifest(source / L"manifest.toml");
    ASSERT_TRUE(manifest) << manifest.error().message();

    GuiModel model(layout.config, layout.root / L"packages");
    ASSERT_TRUE(model.reload());
    ASSERT_TRUE(model.configuration().games.empty());

    auto imported = model.import_package(*manifest);
    ASSERT_TRUE(imported) << imported.error().message();
    EXPECT_TRUE(model.configuration().games.empty());

    const auto stable = layout.root / L"packages" / L"imported-game";
    EXPECT_TRUE(std::filesystem::is_regular_file(stable / L"manifest.toml"));
    EXPECT_TRUE(std::filesystem::is_regular_file(stable / L"adapter.lua"));
    const auto found = std::ranges::find_if(
        model.packages(), [](const auto& package) { return package.id == "imported-game"; });
    ASSERT_NE(found, model.packages().end());
    EXPECT_EQ(found->version, "1");

    GuiLayout::write(source / L"manifest.toml", R"toml(
package_api = 1
id = "imported-game"
name = "Imported Game"
version = "2"
adapter = "adapter.lua"
[game]
process_name = "game.exe"
)toml");
    auto updated = load_package_manifest(source / L"manifest.toml");
    ASSERT_TRUE(updated) << updated.error().message();
    auto updated_status = model.import_package(*updated);
    ASSERT_TRUE(updated_status) << updated_status.error().message();
    const auto refreshed = std::ranges::find_if(
        model.packages(), [](const auto& package) { return package.id == "imported-game"; });
    ASSERT_NE(refreshed, model.packages().end());
    EXPECT_EQ(refreshed->version, "2");

    ASSERT_TRUE(model.install({
        .package = *refreshed,
        .process_path = layout.executable,
        .repositories = {{.path = layout.save, .include_globs = {"*.sav"}}},
    }));
    std::filesystem::create_directories(source / L"scripts");
    GuiLayout::write(source / L"scripts" / L"parse.lua",
        "function install() return {} end\nfunction parse() return {} end\n");
    GuiLayout::write(source / L"manifest.toml", R"toml(
package_api = 1
id = "imported-game"
name = "Imported Game"
version = "3"
adapter = "scripts/parse.lua"
[game]
process_name = "game.exe"
)toml");
    auto incompatible = load_package_manifest(source / L"manifest.toml");
    ASSERT_TRUE(incompatible) << incompatible.error().message();
    auto rejected = model.import_package(*incompatible);
    EXPECT_FALSE(rejected);
    auto unchanged = load_package_manifest(stable / L"manifest.toml");
    ASSERT_TRUE(unchanged) << unchanged.error().message();
    EXPECT_EQ(unchanged->version, "2");
    EXPECT_EQ(unchanged->adapter.filename(), L"adapter.lua");
}

TEST(GuiModel, InstallUpdatesExistingGamePreservingEnabledAndSync) {
    GuiLayout layout;
    const auto package_dir = layout.root / L"package";
    const auto parser = package_dir / L"adapter.lua";
    std::filesystem::create_directories(package_dir);
    GuiLayout::write(parser,
        "function install() return {} end\nfunction parse() return {} end\n");
    GuiLayout::write(package_dir / L"manifest.toml", R"toml(
package_api = 1
id = "test-game"
name = "Test Game"
version = "1"
adapter = "adapter.lua"
[game]
process_name = "game.exe"
[git]
exclude_globs = ["new-cache/**"]
)toml");
    const core::CommitPolicy commit{
        .strategy = core::CommitStrategy::hybrid,
        .quiet_interval = std::chrono::seconds{5},
        .max_interval = std::chrono::seconds{300},
        .commit_on_exit = true,
    };
    const config::SyncPolicy cloud{
        .backend = config::SyncBackend::git,
        .trigger = config::SyncTrigger::on_exit,
        .remote = "origin",
        .credential_reference = "G-SAVE/test",
    };
    config::Config initial{{config::GameConfig{
        .id = "test-game", .enabled = false,
        .process_name = "game.exe", .process_path = layout.executable,
        .parser = parser, .saves = {{.path = layout.save}},
        .commit = commit, .sync = cloud,
    }}};
    ASSERT_TRUE(config::save_config_atomic(layout.config, initial));
    PackageManifest package{
        .root = package_dir,
        .id = "test-game",
        .name = "Test Game",
        .version = "1",
        .process_name = "game.exe",
        .adapter = parser,
        .exclude_patterns = {"new-cache/**"},
        .commit = commit,
    };
    auto previous = repository::initialize_repository({
        .repository = layout.save2,
        .game_id = "generic",
        .parser = parser,
    });
    ASSERT_TRUE(previous) << previous.error().message();
    GuiModel model{layout.config, layout.root / L"packages"};
    ASSERT_TRUE(model.reload());
    auto installed = model.install({
        .package = package,
        .process_path = layout.executable,
        .repositories = {{.path = layout.save2, .include_globs = {"*.sav"}}},
    });
    ASSERT_TRUE(installed) << installed.error().message();
    ASSERT_EQ(model.configuration().games.size(), 1U);
    const auto& game = model.configuration().games.front();
    EXPECT_FALSE(game.enabled);
    EXPECT_EQ(game.saves.size(), 1U);
    EXPECT_EQ(game.saves.front().path, layout.save2);
    EXPECT_EQ(game.sync, cloud);
    EXPECT_TRUE(std::filesystem::is_directory(layout.save2 / L".git"));
    std::ifstream excludes{layout.save2 / L".git" / L"info" / L"exclude"};
    const std::string exclude_text{
        std::istreambuf_iterator<char>{excludes}, std::istreambuf_iterator<char>{}};
    EXPECT_NE(exclude_text.find("new-cache/**"), std::string::npos);
}

}  // namespace
}  // namespace gsave::gui
