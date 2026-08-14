#include "gsave/config/config.hpp"

#include <toml++/toml.hpp>

#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace gsave::config {
namespace {

template <typename T>
[[nodiscard]] Result<T> invalid(std::string context) {
    return std::unexpected(make_error(std::errc::invalid_argument, std::move(context)));
}

#ifndef GSAVE_CONFIG_WRITE_ONLY
[[nodiscard]] Result<std::string> required_string(
    const toml::table& table,
    const std::string_view key,
    const std::string_view scope) {
    const auto value = table[key].value<std::string>();
    if (!value || value->empty()) {
        return invalid<std::string>(
            std::string(scope) + "." + std::string(key) + " must be a non-empty string");
    }
    return *value;
}

[[nodiscard]] Result<bool> required_bool(
    const toml::table& table,
    const std::string_view key,
    const std::string_view scope) {
    const auto value = table[key].value<bool>();
    if (!value) {
        return invalid<bool>(
            std::string(scope) + "." + std::string(key) + " must be a boolean");
    }
    return *value;
}

[[nodiscard]] Result<std::chrono::seconds> required_positive_seconds(
    const toml::table& table,
    const std::string_view key,
    const std::string_view scope) {
    const auto value = table[key].value<std::int64_t>();
    if (!value || *value <= 0) {
        return invalid<std::chrono::seconds>(
            std::string(scope) + "." + std::string(key) + " must be a positive integer");
    }
    using Rep = std::chrono::seconds::rep;
    if (static_cast<std::uint64_t>(*value)
        > static_cast<std::uint64_t>(std::numeric_limits<Rep>::max())) {
        return invalid<std::chrono::seconds>(
            std::string(scope) + "." + std::string(key) + " is too large");
    }
    return std::chrono::seconds{static_cast<Rep>(*value)};
}

[[nodiscard]] std::filesystem::path normalized_path(const std::string& source) {
    const auto* first = reinterpret_cast<const char8_t*>(source.data());
    return core::normalize_windows_path_lexically(
        std::filesystem::path{std::u8string{first, first + source.size()}});
}

[[nodiscard]] std::string comparable_path(const std::filesystem::path& path) {
    return core::windows_path_key(path);
}

[[nodiscard]] bool path_text_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return comparable_path(left) == comparable_path(right);
}
#endif

