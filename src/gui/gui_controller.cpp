#include "gsave/gui/gui_controller.hpp"

#include "gsave/gui/cloud_service.hpp"
#include "gsave/gui/package_archive.hpp"
#include "gsave/gui/windows_control.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <ranges>

namespace gsave::gui {
namespace {

[[nodiscard]] QString text(const std::string& value) {
    return QString::fromUtf8(value);
}

[[nodiscard]] QString path_text(const std::filesystem::path& value) {
    return QString::fromStdWString(value.wstring());
}

[[nodiscard]] std::filesystem::path path_from(const QString& value) {
    return std::filesystem::path{value.toStdWString()};
}

[[nodiscard]] QString friendly_reason(const std::string& summary) {
    const auto colon = summary.find(": ");
    const auto at = summary.rfind(" at ");
    const auto reason = colon == std::string::npos
        ? summary
        : summary.substr(colon + 2, at == std::string::npos
            ? std::string::npos : at - colon - 2);
    if (reason == "initial-baseline") return QStringLiteral("开始保护存档");
    if (reason == "pre-restore-recovery") return QStringLiteral("恢复前的安全备份");
    if (reason.starts_with("restore-")) return QStringLiteral("恢复了历史版本");
    if (reason == "game-exit") return QStringLiteral("退出游戏时保存");
    if (reason == "quiet-period") return QStringLiteral("游戏保存完成");
    if (reason == "max-interval") return QStringLiteral("游戏持续写入时保存");
    if (reason == "automatic") return QStringLiteral("自动备份");
    return text(reason);
}

[[nodiscard]] QList<QJsonValue> ordered_values(const QJsonValue& value) {
    QList<QJsonValue> result;
    if (value.isArray()) {
        for (const auto item : value.toArray()) result.push_back(item);
        return result;
    }
    if (!value.isObject()) return result;
    auto keys = value.toObject().keys();
    std::ranges::sort(keys, [](const QString& left, const QString& right) {
        bool left_ok = false;
        bool right_ok = false;
        const int left_number = left.toInt(&left_ok);
        const int right_number = right.toInt(&right_ok);
        return left_ok && right_ok ? left_number < right_number : left < right;
    });
    for (const auto& key : keys) result.push_back(value.toObject().value(key));
    return result;
}

[[nodiscard]] QVariantList slot_rows(const std::string& metadata) {
    QVariantList result;
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(
        QByteArray::fromStdString(metadata), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return result;
    for (const auto& account_value : ordered_values(document.object().value("accounts"))) {
        const auto account = account_value.toObject();
        const auto account_id = account.value("account_id").toString();
        for (const auto& slot_value : ordered_values(account.value("slots"))) {
            const auto slot = slot_value.toObject();
            QVariantMap row;
            row.insert(QStringLiteral("account"), account_id);
            row.insert(QStringLiteral("index"), slot.value("index").toInt());
            row.insert(QStringLiteral("occupied"), slot.value("occupied").toBool());
            row.insert(QStringLiteral("name"), slot.value("character_name").toString());
            row.insert(QStringLiteral("label"), slot.value("label").toString());
            result.push_back(row);
        }
    }
    return result;
}

[[nodiscard]] std::optional<std::chrono::seconds> positive_seconds(
    const QVariant& value) {
    bool ok = false;
    const auto seconds = value.toLongLong(&ok);
    if (!ok || seconds <= 0) return std::nullopt;
    return std::chrono::seconds{seconds};
}

struct QStringWiper final {
    QString& value;
    ~QStringWiper() {
        if (!value.isEmpty()) value.fill(QChar{0});
    }
};

[[nodiscard]] QString credential_reference(const GitServiceIdentity& identity) {
    auto host = identity.service_url.host().toLower();
    if (identity.service_url.port() > 0) {
        host += QStringLiteral("-%1").arg(identity.service_url.port());
    }
    return QStringLiteral("G-SAVE/git-service/%1/%2")
        .arg(host, identity.username);
}

[[nodiscard]] bool ask(
    const QString& title,
    const QString& body,
    const QMessageBox::Icon icon = QMessageBox::Question) {
    QMessageBox box{icon, title, body, QMessageBox::NoButton};
    auto* proceed = box.addButton(QStringLiteral("继续"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    return box.clickedButton() == proceed;
}

// A dedicated package that cannot find the game must say why and stop. Falling
// back to manual pickers here would let a player point G-SAVE at the wrong
// directory and quietly version files the adapter cannot parse.
[[nodiscard]] QString detection_failure_text(
    const PackageManifest& package,
    const PackageInstallDetection& detected) {
    QString message = QStringLiteral("「%1」支持包没有找到完整的游戏和存档位置。")
        .arg(text(package.name));
    if (!detected.problems.empty()) {
        message += QStringLiteral("\n\n支持包报告：");
        for (const auto& problem : detected.problems) {
            message += QStringLiteral("\n· %1").arg(text(problem));
        }
    } else {
        if (detected.process_path.empty()) {
            message += QStringLiteral("\n\n· 未找到游戏可执行文件 %1。")
                .arg(text(package.process_name));
        }
        if (detected.repositories.empty()) {
            message += QStringLiteral("\n\n· 未找到有效的存档目录。");
        }
    }
    message += QStringLiteral(
        "\n\n请确认游戏已安装，并且至少运行过一次生成了存档，然后重新点击这张卡片。");
    return message;
}

}  // namespace

GuiController::GuiController(
    std::filesystem::path core_path,
    std::filesystem::path config_path,
    std::filesystem::path package_root,
    QObject* parent)
    : QObject(parent),
      core_path_(std::move(core_path)),
      model_(std::move(config_path), std::move(package_root)) {}

GuiController::~GuiController() {
    static_cast<void>(resumeDeferredCore());
}

bool GuiController::initialize() {
    if (auto loaded = model_.reload(); !loaded) {
        QMessageBox::critical(
            nullptr, QStringLiteral("G-SAVE 无法启动"),
            text(loaded.error().message()));
        return false;
    }
    refreshService();
    return true;
}

QVariantList GuiController::games() const {
    QVariantList result;
    const auto& games = model_.configuration().games;
    for (std::size_t index = 0; index < games.size(); ++index) {
        const auto& game = games[index];
        QVariantMap item;
        item.insert(QStringLiteral("name"), gameName(index));
        item.insert(QStringLiteral("id"), text(game.id));
        item.insert(QStringLiteral("enabled"), game.enabled);
        item.insert(QStringLiteral("process"), text(game.process_name));
        item.insert(QStringLiteral("processPath"), path_text(game.process_path));
        item.insert(QStringLiteral("saveCount"), static_cast<int>(game.saves.size()));
        item.insert(QStringLiteral("savePath"), game.saves.empty()
            ? QString{} : path_text(game.saves.front().path));
        result.push_back(item);
    }
    return result;
}

QVariantList GuiController::packages() const {
    QVariantList result;
    for (std::size_t index = 0; index < model_.packages().size(); ++index) {
        const auto& package = model_.packages()[index];
        if (package.generic) continue;
        const bool configured = std::ranges::any_of(
            model_.configuration().games,
            [&](const auto& game) { return game.id == package.id; });
        QVariantMap item;
        item.insert(QStringLiteral("index"), static_cast<int>(index));
        item.insert(QStringLiteral("name"), text(package.name));
        item.insert(QStringLiteral("version"), text(package.version));
        item.insert(QStringLiteral("configured"), configured);
        result.push_back(item);
    }
    return result;
}

QVariantList GuiController::library() const {
    // One card list for the whole library page. Installed games come first, then
    // packages that are present but not configured, then online-only entries.
    // A card without a Steam application ID simply has no poster.
    QVariantList result;
    const auto& games = model_.configuration().games;
    const auto& packages = model_.packages();

    QStringList configured_ids;
    for (std::size_t index = 0; index < games.size(); ++index) {
        const auto& game = games[index];
        const auto* package = packageForGame(index);
        const std::int64_t app_id = package == nullptr ? 0 : package->steam_app_id;
        configured_ids.push_back(text(game.id));

        QString update_version;
        if (package != nullptr) {
            const auto indexed = std::ranges::find_if(
                index_.packages, [&](const auto& entry) {
                    return entry.id == text(package->id);
                });
            if (indexed != index_.packages.end()
                && indexed->version != text(package->version)) {
                update_version = indexed->version;
            }
        }

        QVariantMap item;
        item.insert(QStringLiteral("kind"), QStringLiteral("installed"));
        item.insert(QStringLiteral("gameIndex"), static_cast<int>(index));
        item.insert(QStringLiteral("id"), text(game.id));
        item.insert(QStringLiteral("name"), gameName(index));
        item.insert(QStringLiteral("enabled"), game.enabled);
        item.insert(QStringLiteral("saveCount"), static_cast<int>(game.saves.size()));
        item.insert(QStringLiteral("savePath"), game.saves.empty()
            ? QString{} : path_text(game.saves.front().path));
        item.insert(QStringLiteral("process"), text(game.process_name));
        item.insert(QStringLiteral("version"),
            package == nullptr ? QString{} : text(package->version));
        item.insert(QStringLiteral("updateVersion"), update_version);
        item.insert(QStringLiteral("packageIndex"), package == nullptr
            ? -1
            : static_cast<int>(std::distance(packages.begin(),
                std::ranges::find_if(packages, [&](const auto& value) {
                    return value.id == package->id;
                }))));
        item.insert(QStringLiteral("poster"), steam_poster_url(app_id).toString());
        item.insert(QStringLiteral("banner"), steam_header_url(app_id).toString());
        result.push_back(item);
    }

    QStringList local_ids;
    for (std::size_t index = 0; index < packages.size(); ++index) {
        const auto& package = packages[index];
        if (package.generic) continue;
        const auto id = text(package.id);
        local_ids.push_back(id);
        if (configured_ids.contains(id)) continue;
        QVariantMap item;
        item.insert(QStringLiteral("kind"), QStringLiteral("available"));
        item.insert(QStringLiteral("source"), QStringLiteral("local"));
        item.insert(QStringLiteral("packageIndex"), static_cast<int>(index));
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("name"), text(package.name));
        item.insert(QStringLiteral("version"), text(package.version));
        item.insert(QStringLiteral("process"), text(package.process_name));
        item.insert(QStringLiteral("summary"), QString{});
        item.insert(QStringLiteral("poster"),
            steam_poster_url(package.steam_app_id).toString());
        item.insert(QStringLiteral("banner"),
            steam_header_url(package.steam_app_id).toString());
        result.push_back(item);
    }

    for (const auto& entry : index_.packages) {
        if (configured_ids.contains(entry.id) || local_ids.contains(entry.id)) continue;
        QVariantMap item;
        item.insert(QStringLiteral("kind"), QStringLiteral("available"));
        item.insert(QStringLiteral("source"), QStringLiteral("online"));
        item.insert(QStringLiteral("packageIndex"), -1);
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("version"), entry.version);
        item.insert(QStringLiteral("process"), entry.process_name);
        item.insert(QStringLiteral("summary"), entry.summary);
        item.insert(QStringLiteral("poster"),
            steam_poster_url(entry.steam_app_id).toString());
        item.insert(QStringLiteral("banner"),
            steam_header_url(entry.steam_app_id).toString());
        result.push_back(item);
    }
    return result;
}

QVariantList GuiController::history() const {
    QVariantList result;
    for (const auto& commit : history_) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), text(commit.id));
        item.insert(QStringLiteral("shortId"), text(commit.id.substr(0, 8)));
        item.insert(QStringLiteral("title"), friendly_reason(commit.summary));
        item.insert(QStringLiteral("summary"), text(commit.summary));
        item.insert(QStringLiteral("time"), QDateTime::fromSecsSinceEpoch(
            commit.committed_at).toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
        item.insert(QStringLiteral("metadata"), text(commit.metadata_json));
        const auto slot_items = slot_rows(commit.metadata_json);
        item.insert(QStringLiteral("slots"), slot_items);
        int occupied = 0;
        QStringList names;
        for (const auto& value : slot_items) {
            const auto slot = value.toMap();
            if (slot.value(QStringLiteral("occupied")).toBool()) {
                ++occupied;
                const auto name = slot.value(QStringLiteral("name")).toString();
                if (!name.isEmpty()) names.push_back(name);
            }
        }
        item.insert(QStringLiteral("slotSummary"), occupied == 0
            ? QStringLiteral("未解析到存档槽位")
            : names.empty()
                ? QStringLiteral("%1 个存档槽位").arg(occupied)
                : QStringLiteral("%1 个角色 · %2").arg(occupied).arg(names.join(QStringLiteral("、"))));
        result.push_back(item);
    }
    return result;
}

