#include "gsave/gui/gui_model.hpp"

#include "gsave/core/types.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace gsave::gui {
namespace {

template <typename T>
[[nodiscard]] Result<T> invalid(std::string message) {
    return std::unexpected(make_error(std::errc::invalid_argument, std::move(message)));
}

[[nodiscard]] Result<std::string> required_text(
    const toml::table& table,
    const std::string_view key,
    const std::string_view scope) {
    auto value = table[key].value<std::string>();
    if (!value || value->empty()) {
        return invalid<std::string>(
            std::string(scope) + "." + std::string(key) + " must be a non-empty string");
    }
    return *value;
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.filename().u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path utf8_path(const std::string_view value) {
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path{std::u8string{first, first + value.size()}};
}

[[nodiscard]] Result<core::CommitPolicy> manifest_commit(
    const toml::table& document) {
    using namespace std::chrono_literals;
    const auto* table = document["commit"].as_table();
    if (table == nullptr) {
        return core::CommitPolicy{
            .strategy = core::CommitStrategy::hybrid,
            .quiet_interval = 5s,
            .max_interval = 300s,
            .commit_on_exit = true,
        };
    }
    auto strategy = required_text(*table, "strategy", "commit");
    if (!strategy) return std::unexpected(strategy.error());
    core::CommitPolicy result;
    if (*strategy == "quiet") {
        result.strategy = core::CommitStrategy::quiet;
    } else if (*strategy == "periodic") {
        result.strategy = core::CommitStrategy::periodic;
    } else if (*strategy == "on_exit") {
        result.strategy = core::CommitStrategy::on_exit;
    } else if (*strategy == "hybrid") {
        result.strategy = core::CommitStrategy::hybrid;
    } else {
        return invalid<core::CommitPolicy>("commit.strategy is unsupported");
    }
    const auto positive_seconds = [table](const char* key)
        -> Result<std::optional<std::chrono::seconds>> {
        const auto* node = table->get(key);
        if (node == nullptr) return std::optional<std::chrono::seconds>{};
        auto value = node->value<std::int64_t>();
        if (!value || *value <= 0) {
            return invalid<std::optional<std::chrono::seconds>>(
                std::string{"commit."} + key + " must be positive");
        }
        return std::optional<std::chrono::seconds>{std::chrono::seconds{*value}};
    };
    auto quiet = positive_seconds("quiet_seconds");
    if (!quiet) return std::unexpected(quiet.error());
    auto maximum = positive_seconds("max_interval_seconds");
    if (!maximum) return std::unexpected(maximum.error());
    result.quiet_interval = *quiet;
    result.max_interval = *maximum;
    result.commit_on_exit = table->get("commit_on_exit") == nullptr
        ? result.strategy == core::CommitStrategy::on_exit
        : table->get("commit_on_exit")->value_or(false);
    if ((result.strategy == core::CommitStrategy::quiet && !result.quiet_interval)
        || (result.strategy == core::CommitStrategy::periodic && !result.max_interval)
        || (result.strategy == core::CommitStrategy::hybrid
            && (!result.quiet_interval || !result.max_interval || !result.commit_on_exit))) {
        return invalid<core::CommitPolicy>("commit policy is incomplete");
    }
    return result;
}

[[nodiscard]] Result<PackageManifest> persist_package_files(
    const PackageManifest& package,
    const std::filesystem::path& config_path,
    const std::string& installed_id) {
    const auto destination = config_path.parent_path() / L"packages"
        / utf8_path(installed_id);
    std::error_code error;
    if (std::filesystem::is_directory(destination, error)) {
        return load_package_manifest(destination / L"manifest.toml");
    }
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        return std::unexpected(make_error(
            error, "cannot create installed support package directory"));
    }
    auto temporary = destination;
    temporary += L".installing-" + std::to_wstring(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::copy(
        package.root, temporary,
        std::filesystem::copy_options::recursive,
        error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        return std::unexpected(make_error(error, "cannot copy support package files"));
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        return std::unexpected(make_error(error, "cannot publish installed support package"));
    }
    auto installed = load_package_manifest(destination / L"manifest.toml");
    if (!installed) return std::unexpected(installed.error());
    return installed;
}

}  // namespace

Result<PackageManifest> load_package_manifest(
    const std::filesystem::path& manifest_path) {
    toml::table document;
    try {
        std::ifstream input(manifest_path, std::ios::binary);
        if (!input) {
            return std::unexpected(make_error(
                std::errc::no_such_file_or_directory,
                "cannot open package manifest"));
        }
        const std::string source{
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        document = toml::parse(source);
    } catch (const toml::parse_error& error) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "package manifest parse failed: " + std::string(error.description())));
    }
    if (document["package_api"].value_or<std::int64_t>(0) != 1) {
        return invalid<PackageManifest>("package_api must be 1");
    }
    auto id = required_text(document, "id", "package");
    if (!id) return std::unexpected(id.error());
    auto name = required_text(document, "name", "package");
    if (!name) return std::unexpected(name.error());
    auto version = required_text(document, "version", "package");
    if (!version) return std::unexpected(version.error());
    auto adapter = required_text(document, "adapter", "package");
    if (!adapter) return std::unexpected(adapter.error());
    const auto* adapter_u8 = reinterpret_cast<const char8_t*>(adapter->data());
    const auto adapter_relative = std::filesystem::path{std::u8string{
        adapter_u8, adapter_u8 + adapter->size()}};
    const bool escapes_package = std::ranges::any_of(
        adapter_relative, [](const auto& component) {
            return component == std::filesystem::path{".."};
        });
    if (adapter_relative.is_absolute() || escapes_package) {
        return invalid<PackageManifest>("package adapter must stay inside the package directory");
    }
    const auto root = manifest_path.parent_path();
    const auto adapter_path = root / adapter_relative;
    if (!std::filesystem::is_regular_file(adapter_path)) {
        return invalid<PackageManifest>("package adapter file is missing");
    }
    const auto* game = document["game"].as_table();
    if (game == nullptr) {
        return invalid<PackageManifest>("package.game must be a table");
    }
    auto process_name = required_text(*game, "process_name", "package.game");
    if (!process_name) return std::unexpected(process_name.error());
    auto commit = manifest_commit(document);
    if (!commit) return std::unexpected(commit.error());

    PackageManifest result{};
    result.root = root;
    result.id = std::move(*id);
    result.name = std::move(*name);
    result.version = std::move(*version);
    result.process_name = std::move(*process_name);
    result.adapter = adapter_path;
    result.commit = std::move(*commit);
    result.generic = document["generic"].value_or(false);
    if (const auto* git = document["git"].as_table()) {
        if (const auto* patterns = (*git)["exclude_globs"].as_array()) {
            for (const auto& node : *patterns) {
                if (auto pattern = node.value<std::string>(); pattern && !pattern->empty()) {
                    result.exclude_patterns.push_back(std::move(*pattern));
                }
            }
        }
    }
    if (const auto* watch = document["watch"].as_table()) {
        const auto append_patterns = [&](const char* key, std::vector<std::string>& target) {
            if (const auto* patterns = (*watch)[key].as_array()) {
                for (const auto& node : *patterns) {
                    if (auto pattern = node.value<std::string>(); pattern && !pattern->empty()) {
                        target.push_back(std::move(*pattern));
                    }
                }
            }
        };
        append_patterns("include_globs", result.watch_include_patterns);
        append_patterns("exclude_globs", result.watch_exclude_patterns);
    }
    if (std::ranges::find(result.exclude_patterns, ".git/**")
        == result.exclude_patterns.end()) {
        result.exclude_patterns.emplace_back(".git/**");
    }
    return result;
}

