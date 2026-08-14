#pragma once

#include "gsave/base/error.hpp"
#include "gsave/config/config.hpp"
#include "gsave/repository/repository_engine.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gsave::gui {

struct PackageManifest final {
    std::filesystem::path root;
    std::string id;
    std::string name;
    std::string version;
    std::string process_name;
    std::filesystem::path adapter;
    std::vector<std::string> exclude_patterns;
    std::vector<std::string> watch_include_patterns;
    std::vector<std::string> watch_exclude_patterns;
    core::CommitPolicy commit;
    bool generic{};
};

struct InstallRepository final {
    std::filesystem::path path;
    std::vector<std::string> include_globs;
    std::vector<std::string> exclude_globs;
};

struct InstallRequest final {
    PackageManifest package;
    std::filesystem::path process_path;
    std::vector<InstallRepository> repositories;
};

struct PackageInstallDetection final {
    std::filesystem::path process_path;
    std::string process_name;
    std::vector<InstallRepository> repositories;
    std::vector<std::string> problems;
};

[[nodiscard]] Result<PackageManifest> load_package_manifest(
    const std::filesystem::path& manifest_path);
[[nodiscard]] Result<std::vector<PackageManifest>> scan_packages(
    const std::filesystem::path& package_root);
[[nodiscard]] Result<PackageInstallDetection> detect_package_install(
    const PackageManifest& package);

class GuiModel final {
public:
    GuiModel(
        std::filesystem::path config_path,
        std::filesystem::path package_root);

    [[nodiscard]] Status reload();
    [[nodiscard]] const config::Config& configuration() const noexcept;
    [[nodiscard]] const std::vector<PackageManifest>& packages() const noexcept;
    [[nodiscard]] const std::filesystem::path& config_path() const noexcept;

    [[nodiscard]] Status install(const InstallRequest& request);
    [[nodiscard]] Status import_package(const PackageManifest& package);
    [[nodiscard]] Status set_enabled(std::size_t game_index, bool enabled);
    [[nodiscard]] Status remove_game(std::size_t game_index);
    [[nodiscard]] Status update_all_sync_policies(const config::SyncPolicy& sync);

private:
    [[nodiscard]] Status persist(config::Config next);

    std::filesystem::path config_path_;
    std::filesystem::path package_root_;
    config::Config config_;
    std::vector<PackageManifest> packages_;
};

}  // namespace gsave::gui