bool GuiController::historyDiverged() const noexcept { return history_diverged_; }
QString GuiController::historyStatus() const { return history_status_; }
bool GuiController::coreRunning() const noexcept { return core_running_; }
bool GuiController::coreBusy() const noexcept { return core_busy_; }
bool GuiController::autostartEnabled() const noexcept { return autostart_enabled_; }
QString GuiController::corePath() const { return path_text(core_path_); }
QString GuiController::configPath() const { return path_text(model_.config_path()); }
QString GuiController::packageRoot() const {
    return path_text(model_.config_path().parent_path() / L"packages");
}
QString GuiController::indexUrl() const { return index_url_.toString(); }
QString GuiController::indexStatus() const { return index_status_; }
bool GuiController::indexLoading() const noexcept { return index_loading_; }
bool GuiController::hasPendingChanges() const noexcept {
    return !pending_commit_.empty();
}

void GuiController::setIndexUrl(const QString& url) {
    const QUrl parsed{url.trimmed()};
    if (parsed == index_url_) return;
    if (!parsed.isValid() || parsed.isEmpty()) {
        report(make_error(std::errc::invalid_argument, "支持包清单地址无效。"));
        return;
    }
    index_url_ = parsed;
    emit indexChanged();
}

const PackageManifest* GuiController::packageForGame(
    const std::size_t game_index) const {
    const auto& games = model_.configuration().games;
    if (game_index >= games.size()) return nullptr;
    const auto found = std::ranges::find_if(model_.packages(),
        [&](const auto& value) { return value.id == games[game_index].id; });
    return found == model_.packages().end() ? nullptr : &*found;
}