#ifndef GSAVE_CONFIG_READ_ONLY
[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::string_view commit_strategy_name(
    const core::CommitStrategy strategy) noexcept {
    switch (strategy) {
    case core::CommitStrategy::quiet: return "quiet";
    case core::CommitStrategy::periodic: return "periodic";
    case core::CommitStrategy::on_exit: return "on_exit";
    case core::CommitStrategy::hybrid: return "hybrid";
    }
    return "on_exit";
}

[[nodiscard]] std::string_view sync_backend_name(
    const SyncBackend backend) noexcept {
    switch (backend) {
    case SyncBackend::git: return "git";
    case SyncBackend::webdav: return "webdav";
    case SyncBackend::onedrive: return "onedrive";
    }
    return "git";
}

[[nodiscard]] std::string_view sync_trigger_name(
    const SyncTrigger trigger) noexcept {
    switch (trigger) {
    case SyncTrigger::on_commit: return "on_commit";
    case SyncTrigger::on_exit: return "on_exit";
    case SyncTrigger::periodic: return "periodic";
    case SyncTrigger::manual: return "manual";
    }
    return "manual";
}
#endif

#ifndef GSAVE_CONFIG_WRITE_ONLY
[[nodiscard]] Result<core::CommitPolicy> parse_commit_policy(
    const toml::table& game,
    const std::string_view scope) {
    const auto* table = game["commit"].as_table();
    if (table == nullptr) {
        return invalid<core::CommitPolicy>(std::string(scope) + ".commit must be a table");
    }

    auto strategy_name = required_string(*table, "strategy", std::string(scope) + ".commit");
    if (!strategy_name) {
        return std::unexpected(strategy_name.error());
    }

    core::CommitPolicy policy;
    if (*strategy_name == "quiet") {
        policy.strategy = core::CommitStrategy::quiet;
        auto quiet = required_positive_seconds(*table, "quiet_seconds", std::string(scope) + ".commit");
        if (!quiet) {
            return std::unexpected(quiet.error());
        }
        if (table->contains("max_interval_seconds")) {
            return invalid<core::CommitPolicy>(
                std::string(scope) + ".commit.max_interval_seconds is not valid for quiet strategy");
        }
        policy.quiet_interval = *quiet;
        policy.commit_on_exit = table->get("commit_on_exit") == nullptr
            ? false
            : table->get("commit_on_exit")->value_or(false);
        if (policy.commit_on_exit) {
            return invalid<core::CommitPolicy>(
                std::string(scope) + ".commit.commit_on_exit requires hybrid strategy");
        }
    } else if (*strategy_name == "periodic") {
        policy.strategy = core::CommitStrategy::periodic;
        auto maximum = required_positive_seconds(
            *table, "max_interval_seconds", std::string(scope) + ".commit");
        if (!maximum) {
            return std::unexpected(maximum.error());
        }
        if (table->contains("quiet_seconds")) {
            return invalid<core::CommitPolicy>(
                std::string(scope) + ".commit.quiet_seconds is not valid for periodic strategy");
        }
        policy.max_interval = *maximum;
        policy.commit_on_exit = table->get("commit_on_exit") == nullptr
            ? false
            : table->get("commit_on_exit")->value_or(false);
        if (policy.commit_on_exit) {
            return invalid<core::CommitPolicy>(
                std::string(scope) + ".commit.commit_on_exit requires hybrid strategy");
        }
    } else if (*strategy_name == "on_exit") {
        policy.strategy = core::CommitStrategy::on_exit;
        if (table->contains("quiet_seconds") || table->contains("max_interval_seconds")) {
            return invalid<core::CommitPolicy>(
                std::string(scope) + ".commit timing fields are not valid for on_exit strategy");
        }
        policy.commit_on_exit = table->get("commit_on_exit") == nullptr
            ? true
            : table->get("commit_on_exit")->value_or(false);
        if (!policy.commit_on_exit) {
            return invalid<core::CommitPolicy>(
                std::string(scope) + ".commit.commit_on_exit must be true for on_exit strategy");
        }
    } else if (*strategy_name == "hybrid") {
        policy.strategy = core::CommitStrategy::hybrid;
        auto quiet = required_positive_seconds(*table, "quiet_seconds", std::string(scope) + ".commit");
        if (!quiet) {
            return std::unexpected(quiet.error());
        }
        auto maximum = required_positive_seconds(
            *table, "max_interval_seconds", std::string(scope) + ".commit");
        if (!maximum) {
            return std::unexpected(maximum.error());
        }
        auto on_exit = required_bool(*table, "commit_on_exit", std::string(scope) + ".commit");
        if (!on_exit) {
            return std::unexpected(on_exit.error());
        }
        if (!*on_exit) {
            return invalid<core::CommitPolicy>(
                std::string(scope) + ".commit.commit_on_exit must be true for hybrid strategy");
        }
        policy.quiet_interval = *quiet;
        policy.max_interval = *maximum;
        policy.commit_on_exit = true;
    } else {
        return invalid<core::CommitPolicy>(
            std::string(scope) + ".commit.strategy has an unsupported value");
    }

    return policy;
}

[[nodiscard]] Result<SyncPolicy> parse_sync_policy(
    const toml::table& game,
    const std::string_view scope) {
    const auto* table = game["sync"].as_table();
    if (table == nullptr) {
        return invalid<SyncPolicy>(std::string(scope) + ".sync must be a table");
    }

    auto backend_name = required_string(*table, "backend", std::string(scope) + ".sync");
    if (!backend_name) {
        return std::unexpected(backend_name.error());
    }
    auto trigger_name = required_string(*table, "trigger", std::string(scope) + ".sync");
    if (!trigger_name) {
        return std::unexpected(trigger_name.error());
    }

    SyncPolicy policy;
    if (*backend_name == "git") {
        policy.backend = SyncBackend::git;
    } else if (*backend_name == "webdav") {
        policy.backend = SyncBackend::webdav;
    } else if (*backend_name == "onedrive") {
        policy.backend = SyncBackend::onedrive;
    } else {
        return invalid<SyncPolicy>(std::string(scope) + ".sync.backend has an unsupported value");
    }

    if (*trigger_name == "on_commit") {
        policy.trigger = SyncTrigger::on_commit;
    } else if (*trigger_name == "on_exit") {
        policy.trigger = SyncTrigger::on_exit;
    } else if (*trigger_name == "periodic") {
        policy.trigger = SyncTrigger::periodic;
        auto interval = required_positive_seconds(
            *table, "interval_seconds", std::string(scope) + ".sync");
        if (!interval) {
            return std::unexpected(interval.error());
        }
        policy.interval = *interval;
    } else if (*trigger_name == "manual") {
        policy.trigger = SyncTrigger::manual;
    } else {
        return invalid<SyncPolicy>(std::string(scope) + ".sync.trigger has an unsupported value");
    }

    if (policy.trigger != SyncTrigger::periodic && table->contains("interval_seconds")) {
        return invalid<SyncPolicy>(
            std::string(scope) + ".sync.interval_seconds requires periodic trigger");
    }
    if (const auto remote = table->get("remote")) {
        const auto value = remote->value<std::string>();
        if (!value || value->empty()) {
            return invalid<SyncPolicy>(
                std::string(scope) + ".sync.remote must be a non-empty string");
        }
        policy.remote = *value;
    }
    if (const auto credential = table->get("credential_ref")) {
        const auto value = credential->value<std::string>();
        if (!value || value->empty()) {
            return invalid<SyncPolicy>(
                std::string(scope) + ".sync.credential_ref must be a non-empty string");
        }
        policy.credential_reference = *value;
    }
    return policy;
}

[[nodiscard]] Result<std::vector<SaveConfig>> parse_saves(
    const toml::table& game,
    const std::string_view scope,
    std::unordered_set<std::string>& repository_paths) {
    const auto* saves = game["saves"].as_array();
    if (saves == nullptr || saves->empty()) {
        return invalid<std::vector<SaveConfig>>(
            std::string(scope) + ".saves must contain at least one repository");
    }

    std::vector<SaveConfig> result;
    result.reserve(saves->size());
    for (std::size_t index = 0; index < saves->size(); ++index) {
        const auto* save = (*saves)[index].as_table();
        const auto save_scope = std::string(scope) + ".saves[" + std::to_string(index) + "]";
        if (save == nullptr) {
            return invalid<std::vector<SaveConfig>>(save_scope + " must be a table");
        }
        auto path_text = required_string(*save, "path", save_scope);
        if (!path_text) {
            return std::unexpected(path_text.error());
        }
        auto path = normalized_path(*path_text);
        if (!core::is_windows_absolute_path(path)) {
            return invalid<std::vector<SaveConfig>>(save_scope + ".path must be absolute");
        }
        if (!repository_paths.emplace(comparable_path(path)).second) {
            return invalid<std::vector<SaveConfig>>(save_scope + ".path duplicates another repository");
        }
        const auto parse_globs = [&](const char* key) -> Result<std::vector<std::string>> {
            std::vector<std::string> values;
            const auto* node = save->get(key);
            if (node == nullptr) return values;
            const auto* array = node->as_array();
            if (array == nullptr || array->size() > 128) {
                return invalid<std::vector<std::string>>(
                    save_scope + "." + key + " must be an array of at most 128 strings");
            }
            values.reserve(array->size());
            for (const auto& item : *array) {
                auto value = item.value<std::string>();
                if (!value || value->empty() || value->size() > 512
                    || value->front() == '/' || value->front() == '\\'
                    || value->find(':') != std::string::npos) {
                    return invalid<std::vector<std::string>>(
                        save_scope + "." + key + " contains an invalid relative glob");
                }
                values.push_back(std::move(*value));
            }
            return values;
        };
        auto includes = parse_globs("include_globs");
        if (!includes) return std::unexpected(includes.error());
        auto excludes = parse_globs("exclude_globs");
        if (!excludes) return std::unexpected(excludes.error());
        result.push_back(SaveConfig{
            .path = std::move(path),
            .include_globs = std::move(*includes),
            .exclude_globs = std::move(*excludes),
        });
    }
    return result;
}
#endif

}  // namespace

