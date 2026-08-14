#include "gsave/gui/gui_controller.hpp"

#include "gsave/repository/repository_engine.hpp"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>
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

class ControllerLayout final {
public:
    ControllerLayout() {
        static std::atomic_uint64_t sequence{};
        root = std::filesystem::temp_directory_path()
            / (L"gsave-gui-controller-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(sequence.fetch_add(1)));
        save = root / L"save";
        parser = root / L"adapter.lua";
        executable = root / L"game.exe";
        core = root / L"gsave-core.exe";
        config = root / L"config.toml";
        packages = root / L"packages";
        std::error_code stale;
        std::filesystem::remove_all(root, stale);
        std::filesystem::create_directories(save);
        std::filesystem::create_directories(packages);
        std::ofstream(save / L"slot.sav", std::ios::binary) << "slot";
        std::ofstream(executable, std::ios::binary) << "exe";
        std::ofstream(core, std::ios::binary) << "core";
        std::ofstream(parser) << R"lua(
function parse(repository, changed_files)
  return { accounts = { { account_id = "123", slots = {
    { index = 1, occupied = true, character_name = "Knight" },
    { index = 2, occupied = false, character_name = "" }
  } } } }
end
)lua";
        auto initialized = repository::initialize_repository({
            .repository = save,
            .game_id = "test-game",
            .parser = parser,
        });
        EXPECT_TRUE(initialized) << initialized.error().message();
        config::Config data{{config::GameConfig{
            .id = "test-game",
            .enabled = true,
            .process_name = "game.exe",
            .process_path = executable,
            .parser = parser,
            .saves = {{save}},
            .commit = core::CommitPolicy{
                .strategy = core::CommitStrategy::hybrid,
                .quiet_interval = std::chrono::seconds{5},
                .max_interval = std::chrono::seconds{300},
                .commit_on_exit = true,
            },
            .sync = config::SyncPolicy{
                .backend = config::SyncBackend::git,
                .trigger = config::SyncTrigger::manual,
                .remote = "origin",
            },
        }}};
        EXPECT_TRUE(config::save_config_atomic(config, data));
    }

    ~ControllerLayout() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    std::filesystem::path root;
    std::filesystem::path save;
    std::filesystem::path parser;
    std::filesystem::path executable;
    std::filesystem::path core;
    std::filesystem::path config;
    std::filesystem::path packages;
};

TEST(GuiController, ScopesHistoryByGameAndExposesParsedSlots) {
    ControllerLayout layout;
    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    ASSERT_EQ(controller.games().size(), 1);
    ASSERT_EQ(controller.repositoriesForGame(0).size(), 1);
    EXPECT_TRUE(controller.repositoriesForGame(1).isEmpty());

    controller.refreshHistory(0, 0);
    const auto history = controller.history();
    ASSERT_EQ(history.size(), 1);
    const auto commit = history.front().toMap();
    const auto slot_items = commit.value(QStringLiteral("slots")).toList();
    ASSERT_EQ(slot_items.size(), 2);
    EXPECT_EQ(slot_items.front().toMap().value(QStringLiteral("name")).toString(),
              QStringLiteral("Knight"));
    EXPECT_EQ(commit.value(QStringLiteral("slotSummary")).toString(),
              QStringLiteral("1 个角色 · Knight"));
}

TEST(GuiController, CloudFormIncludesEveryConfiguredGameWithoutASelector) {
    ControllerLayout layout;
    const auto second_save = layout.root / L"save-two";
    std::filesystem::create_directories(second_save);
    std::ofstream(second_save / L"slot.sav", std::ios::binary) << "slot-two";
    auto initialized = repository::initialize_repository({
        .repository = second_save,
        .game_id = "game-two",
        .parser = layout.parser,
    });
    ASSERT_TRUE(initialized) << initialized.error().message();
    auto configured = config::load_config(layout.config);
    ASSERT_TRUE(configured) << configured.error().message();
    auto second = configured->games.front();
    second.id = "game-two";
    second.enabled = false;
    second.saves = {{.path = second_save}};
    configured->games.push_back(std::move(second));
    ASSERT_TRUE(config::save_config_atomic(layout.config, *configured));

    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    const auto form = controller.cloudSettings();
    EXPECT_EQ(form.value(QStringLiteral("trigger")).toInt(), 3);
    EXPECT_FALSE(form.contains(QStringLiteral("remoteName")));
    EXPECT_FALSE(form.contains(QStringLiteral("remoteUrl")));
    EXPECT_FALSE(form.contains(QStringLiteral("credentialRef")));
    EXPECT_FALSE(form.contains(QStringLiteral("commitStrategy")));
    EXPECT_EQ(form.value(QStringLiteral("gameCount")).toInt(), 2);
    const auto repositories = form.value(QStringLiteral("repositories")).toList();
    ASSERT_EQ(repositories.size(), 2);
    EXPECT_EQ(repositories.front().toMap().value(QStringLiteral("game")).toString(),
              QStringLiteral("game"));
    EXPECT_EQ(repositories.front().toMap().value(QStringLiteral("repository")).toString(),
              QStringLiteral("gsave-test-game"));
    EXPECT_EQ(repositories.back().toMap().value(QStringLiteral("repository")).toString(),
              QStringLiteral("gsave-game-two"));
    EXPECT_FALSE(repositories.back().toMap().value(QStringLiteral("enabled")).toBool());
    EXPECT_TRUE(form.value(QStringLiteral("serviceAddress")).toString().isEmpty());
    EXPECT_FALSE(form.value(QStringLiteral("credentialStored")).toBool());
}