QString GuiController::gameName(const std::size_t game_index) const {
    const auto& game = model_.configuration().games[game_index];
    const auto package = std::ranges::find_if(model_.packages(),
        [&](const auto& value) { return value.id == game.id; });
    if (package != model_.packages().end()) return text(package->name);
    auto stem = game.process_path.stem().wstring();
    return stem.empty() ? text(game.id) : QString::fromStdWString(stem);
}

std::optional<GuiController::RepositoryRef> GuiController::repositoryRef(
    const int game_index, const int save_index) const {
    if (game_index < 0 || save_index < 0) return std::nullopt;
    const auto game = static_cast<std::size_t>(game_index);
    const auto save = static_cast<std::size_t>(save_index);
    if (game >= model_.configuration().games.size()
        || save >= model_.configuration().games[game].saves.size()) return std::nullopt;
    return RepositoryRef{game, save};
}

void GuiController::reload() {
    if (auto loaded = model_.reload(); !loaded) report(loaded.error());
    else refreshModels();
}

QVariantList GuiController::repositoriesForGame(const int game_index) const {
    QVariantList result;
    if (game_index < 0
        || static_cast<std::size_t>(game_index) >= model_.configuration().games.size()) {
        return result;
    }
    const auto& saves = model_.configuration().games[static_cast<std::size_t>(game_index)].saves;
    for (std::size_t index = 0; index < saves.size(); ++index) {
        const auto& path = saves[index].path;
        QVariantMap item;
        item.insert(QStringLiteral("index"), static_cast<int>(index));
        item.insert(QStringLiteral("name"), saves.size() == 1
            ? QStringLiteral("默认存档")
            : QStringLiteral("存档位置 %1").arg(index + 1));
        item.insert(QStringLiteral("path"), path_text(path));
        result.push_back(item);
    }
    return result;
}

QVariantMap GuiController::cloudSettings() const {
    QVariantMap result;
    const auto& games = model_.configuration().games;
    result.insert(QStringLiteral("gameCount"), static_cast<int>(games.size()));
    if (games.empty()) {
        result.insert(QStringLiteral("trigger"), 1);
        result.insert(QStringLiteral("interval"), 300);
        result.insert(QStringLiteral("repositories"), QVariantList{});
        result.insert(QStringLiteral("allRemoteConfigured"), false);
        result.insert(QStringLiteral("credentialStored"), false);
        return result;
    }
    result.insert(QStringLiteral("trigger"), static_cast<int>(games.front().sync.trigger));
    result.insert(QStringLiteral("interval"), games.front().sync.interval
        ? static_cast<qlonglong>(games.front().sync.interval->count()) : 300);

    QString service_address;
    QString reference;
    QVariantList repositories;
    bool all_configured = true;
    for (std::size_t game_index = 0; game_index < games.size(); ++game_index) {
        const auto& game = games[game_index];
        if (reference.isEmpty() && game.sync.credential_reference) {
            reference = text(*game.sync.credential_reference);
        }
        for (std::size_t save_index = 0; save_index < game.saves.size(); ++save_index) {
            QString url;
            auto info = repository::inspect_repository(
                game.saves[save_index].path, game.sync.remote);
            if (info && info->remote_url) url = text(*info->remote_url);
            if (service_address.isEmpty() && !url.isEmpty()) {
                service_address = service_address_from_remote(url);
            }
            const bool configured = !url.isEmpty();
            all_configured = all_configured && configured;
            QVariantMap item;
            item.insert(QStringLiteral("game"), gameName(game_index));
            item.insert(QStringLiteral("repository"), automatic_repository_name(
                text(game.id), save_index, game.saves.size()));
            item.insert(QStringLiteral("configured"), configured);
            item.insert(QStringLiteral("enabled"), game.enabled);
            repositories.push_back(item);
        }
    }
    if (repositories.empty()) all_configured = false;
    result.insert(QStringLiteral("serviceAddress"), service_address);
    result.insert(QStringLiteral("repositories"), repositories);
    result.insert(QStringLiteral("repositoryCount"), repositories.size());
    result.insert(QStringLiteral("allRemoteConfigured"), all_configured);
    bool stored = false;
    if (!reference.isEmpty()) {
        auto exists = credential_exists(reference.toStdWString());
        stored = exists && *exists;
    }
    result.insert(QStringLiteral("credentialStored"), stored);
    return result;
}

void GuiController::refreshHistory(const int game_index, const int save_index) {
    history_.clear();
    history_diverged_ = false;
    history_status_.clear();
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected) {
        emit historyChanged();
        emit historyStateChanged();
        return;
    }
    const auto& game = model_.configuration().games[selected->game];
    const auto& save = game.saves[selected->save];
    auto commits = repository::list_history(save.path);
    if (!commits) report(commits.error());
    else history_ = std::move(*commits);
    auto info = repository::inspect_repository(save.path, game.sync.remote);
    if (info) {
        history_diverged_ = info->ahead != 0 && info->behind != 0;
        if (history_diverged_) {
            history_status_ = QStringLiteral(
                "本地与云端各有新的存档。请选择保留哪条完整时间线作为主线；另一条会完整保留为分支。");
        } else if (info->ahead != 0) {
            history_status_ = QStringLiteral("有 %1 个本地存档点尚未上传。").arg(info->ahead);
        } else if (info->behind != 0) {
            history_status_ = QStringLiteral("云端有 %1 个新存档点等待同步。").arg(info->behind);
        } else if (info->remote_url) {
            history_status_ = QStringLiteral("本地与云端时间线一致。\u00a0");
        } else {
            history_status_ = QStringLiteral("尚未设置云端备份，本地历史不受影响。");
        }
    }
    emit historyChanged();
    emit historyStateChanged();
}

Status GuiController::runWithCorePaused(
    const std::function<Status()>& action,
    const bool* keep_stopped) {
    auto state = inspect_process(core_path_);
    if (!state) return std::unexpected(state.error());
    const bool restart = state->running;
    if (restart) {
        if (auto stopped = stop_core(core_path_); !stopped) return stopped;
    }
    auto result = action();
    if (restart && (keep_stopped == nullptr || !*keep_stopped)) {
        auto started = start_core_elevated(core_path_, model_.config_path());
        if (started) started = waitForCoreStarted();
        if (!started && result) result = std::unexpected(started.error());
    } else if (restart) {
        core_resume_pending_ = true;
    }
    refreshService();
    return result;
}

Status GuiController::resumeDeferredCore() {
    if (!core_resume_pending_) return {};
    auto started = start_core_elevated(core_path_, model_.config_path());
    if (!started) return started;
    if (auto confirmed = waitForCoreStarted(); !confirmed) return confirmed;
    core_resume_pending_ = false;
    refreshService();
    return {};
}

Status GuiController::waitForCoreStarted() const {
    for (int attempt = 0; attempt < 60; ++attempt) {
        auto state = inspect_process(core_path_);
        if (!state) return std::unexpected(state.error());
        if (state->running) {
            QThread::msleep(250);
            auto stable = inspect_process(core_path_);
            if (!stable) return std::unexpected(stable.error());
            if (stable->running) return {};
            break;
        }
        QThread::msleep(50);
    }
    return std::unexpected(make_error(
        std::errc::protocol_error,
        "G-SAVE Core did not stay running after loading the saved configuration"));
}