#ifndef GSAVE_CONFIG_WRITE_ONLY
Result<Config> parse_config(const std::string_view source) {
    toml::table document;
    try {
        document = toml::parse(source);
    } catch (const toml::parse_error& error) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "TOML parse failed: " + std::string(error.description())));
    }

    const auto* games = document["games"].as_array();
    if (games == nullptr) {
        return invalid<Config>("games must be an array of tables");
    }

    Config config;
    config.games.reserve(games->size());
    std::unordered_set<std::string> game_ids;
    std::unordered_set<std::string> repository_paths;

    for (std::size_t index = 0; index < games->size(); ++index) {
        const auto* table = (*games)[index].as_table();
        const auto scope = "games[" + std::to_string(index) + "]";
        if (table == nullptr) {
            return invalid<Config>(scope + " must be a table");
        }

        GameConfig game;
        auto id = required_string(*table, "id", scope);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (!game_ids.emplace(*id).second) {
            return invalid<Config>(scope + ".id duplicates another game id");
        }
        game.id = std::move(*id);

        auto enabled = required_bool(*table, "enabled", scope);
        if (!enabled) {
            return std::unexpected(enabled.error());
        }
        game.enabled = *enabled;

        auto process_name = required_string(*table, "process_name", scope);
        if (!process_name) {
            return std::unexpected(process_name.error());
        }
        const auto* process_name_first =
            reinterpret_cast<const char8_t*>(process_name->data());
        const auto process_name_path = std::filesystem::path{std::u8string{
            process_name_first,
            process_name_first + process_name->size()}};
        if (process_name_path.filename() != process_name_path || process_name_path.has_parent_path()) {
            return invalid<Config>(scope + ".process_name must be a file name, not a path");
        }
        game.process_name = std::move(*process_name);

        auto process_path = required_string(*table, "process_path", scope);
        if (!process_path) {
            return std::unexpected(process_path.error());
        }
        game.process_path = normalized_path(*process_path);
        if (!core::is_windows_absolute_path(game.process_path)) {
            return invalid<Config>(scope + ".process_path must be absolute");
        }
        if (!path_text_equal(game.process_path.filename(), std::filesystem::path(game.process_name))) {
            return invalid<Config>(scope + ".process_name must match the process_path file name");
        }

        auto parser = required_string(*table, "parser", scope);
        if (!parser) {
            return std::unexpected(parser.error());
        }
        game.parser = normalized_path(*parser);

        auto saves = parse_saves(*table, scope, repository_paths);
        if (!saves) {
            return std::unexpected(saves.error());
        }
        game.saves = std::move(*saves);

        auto commit = parse_commit_policy(*table, scope);
        if (!commit) {
            return std::unexpected(commit.error());
        }
        game.commit = std::move(*commit);

        auto sync = parse_sync_policy(*table, scope);
        if (!sync) {
            return std::unexpected(sync.error());
        }
        game.sync = std::move(*sync);
        config.games.push_back(std::move(game));
    }

    return config;
}