Result<std::vector<PackageManifest>> scan_packages(
    const std::filesystem::path& package_root) {
    std::vector<PackageManifest> result;
    std::error_code error;
    if (!std::filesystem::is_directory(package_root, error)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(package_root, error)) {
        if (error) {
            return std::unexpected(make_error(error, "cannot enumerate game packages"));
        }
        if (!entry.is_directory()) continue;
        const auto manifest_path = entry.path() / "manifest.toml";
        if (!std::filesystem::is_regular_file(manifest_path)) continue;
        auto manifest = load_package_manifest(manifest_path);
        if (!manifest) return std::unexpected(manifest.error());
        result.push_back(std::move(*manifest));
    }
    std::ranges::sort(result, {}, &PackageManifest::name);
    return result;
}

GuiModel::GuiModel(
    std::filesystem::path config_path,
    std::filesystem::path package_root)
    : config_path_(std::move(config_path)),
      package_root_(std::move(package_root)) {}

Status GuiModel::reload() {
    std::error_code error;
    if (std::filesystem::is_regular_file(config_path_, error)) {
        auto loaded = config::load_config(config_path_);
        if (!loaded) return std::unexpected(loaded.error());
        config_ = std::move(*loaded);
    } else {
        config_ = {};
        if (auto saved = config::save_config_atomic(config_path_, config_); !saved) {
            return saved;
        }
    }
    auto discovered = scan_packages(config_path_.parent_path() / L"packages");
    if (!discovered) return std::unexpected(discovered.error());
    auto bundled = scan_packages(package_root_);
    if (!bundled) return std::unexpected(bundled.error());
    for (auto& package : *bundled) {
        if (!package.generic) continue;
        const bool already = std::ranges::any_of(
            *discovered, [&](const auto& other) { return other.id == package.id; });
        if (!already) discovered->push_back(std::move(package));
    }
    std::ranges::sort(*discovered, {}, &PackageManifest::name);
    packages_ = std::move(*discovered);
    return {};
}