Status GuiController::requireGameStopped(const std::size_t game_index) const {
    if (game_index >= model_.configuration().games.size()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "selected game no longer exists"));
    }
    auto state = inspect_process(model_.configuration().games[game_index].process_path);
    if (!state) return std::unexpected(state.error());
    if (state->running) {
        return std::unexpected(make_error(
            std::errc::device_or_resource_busy,
            "请先关闭游戏，再修改真实存档。"));
    }
    return {};
}

QVariantMap GuiController::gameDetail(const int game_index) const {
    QVariantMap result;
    const auto& games = model_.configuration().games;
    if (game_index < 0
        || static_cast<std::size_t>(game_index) >= games.size()) return result;
    const auto index = static_cast<std::size_t>(game_index);
    const auto& game = games[index];
    const auto* package = packageForGame(index);

    result.insert(QStringLiteral("index"), game_index);
    result.insert(QStringLiteral("id"), text(game.id));
    result.insert(QStringLiteral("name"), gameName(index));
    result.insert(QStringLiteral("enabled"), game.enabled);
    result.insert(QStringLiteral("process"), text(game.process_name));
    result.insert(QStringLiteral("processPath"), path_text(game.process_path));
    result.insert(QStringLiteral("parser"), path_text(game.parser));
    result.insert(QStringLiteral("packageName"),
        package == nullptr ? QStringLiteral("通用支持") : text(package->name));
    result.insert(QStringLiteral("packageVersion"),
        package == nullptr ? QString{} : text(package->version));
    result.insert(QStringLiteral("generic"),
        package == nullptr ? true : package->generic);
    result.insert(QStringLiteral("packageIndex"), package == nullptr
        ? -1
        : static_cast<int>(std::distance(model_.packages().begin(),
            std::ranges::find_if(model_.packages(), [&](const auto& value) {
                return value.id == package->id;
            }))));
    const std::int64_t app_id = package == nullptr ? 0 : package->steam_app_id;
    result.insert(QStringLiteral("banner"), steam_header_url(app_id).toString());
    result.insert(QStringLiteral("poster"), steam_poster_url(app_id).toString());

    QVariantList saves;
    for (std::size_t save_index = 0; save_index < game.saves.size(); ++save_index) {
        const auto& save = game.saves[save_index];
        QVariantMap item;
        item.insert(QStringLiteral("index"), static_cast<int>(save_index));
        item.insert(QStringLiteral("path"), path_text(save.path));
        QStringList includes;
        for (const auto& value : save.include_globs) includes.push_back(text(value));
        QStringList excludes;
        for (const auto& value : save.exclude_globs) excludes.push_back(text(value));
        item.insert(QStringLiteral("includeGlobs"), includes);
        item.insert(QStringLiteral("excludeGlobs"), excludes);
        saves.push_back(item);
    }
    result.insert(QStringLiteral("saves"), saves);

    const auto staged = std::ranges::find_if(pending_commit_,
        [&](const auto& entry) { return entry.first == index; });
    const auto& commit = staged == pending_commit_.end()
        ? game.commit : staged->second;
    QVariantMap policy;
    policy.insert(QStringLiteral("strategy"), static_cast<int>(commit.strategy));
    policy.insert(QStringLiteral("quietSeconds"), commit.quiet_interval
        ? static_cast<qlonglong>(commit.quiet_interval->count()) : 0);
    policy.insert(QStringLiteral("maxIntervalSeconds"), commit.max_interval
        ? static_cast<qlonglong>(commit.max_interval->count()) : 0);
    policy.insert(QStringLiteral("commitOnExit"), commit.commit_on_exit);
    policy.insert(QStringLiteral("pending"), staged != pending_commit_.end());
    result.insert(QStringLiteral("commit"), policy);

    QVariantMap sync;
    sync.insert(QStringLiteral("trigger"), static_cast<int>(game.sync.trigger));
    sync.insert(QStringLiteral("interval"), game.sync.interval
        ? static_cast<qlonglong>(game.sync.interval->count()) : 300);
    sync.insert(QStringLiteral("remote"), text(game.sync.remote));
    sync.insert(QStringLiteral("credentialStored"),
        game.sync.credential_reference.has_value());
    result.insert(QStringLiteral("sync"), sync);
    return result;
}

QVariantMap GuiController::repositoryState(
    const int game_index, const int save_index) const {
    QVariantMap result;
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected) return result;
    const auto& game = model_.configuration().games[selected->game];
    const auto& save = game.saves[selected->save];

    auto info = repository::inspect_repository(save.path, game.sync.remote);
    if (!info) {
        result.insert(QStringLiteral("error"), text(info.error().message()));
        return result;
    }
    result.insert(QStringLiteral("branch"), text(info->branch));
    result.insert(QStringLiteral("dirty"), info->worktree_dirty);
    result.insert(QStringLiteral("ahead"), static_cast<int>(info->ahead));
    result.insert(QStringLiteral("behind"), static_cast<int>(info->behind));
    result.insert(QStringLiteral("remoteUrl"),
        info->remote_url ? text(*info->remote_url) : QString{});

    auto history = repository::list_history(save.path, 1);
    if (history && !history->empty()) {
        result.insert(QStringLiteral("lastCommit"), text(history->front().id));
        result.insert(QStringLiteral("lastCommitAt"),
            QDateTime::fromSecsSinceEpoch(history->front().committed_at)
                .toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
        result.insert(QStringLiteral("lastReason"),
            friendly_reason(history->front().summary));
    }
    return result;
}

QVariantList GuiController::branches(
    const int game_index, const int save_index) const {
    QVariantList result;
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected) return result;
    const auto& save = model_.configuration()
        .games[selected->game].saves[selected->save];
    auto listed = repository::list_branches(save.path);
    if (!listed) return result;
    for (const auto& branch : *listed) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), text(branch.name));
        item.insert(QStringLiteral("current"), branch.current);
        item.insert(QStringLiteral("tip"), text(branch.tip_commit));
        item.insert(QStringLiteral("shortTip"), text(branch.tip_commit.substr(0, 8)));
        item.insert(QStringLiteral("time"), branch.tip_committed_at == 0
            ? QString{}
            : QDateTime::fromSecsSinceEpoch(branch.tip_committed_at)
                .toString(QStringLiteral("yyyy-MM-dd  HH:mm")));
        result.push_back(item);
    }
    return result;
}

QString GuiController::suggestedBranchName(
    const int game_index, const int save_index, const QString& commit_id) const {
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected || commit_id.isEmpty()) return {};
    const auto& save = model_.configuration()
        .games[selected->game].saves[selected->save];
    auto suggested = repository::suggest_branch_name(
        save.path, commit_id.toStdString());
    return suggested ? text(*suggested) : QString{};
}

