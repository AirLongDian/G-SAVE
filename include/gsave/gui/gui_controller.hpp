#pragma once

#include "gsave/gui/gui_model.hpp"
#include "gsave/gui/package_index.hpp"
#include "gsave/repository/repository_engine.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace gsave::gui {

class GuiController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList games READ games NOTIFY gamesChanged)
    Q_PROPERTY(QVariantList packages READ packages NOTIFY packagesChanged)
    Q_PROPERTY(QVariantList library READ library NOTIFY packagesChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(bool historyDiverged READ historyDiverged NOTIFY historyStateChanged)
    Q_PROPERTY(QString historyStatus READ historyStatus NOTIFY historyStateChanged)
    Q_PROPERTY(bool coreRunning READ coreRunning NOTIFY serviceChanged)
    Q_PROPERTY(bool coreBusy READ coreBusy NOTIFY serviceChanged)
    Q_PROPERTY(bool autostartEnabled READ autostartEnabled NOTIFY serviceChanged)
    Q_PROPERTY(QString corePath READ corePath CONSTANT)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString packageRoot READ packageRoot CONSTANT)
    Q_PROPERTY(QString indexUrl READ indexUrl WRITE setIndexUrl NOTIFY indexChanged)
    Q_PROPERTY(QString indexStatus READ indexStatus NOTIFY indexChanged)
    Q_PROPERTY(bool indexLoading READ indexLoading NOTIFY indexChanged)
    Q_PROPERTY(bool hasPendingChanges READ hasPendingChanges NOTIFY pendingChanged)

public:
    GuiController(
        std::filesystem::path core_path,
        std::filesystem::path config_path,
        std::filesystem::path package_root,
        QObject* parent = nullptr);
    ~GuiController() override;

    [[nodiscard]] bool initialize();

    [[nodiscard]] QVariantList games() const;
    [[nodiscard]] QVariantList packages() const;
    [[nodiscard]] QVariantList library() const;
    [[nodiscard]] QVariantList history() const;
    [[nodiscard]] bool historyDiverged() const noexcept;
    [[nodiscard]] QString historyStatus() const;
    [[nodiscard]] bool coreRunning() const noexcept;
    [[nodiscard]] bool coreBusy() const noexcept;
    [[nodiscard]] bool autostartEnabled() const noexcept;
    [[nodiscard]] QString corePath() const;
    [[nodiscard]] QString configPath() const;
    [[nodiscard]] QString packageRoot() const;
    [[nodiscard]] QString indexUrl() const;
    [[nodiscard]] QString indexStatus() const;
    [[nodiscard]] bool indexLoading() const noexcept;
    [[nodiscard]] bool hasPendingChanges() const noexcept;

    void setIndexUrl(const QString& url);

    Q_INVOKABLE void reload();
    Q_INVOKABLE QVariantList repositoriesForGame(int game_index) const;
    Q_INVOKABLE QVariantMap cloudSettings() const;
    Q_INVOKABLE void refreshHistory(int game_index, int save_index);

    Q_INVOKABLE QVariantMap gameDetail(int game_index) const;
    Q_INVOKABLE QVariantMap repositoryState(int game_index, int save_index) const;
    Q_INVOKABLE QVariantList branches(int game_index, int save_index) const;
    Q_INVOKABLE QString suggestedBranchName(
        int game_index, int save_index, const QString& commit_id) const;
    Q_INVOKABLE void openCommitAsBranch(
        int game_index, int save_index, const QString& commit_id, const QString& branch);
    Q_INVOKABLE void switchToBranch(
        int game_index, int save_index, const QString& branch);

    Q_INVOKABLE void stageCommitPolicy(int game_index, const QVariantMap& policy);
    Q_INVOKABLE void discardPendingChanges();
    Q_INVOKABLE bool savePendingChanges();

    Q_INVOKABLE void refreshIndex();
    Q_INVOKABLE void installFromIndex(const QString& package_id);

    Q_INVOKABLE void installPackage(int package_index);
    Q_INVOKABLE void importPackage();
    Q_INVOKABLE void importPackageFile(const QString& zip_path);
    Q_INVOKABLE void installGenericPackage();
    Q_INVOKABLE void toggleGame(int game_index);
    Q_INVOKABLE void removeGame(int game_index);

    Q_INVOKABLE void restoreVersion(
        int game_index, int save_index, const QString& commit_id);
    Q_INVOKABLE void pushSave(int game_index, int save_index);
    Q_INVOKABLE void integrateSave(int game_index, int save_index);
    Q_INVOKABLE void resolveTimeline(
        int game_index, int save_index, bool local_as_main);
    Q_INVOKABLE void deferTimelineDecision();

    Q_INVOKABLE bool saveCloudSettings(const QVariantMap& form);
    Q_INVOKABLE void testCloudConnection(const QVariantMap& form);
    Q_INVOKABLE bool deleteCredential();

    Q_INVOKABLE void refreshService();
    Q_INVOKABLE void startCore();
    Q_INVOKABLE void stopCore();
    Q_INVOKABLE void restartCore();
    Q_INVOKABLE void toggleCore();
    Q_INVOKABLE void setAutostart(bool enabled);

signals:
    void gamesChanged();
    void packagesChanged();
    void historyChanged();
    void historyStateChanged();
    void serviceChanged();
    void indexChanged();
    void pendingChanged();
    void message(const QString& text, bool error);

private:
    struct RepositoryRef final {
        std::size_t game{};
        std::size_t save{};
    };

    [[nodiscard]] QString gameName(std::size_t game_index) const;
    [[nodiscard]] std::optional<RepositoryRef> repositoryRef(
        int game_index, int save_index) const;
    [[nodiscard]] Status runWithCorePaused(
        const std::function<Status()>& action,
        const bool* keep_stopped = nullptr);
    [[nodiscard]] Status resumeDeferredCore();
    [[nodiscard]] Status waitForCoreStarted() const;
    [[nodiscard]] Status requireGameStopped(std::size_t game_index) const;
    [[nodiscard]] const PackageManifest* packageForGame(std::size_t game_index) const;
    void installManifest(PackageManifest package);
    void refreshModels();
    void report(const Error& error);
    void report(QString message);

    std::filesystem::path core_path_;
    GuiModel model_;
    std::vector<repository::CommitInfo> history_;
    bool history_diverged_{};
    QString history_status_;
    bool core_running_{};
    bool core_busy_{};
    bool autostart_enabled_{};
    bool core_resume_pending_{};
    QUrl index_url_{default_package_index_url()};
    QString index_status_;
    bool index_loading_{};
    PackageIndex index_;
    // Configuration edits are staged here because Core has no hot reload: the
    // player is asked to save and restart the service instead of restarting it
    // once per changed field.
    std::vector<std::pair<std::size_t, core::CommitPolicy>> pending_commit_;
};

}  // namespace gsave::gui