Result<Config> load_config(const std::filesystem::path& path) {
    errno = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        const auto code = errno == 0
            ? std::make_error_code(std::errc::no_such_file_or_directory)
            : std::error_code(errno, std::generic_category());
        return std::unexpected(make_error(code, "cannot open configuration " + path.string()));
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        return std::unexpected(make_error(
            std::errc::io_error,
            "cannot read configuration " + path.string()));
    }
    return parse_config(buffer.str());
}
#endif

#ifndef GSAVE_CONFIG_READ_ONLY
Result<std::string> serialize_config(const Config& config) {
    toml::table document;
    toml::array games;
    for (const auto& game : config.games) {
        toml::table game_table;
        game_table.insert("id", game.id);
        game_table.insert("enabled", game.enabled);
        game_table.insert("process_name", game.process_name);
        game_table.insert("process_path", path_utf8(game.process_path));
        game_table.insert("parser", path_utf8(game.parser));

        toml::array saves;
        for (const auto& save : game.saves) {
            toml::table save_table;
            save_table.insert("path", path_utf8(save.path));
            if (!save.include_globs.empty()) {
                toml::array patterns;
                for (const auto& pattern : save.include_globs) patterns.push_back(pattern);
                save_table.insert("include_globs", std::move(patterns));
            }
            if (!save.exclude_globs.empty()) {
                toml::array patterns;
                for (const auto& pattern : save.exclude_globs) patterns.push_back(pattern);
                save_table.insert("exclude_globs", std::move(patterns));
            }
            saves.push_back(std::move(save_table));
        }
        game_table.insert("saves", std::move(saves));

        toml::table commit;
        commit.insert("strategy", commit_strategy_name(game.commit.strategy));
        if (game.commit.quiet_interval) {
            commit.insert("quiet_seconds", game.commit.quiet_interval->count());
        }
        if (game.commit.max_interval) {
            commit.insert("max_interval_seconds", game.commit.max_interval->count());
        }
        if (game.commit.commit_on_exit
            || game.commit.strategy == core::CommitStrategy::hybrid
            || game.commit.strategy == core::CommitStrategy::on_exit) {
            commit.insert("commit_on_exit", game.commit.commit_on_exit);
        }
        game_table.insert("commit", std::move(commit));

        toml::table sync;
        sync.insert("backend", sync_backend_name(game.sync.backend));
        sync.insert("trigger", sync_trigger_name(game.sync.trigger));
        if (game.sync.interval) {
            sync.insert("interval_seconds", game.sync.interval->count());
        }
        sync.insert("remote", game.sync.remote);
        if (game.sync.credential_reference) {
            sync.insert("credential_ref", *game.sync.credential_reference);
        }
        game_table.insert("sync", std::move(sync));
        games.push_back(std::move(game_table));
    }
    document.insert("games", std::move(games));

    std::ostringstream output;
    output << document;
    auto text = std::move(output).str();
    auto round_trip = parse_config(text);
    if (!round_trip) {
        return std::unexpected(make_error(
            round_trip.error().code,
            "generated configuration is invalid: " + round_trip.error().context));
    }
    return text;
}