TEST(GuiController, ShowsEveryImportedPackageWithConfiguredState) {
    ControllerLayout layout;
    const auto installed = layout.packages / L"test-game";
    const auto available = layout.packages / L"another-game";
    std::filesystem::create_directories(installed);
    std::filesystem::create_directories(available);
    std::ofstream(installed / L"adapter.lua") << "function parse() return {} end";
    std::ofstream(available / L"adapter.lua") << "function parse() return {} end";
    std::ofstream(installed / L"manifest.toml")
        << "package_api=1\nid='test-game'\nname='Installed'\nversion='1'\n"
           "adapter='adapter.lua'\n[game]\nprocess_name='game.exe'\n";
    std::ofstream(available / L"manifest.toml")
        << "package_api=1\nid='another-game'\nname='Available'\nversion='1'\n"
           "adapter='adapter.lua'\n[game]\nprocess_name='another.exe'\n";

    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    const auto packages = controller.packages();
    ASSERT_EQ(packages.size(), 2);
    const auto configured_item = std::ranges::find_if(
        packages, [](const QVariant& item) {
            return item.toMap().value(QStringLiteral("name")).toString()
                == QStringLiteral("Installed");
        });
    ASSERT_NE(configured_item, packages.end());
    EXPECT_TRUE(configured_item->toMap().value(QStringLiteral("configured")).toBool());
    const auto available_item = std::ranges::find_if(
        packages, [](const QVariant& item) {
            return item.toMap().value(QStringLiteral("name")).toString()
                == QStringLiteral("Available");
        });
    ASSERT_NE(available_item, packages.end());
    EXPECT_FALSE(available_item->toMap().value(QStringLiteral("configured")).toBool());
}

TEST(GuiController, RejectsCloudSetupWithoutAServiceAndToken) {
    ControllerLayout layout;
    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    QString message;
    bool error = false;
    QObject::connect(
        &controller, &GuiController::message,
        [&](const QString& value, const bool failed) {
            message = value;
            error = failed;
        });

    controller.saveCloudSettings(QVariantMap{
        {QStringLiteral("serviceAddress"), QString{}},
        {QStringLiteral("token"), QString{}},
        {QStringLiteral("trigger"), 3},
    });

    EXPECT_TRUE(error);
    EXPECT_TRUE(message.contains(QStringLiteral("服务地址")));
    auto loaded = config::load_config(layout.config);
    ASSERT_TRUE(loaded) << loaded.error().message();
    ASSERT_EQ(loaded->games.size(), 1U);
    EXPECT_EQ(loaded->games.front().commit.strategy, core::CommitStrategy::hybrid);
    EXPECT_EQ(loaded->games.front().sync.trigger, config::SyncTrigger::manual);
    EXPECT_EQ(loaded->games.front().sync.remote, "origin");
    EXPECT_FALSE(loaded->games.front().sync.credential_reference.has_value());
}

TEST(GuiController, LoginTestDoesNotChangeConfigurationOrRemote) {
    ControllerLayout layout;
    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    auto before = config::load_config(layout.config);
    ASSERT_TRUE(before);
    QString message;
    bool error = false;
    QObject::connect(
        &controller, &GuiController::message,
        [&](const QString& value, const bool failed) {
            message = value;
            error = failed;
        });

    controller.testCloudConnection(QVariantMap{
        {QStringLiteral("serviceAddress"), QString{}},
        {QStringLiteral("token"), QString{}},
    });

    EXPECT_TRUE(error);
    EXPECT_TRUE(message.contains(QStringLiteral("服务地址")));
    auto after = config::load_config(layout.config);
    ASSERT_TRUE(after);
    ASSERT_EQ(after->games.size(), before->games.size());
    EXPECT_EQ(after->games.front().sync, before->games.front().sync);
    auto remote = repository::inspect_repository(layout.save, "origin");
    ASSERT_TRUE(remote);
    EXPECT_FALSE(remote->remote_url.has_value());
}