void GuiController::openCommitAsBranch(
    const int game_index,
    const int save_index,
    const QString& commit_id,
    const QString& branch) {
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected || commit_id.isEmpty()) return;
    if (auto stopped = requireGameStopped(selected->game); !stopped) {
        report(stopped.error()); return;
    }
    const auto& save = model_.configuration()
        .games[selected->game].saves[selected->save];
    auto name = branch.trimmed();
    if (name.isEmpty()) {
        name = suggestedBranchName(game_index, save_index, commit_id);
        if (name.isEmpty()) {
            report(make_error(
                std::errc::invalid_argument, "无法为这个版本生成存档线名称。"));
            return;
        }
    }
    if (!ask(QStringLiteral("从这个版本开一条新存档线？"),
            QStringLiteral(
                "G-SAVE 会在版本 %1 建立存档线「%2」并切换过去。\n\n"
                "当前存档线保持不变，两条存档线都可以继续游玩和上传。\n"
                "切换后存档文件会变成这个版本的内容。")
                .arg(commit_id.left(8), name))) return;

    // Core must stay stopped for the whole operation: it would otherwise commit
    // into the repository while HEAD and the worktree are being moved.
    const bool keep_stopped = true;
    auto status = runWithCorePaused([&]() -> Status {
        return repository::create_branch_from_commit({
            .repository = save.path,
            .commit_id = commit_id.toStdString(),
            .branch = name.toStdString(),
        });
    }, &keep_stopped);
    if (!status) { report(status.error()); return; }
    report(QStringLiteral("已建立存档线「%1」并切换过去。存档文件现在是这个版本的内容。")
        .arg(name));
    refreshHistory(game_index, save_index);
}

void GuiController::switchToBranch(
    const int game_index, const int save_index, const QString& branch) {
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected || branch.trimmed().isEmpty()) return;
    if (auto stopped = requireGameStopped(selected->game); !stopped) {
        report(stopped.error()); return;
    }
    const auto& save = model_.configuration()
        .games[selected->game].saves[selected->save];
    const auto name = branch.trimmed();
    if (!ask(QStringLiteral("切换到这条存档线？"),
            QStringLiteral(
                "存档文件会整体替换为存档线「%1」的内容。\n\n"
                "当前存档线的历史不会丢失，随时可以切回来。")
                .arg(name))) return;

    const bool keep_stopped = true;
    auto status = runWithCorePaused([&]() -> Status {
        return repository::switch_branch(save.path, name.toStdString());
    }, &keep_stopped);
    if (!status) { report(status.error()); return; }
    report(QStringLiteral("已切换到存档线「%1」。").arg(name));
    refreshHistory(game_index, save_index);
}

void GuiController::stageCommitPolicy(
    const int game_index, const QVariantMap& policy) {
    const auto& games = model_.configuration().games;
    if (game_index < 0
        || static_cast<std::size_t>(game_index) >= games.size()) return;
    const auto index = static_cast<std::size_t>(game_index);

    core::CommitPolicy next = games[index].commit;
    if (policy.contains(QStringLiteral("strategy"))) {
        const int strategy = policy.value(QStringLiteral("strategy")).toInt();
        if (strategy < 0 || strategy > static_cast<int>(core::CommitStrategy::hybrid)) {
            report(make_error(std::errc::invalid_argument, "提交方式无效。"));
            return;
        }
        next.strategy = static_cast<core::CommitStrategy>(strategy);
    }
    if (policy.contains(QStringLiteral("quietSeconds"))) {
        const auto seconds = policy.value(QStringLiteral("quietSeconds")).toLongLong();
        if (seconds < 1 || seconds > 3600) {
            report(make_error(std::errc::invalid_argument,
                "安静时间必须在 1 到 3600 秒之间。"));
            return;
        }
        next.quiet_interval = std::chrono::seconds{seconds};
    }
    if (policy.contains(QStringLiteral("maxIntervalSeconds"))) {
        const auto seconds = policy.value(
            QStringLiteral("maxIntervalSeconds")).toLongLong();
        if (seconds < 1 || seconds > 86400) {
            report(make_error(std::errc::invalid_argument,
                "最长间隔必须在 1 到 86400 秒之间。"));
            return;
        }
        next.max_interval = std::chrono::seconds{seconds};
    }
    if (policy.contains(QStringLiteral("commitOnExit"))) {
        next.commit_on_exit = policy.value(QStringLiteral("commitOnExit")).toBool();
    }
    if (next.max_interval && next.quiet_interval
        && *next.max_interval < *next.quiet_interval) {
        report(make_error(std::errc::invalid_argument,
            "最长间隔不能短于安静时间。"));
        return;
    }

    const auto same = [](const core::CommitPolicy& left,
                         const core::CommitPolicy& right) {
        return left.strategy == right.strategy
            && left.quiet_interval == right.quiet_interval
            && left.max_interval == right.max_interval
            && left.commit_on_exit == right.commit_on_exit;
    };
    const auto staged = std::ranges::find_if(pending_commit_,
        [&](const auto& entry) { return entry.first == index; });
    if (same(next, games[index].commit)) {
        if (staged != pending_commit_.end()) pending_commit_.erase(staged);
    } else if (staged != pending_commit_.end()) {
        staged->second = next;
    } else {
        pending_commit_.emplace_back(index, next);
    }
    emit gamesChanged();
    emit pendingChanged();
}

void GuiController::discardPendingChanges() {
    if (pending_commit_.empty()) return;
    pending_commit_.clear();
    emit gamesChanged();
    emit pendingChanged();
}

bool GuiController::savePendingChanges() {
    if (pending_commit_.empty()) return true;
    // Core reads the configuration once at startup, so persisting settings and
    // restarting the service is a single operation from the player's point of
    // view. A service that was not running stays stopped.
    auto status = runWithCorePaused([&]() -> Status {
        auto next = model_.configuration();
        for (const auto& [index, policy] : pending_commit_) {
            if (index >= next.games.size()) {
                return std::unexpected(make_error(
                    std::errc::state_not_recoverable,
                    "configuration changed while settings were pending"));
            }
            next.games[index].commit = policy;
        }
        return model_.replace_configuration(std::move(next));
    });
    if (!status) { report(status.error()); return false; }
    const auto count = pending_commit_.size();
    pending_commit_.clear();
    refreshModels();
    emit pendingChanged();
    report(QStringLiteral("已保存 %1 个游戏的设置%2")
        .arg(count)
        .arg(core_running_
            ? QStringLiteral("，存档保护已重启并加载新设置。")
            : QStringLiteral("，将在下次启动存档保护时生效。")));
    return true;
}

void GuiController::refreshIndex() {
    index_loading_ = true;
    index_status_ = QStringLiteral("正在获取在线支持包清单…");
    emit indexChanged();

    auto fetched = fetch_package_index(index_url_);
    index_loading_ = false;
    if (!fetched) {
        // Not reaching the index is normal: installed cards must still show.
        index_.packages.clear();
        index_status_ = QStringLiteral("无法获取在线清单：%1")
            .arg(text(fetched.error().message()));
        emit indexChanged();
        emit packagesChanged();
        return;
    }
    index_ = std::move(*fetched);
    index_status_ = index_.packages.empty()
        ? QStringLiteral("在线清单没有可用的支持包。")
        : QStringLiteral("在线清单已更新，共 %1 个支持包%2")
            .arg(index_.packages.size())
            .arg(index_.updated_at.isEmpty()
                ? QStringLiteral("。")
                : QStringLiteral("，更新于 %1。").arg(index_.updated_at));
    emit indexChanged();
    emit packagesChanged();
}