Status save_config_atomic(
    const std::filesystem::path& path,
    const Config& config) {
    auto serialized = serialize_config(config);
    if (!serialized) return std::unexpected(serialized.error());

    std::error_code filesystem_error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected(make_error(
                filesystem_error, "cannot create configuration directory"));
        }
    }

#ifdef _WIN32
    auto temporary = path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"."
        + std::to_wstring(GetTickCount64());
    HANDLE file = CreateFileW(
        temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::unexpected(make_error(
            std::error_code(static_cast<int>(GetLastError()), std::system_category()),
            "cannot create temporary configuration"));
    }
    const auto close_and_remove = [&] {
        CloseHandle(file);
        DeleteFileW(temporary.c_str());
    };
    std::size_t written_total = 0;
    while (written_total < serialized->size()) {
        const auto remaining = std::min<std::size_t>(
            serialized->size() - written_total,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (WriteFile(
                file, serialized->data() + written_total,
                static_cast<DWORD>(remaining), &written, nullptr) == FALSE
            || written == 0) {
            const auto error = GetLastError();
            close_and_remove();
            return std::unexpected(make_error(
                std::error_code(static_cast<int>(error), std::system_category()),
                "cannot write temporary configuration"));
        }
        written_total += written;
    }
    if (FlushFileBuffers(file) == FALSE) {
        const auto error = GetLastError();
        close_and_remove();
        return std::unexpected(make_error(
            std::error_code(static_cast<int>(error), std::system_category()),
            "cannot flush temporary configuration"));
    }
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str());
        return std::unexpected(make_error(
            std::error_code(static_cast<int>(error), std::system_category()),
            "cannot replace configuration atomically"));
    }
#else
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output_file(temporary, std::ios::binary | std::ios::trunc);
        output_file.write(serialized->data(), static_cast<std::streamsize>(serialized->size()));
        output_file.flush();
        if (!output_file) {
            std::filesystem::remove(temporary, filesystem_error);
            return std::unexpected(make_error(
                std::errc::io_error, "cannot write temporary configuration"));
        }
    }
    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(temporary, filesystem_error);
        return std::unexpected(make_error(
            filesystem_error, "cannot replace configuration atomically"));
    }
#endif
    return {};
}
#endif

}  // namespace gsave::config