const config::Config& GuiModel::configuration() const noexcept {
    return config_;
}

const std::vector<PackageManifest>& GuiModel::packages() const noexcept {
    return packages_;
}

const std::filesystem::path& GuiModel::config_path() const noexcept {
    return config_path_;
}

Status GuiModel::persist(config::Config next) {
    if (auto saved = config::save_config_atomic(config_path_, next); !saved) {
        return saved;
    }
    config_ = std::move(next);
    return {};
}

Status GuiModel::install(const InstallRequest& request) {
    if (!std::filesystem::is_regular_file(request.process_path)) {
        return std::unexpected(make_error(
            std::errc::no_such_file_or_directory, "selected game executable is missing"));
    }
    if (request.repositories.empty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "at least one save directory is required"));
    }
    const auto observed_process_name = path_utf8(request.process_path);
    if (!request.package.generic
        && core::windows_path_key(request.process_path.filename())
            != core::windows_path_key(std::filesystem::path{request.package.process_name})) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "selected executable does not match the support package"));
    }

    std::string game_id = request.package.id;
    if (request.package.generic) {
        game_id = "generic-";
        for (const unsigned char character : observed_process_name) {
            game_id.push_back(std::isalnum(character)
                ? static_cast<char>(std::tolower(character)) : '-');
        }
    }
    std::vector<std::filesystem::path> repositories;
    repositories.reserve(request.repositories.size());
    std::unordered_set<std::string> repository_keys;
    for (const auto& repository : request.repositories) {
        if (!std::filesystem::is_directory(repository.path)) {
            return std::unexpected(make_error(
                std::errc::not_a_directory, "selected save directory is missing"));
        }
        auto key = core::windows_path_key(repository.path);
        if (!repository_keys.emplace(key).second) {
            return std::unexpected(make_error(
                std::errc::invalid_argument, "save directory was selected more than once"));
        }
        repositories.push_back(repository.path);
    }

    std::optional<std::size_t> existing_index;
    for (std::size_t game_index = 0; game_index < config_.games.size(); ++game_index) {
        const auto& game = config_.games[game_index];
        if (game.id == game_id) {
            existing_index = game_index;
            continue;
        }
        for (const auto& save : game.saves) {
            for (const auto& repository : repositories) {
                if (core::windows_path_key(save.path)
                    == core::windows_path_key(repository)) {
                    return std::unexpected(make_error(
                        std::errc::file_exists,
                        "save directory is already managed by another game"));
                }
            }
        }
    }

    auto installed_package = persist_package_files(
        request.package, config_path_, game_id);
    if (!installed_package) return std::unexpected(installed_package.error());
    for (const auto& repository_path : repositories) {
        auto initialized = repository::initialize_repository(repository::InitOptions{
            .repository = repository_path,
            .game_id = game_id,
            .parser = installed_package->adapter,
            .exclude_patterns = installed_package->exclude_patterns,
        });
        if (!initialized) return std::unexpected(initialized.error());
        if (*initialized == repository::CommitOutcome::worktree_unstable) {
            return std::unexpected(make_error(
                std::errc::device_or_resource_busy,
                "save files changed while the baseline was being created"));
        }
    }

    auto next = config_;
    std::vector<config::SaveConfig> saves;
    saves.reserve(repositories.size());
    for (std::size_t index = 0; index < repositories.size(); ++index) {
        saves.push_back(config::SaveConfig{
            .path = std::move(repositories[index]),
            .include_globs = request.repositories[index].include_globs,
            .exclude_globs = request.repositories[index].exclude_globs,
        });
    }
    config::GameConfig updated{
        .id = std::move(game_id),
        .enabled = true,
        .process_name = observed_process_name,
        .process_path = request.process_path,
        .parser = installed_package->adapter,
        .saves = std::move(saves),
        .commit = installed_package->commit,
        .sync = config::SyncPolicy{
            .backend = config::SyncBackend::git,
            .trigger = config::SyncTrigger::on_exit,
            .interval = std::nullopt,
            .remote = "origin",
            .credential_reference = std::nullopt,
        },
    };
    if (existing_index) {
        updated.enabled = next.games[*existing_index].enabled;
        updated.sync = next.games[*existing_index].sync;
        next.games[*existing_index] = std::move(updated);
    } else {
        next.games.push_back(std::move(updated));
    }
    return persist(std::move(next));
}

