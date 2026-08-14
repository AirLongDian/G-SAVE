#include "gsave/config/config.hpp"
#include "gsave/repository/repository_engine.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "usage: gui_visual_fixture SAMPLE ROOT\n";
        return 2;
    }
    const auto sample = std::filesystem::path{argv[1]};
    const auto root = std::filesystem::path{argv[2]};
    const auto repository = root / L"DarkSoulsIII";
    const auto save = repository / L"0110000139dd872d" / L"DS30000.sl2";
    const auto executable = root / L"DarkSoulsIII.exe";
    const auto parser = std::filesystem::path{GSAVE_SOURCE_DIR}
        / L"packages" / L"dark-souls-iii" / L"adapter.lua";
    std::filesystem::create_directories(save.parent_path());
    std::filesystem::copy_file(
        sample, save, std::filesystem::copy_options::overwrite_existing);
    std::ofstream(executable, std::ios::binary) << "visual fixture";

    auto initialized = gsave::repository::initialize_repository({
        .repository = repository,
        .game_id = "dark-souls-iii",
        .parser = parser,
    });
    if (!initialized) {
        std::cerr << initialized.error().message() << '\n';
        return 1;
    }
    gsave::config::Config data{{gsave::config::GameConfig{
        .id = "dark-souls-iii",
        .enabled = true,
        .process_name = "DarkSoulsIII.exe",
        .process_path = executable,
        .parser = parser,
        .saves = {{repository, {"*/DS30000.sl2"}, {}}},
        .commit = gsave::core::CommitPolicy{
            .strategy = gsave::core::CommitStrategy::hybrid,
            .quiet_interval = std::chrono::seconds{5},
            .max_interval = std::chrono::seconds{300},
            .commit_on_exit = true,
        },
        .sync = gsave::config::SyncPolicy{
            .backend = gsave::config::SyncBackend::git,
            .trigger = gsave::config::SyncTrigger::manual,
            .remote = "origin",
        },
    }}};
    auto saved = gsave::config::save_config_atomic(root / L"config.toml", data);
    if (!saved) {
        std::cerr << saved.error().message() << '\n';
        return 1;
    }
    return 0;
}