void GuiController::installFromIndex(const QString& package_id) {
    const auto entry = std::ranges::find_if(index_.packages,
        [&](const auto& value) { return value.id == package_id; });
    if (entry == index_.packages.end()) {
        report(make_error(std::errc::no_such_file_or_directory,
            "在线清单里没有这个支持包，请先刷新清单。"));
        return;
    }
    QTemporaryDir temporary{
        QDir::tempPath() + QStringLiteral("/G-SAVE-download-XXXXXX")};
    if (!temporary.isValid()) {
        report(make_error(std::errc::io_error, "无法创建下载临时目录。"));
        return;
    }
    // The archive is verified against the declared size and SHA-256 before it is
    // allowed anywhere near the importer.
    auto archive = download_indexed_package(
        *entry, path_from(temporary.path()));
    if (!archive) { report(archive.error()); return; }
    importPackageFile(QString::fromStdWString(archive->wstring()));

    const auto imported = std::ranges::find_if(model_.packages(),
        [&](const auto& value) { return text(value.id) == package_id; });
    if (imported == model_.packages().end()) return;
    installManifest(*imported);
}

void GuiController::installPackage(const int package_index) {
    if (package_index < 0
        || static_cast<std::size_t>(package_index) >= model_.packages().size()) return;
    installManifest(model_.packages()[static_cast<std::size_t>(package_index)]);
}

void GuiController::importPackage() {
    const auto selected = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("导入游戏支持包"), {},
        QStringLiteral("G-SAVE 支持包 (*.zip)"));
    if (selected.isEmpty()) return;
    importPackageFile(selected);
}

void GuiController::importPackageFile(const QString& zip_path) {
    QTemporaryDir temporary{QDir::tempPath() + QStringLiteral("/G-SAVE-package-XXXXXX")};
    if (!temporary.isValid()) {
        report(make_error(
            std::errc::io_error, "无法创建支持包临时解压目录。"));
        return;
    }
    auto package = extract_package_archive(
        path_from(zip_path), path_from(temporary.path()));
    if (!package) {
        report(package.error());
        return;
    }
    auto status = model_.import_package(*package);
    if (!status) {
        report(status.error());
        return;
    }
    refreshModels();
    report(QStringLiteral("支持包「%1」已导入，点击卡片即可配置或修改游戏和存档位置。")
        .arg(text(package->name)));
}

void GuiController::installGenericPackage() {
    const auto found = std::ranges::find_if(
        model_.packages(), [](const auto& package) { return package.generic; });
    if (found == model_.packages().end()) {
        report(make_error(std::errc::no_such_file_or_directory, "通用支持包缺失"));
        return;
    }
    installManifest(*found);
}

void GuiController::installManifest(PackageManifest package) {
    std::filesystem::path process;
    std::vector<InstallRepository> repositories;
    if (package.generic) {
        // Only the built-in generic support asks the player for paths. A
        // dedicated package must locate everything itself, otherwise a wrong
        // manual pick would silently version the wrong directory.
        const auto executable = QFileDialog::getOpenFileName(
            nullptr, QStringLiteral("选择游戏程序"), {},
            QStringLiteral("Windows 程序 (*.exe)"));
        if (executable.isEmpty()) return;
        const auto save = QFileDialog::getExistingDirectory(
            nullptr, QStringLiteral("选择游戏存档文件夹"));
        if (save.isEmpty()) return;
        process = path_from(executable);
        repositories.push_back(InstallRepository{
            .path = path_from(save),
            .include_globs = package.watch_include_patterns,
            .exclude_globs = package.watch_exclude_patterns,
        });
    } else {
        auto detected = detect_package_install(package);
        if (!detected) { report(detected.error()); return; }
        if (detected->process_path.empty() || detected->repositories.empty()) {
            report(make_error(
                std::errc::no_such_file_or_directory,
                detection_failure_text(package, *detected).toStdString()));
            return;
        }
        process = std::move(detected->process_path);
        repositories = std::move(detected->repositories);
    }
    auto running = inspect_process(process);
    if (!running) { report(running.error()); return; }
    if (running->running) {
        report(make_error(std::errc::device_or_resource_busy,
            "安装支持前请先关闭游戏。"));
        return;
    }
    QString paths;
    for (const auto& repository : repositories) {
        paths += QStringLiteral("\n%1").arg(path_text(repository.path));
    }
    // One confirmation for both flows: a dedicated package shows what it found,
    // generic support shows what the player picked.
    const auto detail = package.generic
        ? QStringLiteral("G-SAVE 会在以下文件夹建立本地版本历史：%1\n\n"
                         "不会复制、删除或移动现有存档。").arg(paths)
        : QStringLiteral("已自动找到游戏和存档。\n\n游戏：%1\n\n存档：%2\n\n"
                         "G-SAVE 会在这些文件夹建立本地版本历史，"
                         "不会复制、删除或移动现有存档。")
            .arg(path_text(process), paths);
    if (!ask(package.generic
            ? QStringLiteral("开始保护这个存档？")
            : QStringLiteral("开始保护这个游戏？"), detail)) return;
    auto status = runWithCorePaused([&] {
        return model_.install(InstallRequest{
            .package = std::move(package),
            .process_path = process,
            .repositories = repositories,
        });
    });
    if (!status) report(status.error());
    else {
        refreshModels();
        report(QStringLiteral("游戏支持已安装，当前存档已保存为第一个时间点。"));
    }
}

void GuiController::toggleGame(const int game_index) {
    if (game_index < 0
        || static_cast<std::size_t>(game_index) >= model_.configuration().games.size()) return;
    const auto index = static_cast<std::size_t>(game_index);
    const bool enabled = !model_.configuration().games[index].enabled;
    auto status = runWithCorePaused([&] { return model_.set_enabled(index, enabled); });
    if (!status) report(status.error());
    else refreshModels();
}

void GuiController::removeGame(const int game_index) {
    if (game_index < 0
        || static_cast<std::size_t>(game_index) >= model_.configuration().games.size()) return;
    if (!ask(QStringLiteral("停止管理这个游戏？"),
            QStringLiteral("只会从 G-SAVE 中移除游戏。存档文件和全部历史都会留在原处。"),
            QMessageBox::Warning)) return;
    const auto index = static_cast<std::size_t>(game_index);
    auto status = runWithCorePaused([&] { return model_.remove_game(index); });
    if (!status) report(status.error());
    else refreshModels();
}

