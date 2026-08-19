// Builds a throwaway configuration with real in-place Git history so the library,
// detail and settings pages can be checked against genuine repository state
// instead of hand-written placeholder data. Development aid only.
#include "gsave/config/config.hpp"
#include "gsave/repository/repository_engine.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

struct Sample final {
    std::string id;
    std::string process;
    std::string package;
};

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: gui_library_fixture ROOT\n";
        return 2;
    }
    const auto root = std::filesystem::path{argv[1]};
    const auto packages = std::filesystem::path{GSAVE_SOURCE_DIR} / L"packages";

    const Sample samples[] = {
        {"elden-ring", "eldenring.exe", "elden-ring"},
        {"dark-souls-iii", "DarkSoulsIII.exe", "dark-souls-iii"},
        {"dragons-dogma-2", "DD2.exe", "dragons-dogma-2"},
    };

    gsave::config::Config data;
    for (const auto& sample : samples) {
        const auto game_root = root / std::filesystem::path{sample.id};
        const auto repository = game_root / L"save";
        const auto executable = game_root / std::filesystem::path{sample.process};
        const auto parser = packages / std::filesystem::path{sample.package}
            / L"adapter.lua";
        std::filesystem::create_directories(repository);
        std::ofstream(executable, std::ios::binary) << "library fixture";
        std::ofstream(repository / L"slot.sav", std::ios::binary) << "baseline";

        auto initialized = gsave::repository::initialize_repository({
            .repository = repository,
            .game_id = sample.id,
            .parser = parser,
        });
        if (!initialized) {
            std::cerr << sample.id << ": " << initialized.error().message() << '\n';
            return 1;
        }
        // A few versions so the timeline, branch list and metadata panes have
        // something real to render.
        for (int round = 1; round <= 3; ++round) {
            std::ofstream(repository / L"slot.sav", std::ios::binary)
                << "progress-" << round;
            auto committed = gsave::repository::commit_repository({
                .repository = repository,
                .game_id = sample.id,
                .parser = parser,
                .reason = round == 3 ? "game-exit" : "quiet-period",
            });
            if (!committed) {
                std::cerr << sample.id << ": " << committed.error().message() << '\n';
                return 1;
            }
        }

        data.games.push_back(gsave::config::GameConfig{
            .id = sample.id,
            .enabled = sample.id != "dragons-dogma-2",
            .process_name = sample.process,
            .process_path = executable,
            .parser = parser,
            .saves = {{repository, {"*.sav"}, {".git/**"}}},
            .commit = gsave::core::CommitPolicy{
                .strategy = gsave::core::CommitStrategy::hybrid,
                .quiet_interval = std::chrono::seconds{5},
                .max_interval = std::chrono::seconds{300},
                .commit_on_exit = true,
            },
            .sync = gsave::config::SyncPolicy{
                .backend = gsave::config::SyncBackend::git,
                .trigger = gsave::config::SyncTrigger::on_exit,
                .remote = "origin",
            },
        });
    }

    auto saved = gsave::config::save_config_atomic(root / L"config.toml", data);
    if (!saved) {
        std::cerr << saved.error().message() << '\n';
        return 1;
    }
    std::cout << "fixture ready: " << (root / L"config.toml").string() << '\n';
    return 0;
}