TEST(GuiController, ImportDistributedPackageShowsCardWhenNotConfigured) {
    const auto configured = qEnvironmentVariable("GSAVE_TEST_PACKAGE_ZIP");
    if (configured.isEmpty()) {
        GTEST_SKIP() << "GSAVE_TEST_PACKAGE_ZIP is not set";
    }
    ControllerLayout layout;
    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    const auto before = controller.packages().size();
    QString message;
    bool error = false;
    QObject::connect(
        &controller, &GuiController::message,
        [&](const QString& value, const bool failed) {
            message = value;
            error = failed;
        });

    controller.importPackageFile(configured);

    EXPECT_FALSE(error);
    EXPECT_TRUE(message.contains(QStringLiteral("已导入")));
    EXPECT_EQ(controller.packages().size(), before + 1);
}

TEST(GuiController, ImportPackageAlreadyConfiguredStillShowsCard) {
    ControllerLayout layout;
    const auto zip = layout.root / L"configured.zip";
    QZipWriter writer{QString::fromStdWString(zip.wstring())};
    writer.addFile(QStringLiteral("test-game/manifest.toml"), QByteArray{
        "package_api=1\nid='test-game'\nname='Installed Game'\nversion='2'\n"
        "adapter='adapter.lua'\n[game]\nprocess_name='game.exe'\n"});
    writer.addFile(QStringLiteral("test-game/adapter.lua"), QByteArray{
        "function install() return {} end\nfunction parse() return {} end\n"});
    writer.close();
    ASSERT_EQ(writer.status(), QZipWriter::NoError);

    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    const auto packages_before = controller.packages().size();
    const auto games_before = controller.games().size();
    QString message;
    bool error = false;
    QObject::connect(
        &controller, &GuiController::message,
        [&](const QString& value, const bool failed) {
            message = value;
            error = failed;
        });

    controller.importPackageFile(QString::fromStdWString(zip.wstring()));

    EXPECT_FALSE(error);
    EXPECT_TRUE(message.contains(QStringLiteral("已导入")));
    EXPECT_EQ(controller.packages().size(), packages_before + 1);
    EXPECT_EQ(controller.games().size(), games_before);
    EXPECT_TRUE(controller.packages().front().toMap()
        .value(QStringLiteral("configured")).toBool());
}

TEST(GuiController, ImportPackageOnlyImportsAndDoesNotConfigure) {
    ControllerLayout layout;
    const auto zip = layout.root / L"package.zip";
    QZipWriter writer{QString::fromStdWString(zip.wstring())};
    writer.addFile(QStringLiteral("imported-game/manifest.toml"), QByteArray{
        "package_api=1\nid='imported-game'\nname='Imported Game'\nversion='1'\n"
        "adapter='adapter.lua'\n[game]\nprocess_name='game.exe'\n"});
    writer.addFile(QStringLiteral("imported-game/adapter.lua"), QByteArray{
        "function install() return {} end\nfunction parse() return {} end\n"});
    writer.close();
    ASSERT_EQ(writer.status(), QZipWriter::NoError);

    GuiController controller{layout.core, layout.config, layout.packages};
    ASSERT_TRUE(controller.initialize());
    const auto games_before = controller.games().size();
    QString message;
    bool error = false;
    QObject::connect(
        &controller, &GuiController::message,
        [&](const QString& value, const bool failed) {
            message = value;
            error = failed;
        });

    controller.importPackageFile(QString::fromStdWString(zip.wstring()));

    EXPECT_FALSE(error);
    EXPECT_TRUE(message.contains(QStringLiteral("已导入")));
    EXPECT_EQ(controller.games().size(), games_before);
    ASSERT_EQ(controller.packages().size(), 1);
    EXPECT_EQ(controller.packages().front().toMap().value(QStringLiteral("name")).toString(),
              QStringLiteral("Imported Game"));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        layout.packages / L"imported-game" / L"adapter.lua"));
    auto loaded = config::load_config(layout.config);
    ASSERT_TRUE(loaded) << loaded.error().message();
    ASSERT_EQ(loaded->games.size(), games_before);
    EXPECT_EQ(loaded->games.front().id, "test-game");
}

}  // namespace
}  // namespace gsave::gui