void GuiController::restoreVersion(
    const int game_index, const int save_index, const QString& commit_id) {
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected || commit_id.isEmpty()) return;
    if (auto stopped = requireGameStopped(selected->game); !stopped) {
        report(stopped.error()); return;
    }
    if (!ask(QStringLiteral("恢复这个存档点？"),
            QStringLiteral("当前存档会先建立一个安全备份，然后恢复所选版本。历史不会被删除。"),
            QMessageBox::Warning)) return;
    const auto& game = model_.configuration().games[selected->game];
    const auto& save = game.saves[selected->save];
    auto status = runWithCorePaused([&]() -> Status {
        auto restored = repository::restore_repository({
            .repository = save.path,
            .commit_id = commit_id.toStdString(),
            .game_id = game.id,
            .parser = game.parser,
        });
        if (!restored) return std::unexpected(restored.error());
        if (*restored == repository::CommitOutcome::worktree_unstable) {
            return std::unexpected(make_error(
                std::errc::device_or_resource_busy,
                "恢复时存档仍在变化，已退回原版本。"));
        }
        return {};
    });
    if (!status) report(status.error());
    else {
        refreshHistory(game_index, save_index);
        report(QStringLiteral("存档已恢复，并建立了新的时间点。"));
    }
}

void GuiController::pushSave(const int game_index, const int save_index) {
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected) return;
    const auto& game = model_.configuration().games[selected->game];
    const auto& save = game.saves[selected->save];
    const auto credential = game.sync.credential_reference
        ? text(*game.sync.credential_reference).toStdWString()
        : std::wstring{};
    auto status = runWithCorePaused([&] {
        return repository::push_repository({save.path, game.sync.remote, credential});
    });
    if (!status) report(status.error());
    else {
        refreshHistory(game_index, save_index);
        report(QStringLiteral("本地存档历史已上传。"));
    }
}

void GuiController::integrateSave(const int game_index, const int save_index) {
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected) return;
    if (auto stopped = requireGameStopped(selected->game); !stopped) {
        report(stopped.error()); return;
    }
    const auto& game = model_.configuration().games[selected->game];
    const auto& save = game.saves[selected->save];
    const auto credential = game.sync.credential_reference
        ? text(*game.sync.credential_reference).toStdWString()
        : std::wstring{};
    repository::IntegrateOutcome outcome{};
    bool keep_stopped = false;
    auto status = runWithCorePaused([&]() -> Status {
        auto integrated = repository::integrate_repository({
            save.path, game.sync.remote, credential});
        if (!integrated) return std::unexpected(integrated.error());
        outcome = *integrated;
        keep_stopped = outcome == repository::IntegrateOutcome::diverged;
        return {};
    }, &keep_stopped);
    if (!status) { report(status.error()); return; }
    refreshHistory(game_index, save_index);
    report(outcome == repository::IntegrateOutcome::diverged
        ? QStringLiteral("发现两条完整存档时间线。请选择一条作为主线；另一条不会丢失。")
        : QStringLiteral("云端存档历史已同步。"));
}

void GuiController::resolveTimeline(
    const int game_index, const int save_index, const bool local_as_main) {
    const auto selected = repositoryRef(game_index, save_index);
    if (!selected) return;
    if (auto stopped = requireGameStopped(selected->game); !stopped) {
        report(stopped.error()); return;
    }
    const auto& game = model_.configuration().games[selected->game];
    const auto& save = game.saves[selected->save];
    const auto credential = game.sync.credential_reference
        ? text(*game.sync.credential_reference).toStdWString()
        : std::wstring{};
    repository::TimelineResolution resolution;
    auto status = runWithCorePaused([&]() -> Status {
        auto result = repository::resolve_divergence(
            {save.path, game.sync.remote, credential},
            local_as_main ? repository::TimelineChoice::local_as_main
                          : repository::TimelineChoice::remote_as_main);
        if (!result) return std::unexpected(result.error());
        resolution = std::move(*result);
        return {};
    });
    if (!status) { report(status.error()); return; }
    if (auto resumed = resumeDeferredCore(); !resumed) { report(resumed.error()); return; }
    refreshHistory(game_index, save_index);
    report(QStringLiteral("主时间线已确定；另一条完整历史保存在分支 %1。")
        .arg(text(resolution.preserved_branch)));
}

void GuiController::deferTimelineDecision() {
    if (auto resumed = resumeDeferredCore(); !resumed) report(resumed.error());
    else report(QStringLiteral("没有修改时间线，G-SAVE 服务已继续运行。"));
}

bool GuiController::saveCloudSettings(const QVariantMap& form) {
    const auto& games = model_.configuration().games;
    if (games.empty()) {
        report(make_error(std::errc::invalid_argument, "请先添加至少一个游戏。"));
        return false;
    }
    const auto address = form.value(QStringLiteral("serviceAddress")).toString().trimmed();
    auto token = form.value(QStringLiteral("token")).toString().trimmed();
    QStringWiper wipe{token};
    QString existing_reference;
    for (const auto& game : games) {
        if (game.sync.credential_reference) {
            existing_reference = text(*game.sync.credential_reference);
            break;
        }
    }
    if (token.isEmpty() && !existing_reference.isEmpty()) {
        auto stored = load_git_credential(existing_reference.toStdWString());
        if (!stored) { report(stored.error()); return false; }
        token = QString::fromStdWString(stored->secret);
        std::ranges::fill(stored->secret, L'\0');
    }
    if (address.isEmpty() || token.isEmpty()) {
        report(make_error(std::errc::invalid_argument,
            "请填写 Git 服务地址和 Token；已保存 Token 时可留空。"));
        return false;
    }
    auto sync = games.front().sync;
    sync.backend = config::SyncBackend::git;
    const auto trigger = std::clamp(
        form.value(QStringLiteral("trigger"), 3).toInt(), 0, 3);
    sync.trigger = static_cast<config::SyncTrigger>(trigger);
    sync.interval = trigger == 2
        ? positive_seconds(form.value(QStringLiteral("interval"))) : std::nullopt;
    sync.remote = "origin";
    if (trigger == 2 && !sync.interval) {
        report(make_error(std::errc::invalid_argument,
            "定时上传需要填写有效的间隔。"));
        return false;
    }
    auto identity = detect_git_service(address, token);
    if (!identity) { report(identity.error()); return false; }
    const auto reference = credential_reference(*identity);
    sync.credential_reference = reference.toStdString();

    struct PendingRemote final {
        std::size_t game{};
        std::size_t save{};
        CloudRepository remote;
    };
    std::vector<PendingRemote> remotes;
    int created_count = 0;
    for (std::size_t game_index = 0; game_index < games.size(); ++game_index) {
        const auto& game = games[game_index];
        for (std::size_t save_index = 0; save_index < game.saves.size(); ++save_index) {
            const auto name = automatic_repository_name(
                text(game.id), save_index, game.saves.size());
            auto remote = ensure_private_repository(
                *identity, token, name, gameName(game_index));
            if (!remote) { report(remote.error()); return false; }
            if (remote->created) ++created_count;
            auto tested = repository::test_remote_connection({
                .repository = game.saves[save_index].path,
                .url = remote->clone_url.toStdString(),
                .credential_reference = {},
                .username = identity->username.toStdString(),
                .secret = token.toStdString(),
            });
            if (!tested) { report(tested.error()); return false; }
            remotes.push_back(PendingRemote{
                game_index, save_index, std::move(*remote)});
        }
    }
    if (remotes.empty()) {
        report(make_error(std::errc::invalid_argument,
            "已配置的游戏没有可备份的存档仓库。"));
        return false;
    }
    auto before = inspect_process(core_path_);
    if (!before) {
        report(before.error());
        return false;
    }
    const bool was_running = before->running;
    const auto expected_game_count = games.size();
    auto status = runWithCorePaused([&]() -> Status {
        for (const auto& pending : remotes) {
            const auto& save = games[pending.game].saves[pending.save];
            const auto url = pending.remote.clone_url.toStdString();
            if (auto remote = repository::set_remote_url(
                    save.path, sync.remote, url); !remote) return remote;
            auto inspected = repository::inspect_repository(
                save.path, sync.remote);
            if (!inspected) return std::unexpected(inspected.error());
            if (!inspected->remote_url || *inspected->remote_url != url) {
                return std::unexpected(make_error(
                    std::errc::state_not_recoverable,
                    "saved Git remote URL did not take effect"));
            }
        }
        auto credential = store_git_credential(
            reference.toStdWString(), identity->username.toStdWString(), token.toStdWString());
        if (!credential) return credential;
        if (auto saved = model_.update_all_sync_policies(sync); !saved) return saved;
        if (auto reloaded = model_.reload(); !reloaded) return reloaded;
        if (model_.configuration().games.size() != expected_game_count) {
            return std::unexpected(make_error(
                std::errc::state_not_recoverable,
                "saved configuration changed the configured game count"));
        }
        for (const auto& applied : model_.configuration().games) {
            if (applied.sync != sync) {
                return std::unexpected(make_error(
                    std::errc::state_not_recoverable,
                    "saved configuration did not apply cloud settings to every game"));
            }
        }
        return {};
    });
    if (!status) {
        report(status.error());
        return false;
    } else {
        refreshModels();
        const auto result = QStringLiteral(
            "%1 账号 %2 已连接；全部 %3 个游戏的 %4 个私有仓库已就绪，其中新建 %5 个。%6")
            .arg(git_service_name(identity->kind), identity->username)
            .arg(static_cast<qulonglong>(expected_game_count))
            .arg(static_cast<qulonglong>(remotes.size())).arg(created_count)
            .arg(was_running
                ? QStringLiteral("G-SAVE 服务已重启并加载设置。")
                : QStringLiteral("设置已保存，将在 Core 下次启动时加载。"));
        report(result + (identity->service_url.scheme() == QStringLiteral("http")
            ? QStringLiteral(" 此服务仅使用 HTTP，Token 会通过未加密连接发送；建议为服务器启用 HTTPS。")
            : QString{}));
    }
    return true;
}