Status GuiModel::import_package(const PackageManifest& package) {
    const auto destination = config_path_.parent_path() / L"packages"
        / utf8_path(package.id);
    const bool package_is_configured = std::ranges::any_of(
        config_.games, [&](const auto& game) { return game.id == package.id; });
    if (package_is_configured && std::filesystem::is_directory(destination)) {
        auto existing = load_package_manifest(destination / L"manifest.toml");
        if (!existing) return std::unexpected(existing.error());
        const auto existing_adapter = existing->adapter.lexically_relative(existing->root);
        const auto incoming_adapter = package.adapter.lexically_relative(package.root);
        if (core::windows_path_key(existing_adapter)
            != core::windows_path_key(incoming_adapter)) {
            return std::unexpected(make_error(
                std::errc::invalid_argument,
                "support package update changed the adapter path; refusing to interrupt an already configured game"));
        }
    }
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        return std::unexpected(make_error(
            error, "cannot create support package directory"));
    }
    auto temporary = destination;
    temporary += L".importing-" + std::to_wstring(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::copy(
        package.root, temporary,
        std::filesystem::copy_options::recursive,
        error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        return std::unexpected(make_error(
            error, "cannot copy imported support package"));
    }
    if (std::filesystem::exists(destination)) {
        auto backup = destination;
        backup += L".old-" + std::to_wstring(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::rename(destination, backup, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove_all(temporary, ignored);
            return std::unexpected(make_error(
                error, "cannot replace existing support package"));
        }
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
            std::filesystem::remove_all(temporary, ignored);
            return std::unexpected(make_error(
                error, "cannot publish imported support package"));
        }
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    } else {
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove_all(temporary, ignored);
            return std::unexpected(make_error(
                error, "cannot publish imported support package"));
        }
    }
    auto installed = load_package_manifest(destination / L"manifest.toml");
    if (!installed) return std::unexpected(installed.error());
    return reload();
}

Status GuiModel::set_enabled(const std::size_t game_index, const bool enabled) {
    if (game_index >= config_.games.size()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "selected game no longer exists"));
    }
    auto next = config_;
    next.games[game_index].enabled = enabled;
    return persist(std::move(next));
}

Status GuiModel::remove_game(const std::size_t game_index) {
    if (game_index >= config_.games.size()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "selected game no longer exists"));
    }
    auto next = config_;
    next.games.erase(next.games.begin() + static_cast<std::ptrdiff_t>(game_index));
    return persist(std::move(next));
}

Status GuiModel::update_all_sync_policies(const config::SyncPolicy& sync) {
    if (config_.games.empty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "no configured games to update"));
    }
    auto next = config_;
    for (auto& game : next.games) game.sync = sync;
    return persist(std::move(next));
}

}  // namespace gsave::gui