void GuiController::testCloudConnection(
    const QVariantMap& form) {
    const auto& games = model_.configuration().games;
    const auto address = form.value(QStringLiteral("serviceAddress")).toString().trimmed();
    auto token = form.value(QStringLiteral("token")).toString().trimmed();
    QStringWiper wipe{token};
    if (token.isEmpty()) {
        for (const auto& game : games) {
            if (!game.sync.credential_reference) continue;
            auto stored = load_git_credential(
                text(*game.sync.credential_reference).toStdWString());
            if (!stored) { report(stored.error()); return; }
            token = QString::fromStdWString(stored->secret);
            std::ranges::fill(stored->secret, L'\0');
            break;
        }
    }
    if (address.isEmpty() || token.isEmpty()) {
        report(make_error(std::errc::invalid_argument,
            "请填写服务地址和 Token；已保存 Token 时可留空。"));
        return;
    }
    auto identity = detect_git_service(address, token);
    if (!identity) { report(identity.error()); return; }
    auto result = QStringLiteral(
        "已自动识别为 %1，Token 属于账号 %2。尚未创建或修改仓库。")
        .arg(git_service_name(identity->kind), identity->username);
    if (identity->service_url.scheme() == QStringLiteral("http")) {
        result += QStringLiteral(
            " 此服务仅使用 HTTP，Token 会通过未加密连接发送；建议为服务器启用 HTTPS。");
    }
    report(result);
}

bool GuiController::deleteCredential() {
    std::vector<QString> references;
    for (const auto& game : model_.configuration().games) {
        if (!game.sync.credential_reference) continue;
        const auto reference = text(*game.sync.credential_reference);
        if (std::ranges::find(references, reference) == references.end()) {
            references.push_back(reference);
        }
    }
    if (references.empty()) return true;
    if (!ask(QStringLiteral("删除 Token？"),
            QStringLiteral("删除后会暂停自动上传；本地版本历史不受影响。重新连接后会继续按退出游戏时上传。"),
            QMessageBox::Warning)) return false;
    auto sync = model_.configuration().games.front().sync;
    sync.credential_reference.reset();
    auto status = runWithCorePaused([&]() -> Status {
        if (auto saved = model_.update_all_sync_policies(sync); !saved) return saved;
        for (const auto& reference : references) {
            if (auto deleted = delete_git_credential(reference.toStdWString()); !deleted) {
                return deleted;
            }
        }
        return {};
    });
    refreshModels();
    if (!status) { report(status.error()); return false; }
    report(QStringLiteral("Token 已删除；自动上传已暂停，本地版本历史继续保存。"));
    return true;
}

void GuiController::refreshService() {
    auto state = inspect_process(core_path_);
    if (!state) report(state.error());
    else core_running_ = state->running;
    auto autostart = autostart_enabled();
    if (!autostart) report(autostart.error());
    else autostart_enabled_ = *autostart;
    emit serviceChanged();
}

void GuiController::startCore() {
    core_busy_ = true;
    emit serviceChanged();
    auto status = start_core_elevated(core_path_, model_.config_path());
    if (status) status = waitForCoreStarted();
    core_busy_ = false;
    if (!status) report(status.error());
    else report(QStringLiteral("G-SAVE 服务已启动。"));
    refreshService();
}

void GuiController::stopCore() {
    core_busy_ = true;
    emit serviceChanged();
    auto status = stop_core(core_path_);
    core_busy_ = false;
    if (!status) report(status.error());
    else report(QStringLiteral("G-SAVE 服务已停止。"));
    refreshService();
}

void GuiController::restartCore() {
    core_busy_ = true;
    emit serviceChanged();
    auto status = stop_core(core_path_);
    if (status) status = start_core_elevated(core_path_, model_.config_path());
    if (status) status = waitForCoreStarted();
    core_busy_ = false;
    if (!status) report(status.error());
    else report(QStringLiteral("G-SAVE 服务已重新启动。"));
    refreshService();
}

void GuiController::toggleCore() {
    // Drives the title bar indicator. The busy flag lets the view disable the
    // control so an impatient double click cannot start and stop at once.
    if (core_busy_) return;
    if (core_running_) stopCore();
    else startCore();
}

void GuiController::setAutostart(const bool enabled) {
    auto status = set_autostart(enabled, core_path_, model_.config_path());
    if (!status) report(status.error());
    else report(enabled
        ? QStringLiteral("以后登录 Windows 后会自动保护存档。")
        : QStringLiteral("已关闭登录后自动启动。"));
    refreshService();
}

void GuiController::refreshModels() {
    emit gamesChanged();
    emit packagesChanged();
    emit serviceChanged();
}

void GuiController::report(const Error& error) {
    emit message(text(error.message()), true);
}

void GuiController::report(QString value) {
    emit message(std::move(value), false);
}

}  // namespace gsave::gui
