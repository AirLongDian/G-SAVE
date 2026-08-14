#pragma once

#include "gsave/base/error.hpp"

#include <QString>
#include <QUrl>

#include <cstddef>

namespace gsave::gui {

enum class GitServiceKind {
    github,
    gitee,
    gitlab,
    gitea,
};

struct GitServiceIdentity final {
    GitServiceKind kind{GitServiceKind::gitea};
    QUrl service_url;
    QUrl api_url;
    QString username;
    QString display_name;
};

struct CloudRepository final {
    QString name;
    QString clone_url;
    bool created{};
};

[[nodiscard]] QString git_service_name(GitServiceKind kind);
[[nodiscard]] Result<QUrl> normalize_git_service_url(const QString& address);
[[nodiscard]] QString automatic_repository_name(
    const QString& stable_game_id,
    std::size_t repository_index,
    std::size_t repository_count);
[[nodiscard]] QString service_address_from_remote(const QString& remote_url);

// These operations are GUI-only and synchronous by design. Each call creates a
// short-lived Qt network manager and leaves no worker or network state behind.
[[nodiscard]] Result<GitServiceIdentity> detect_git_service(
    const QString& address,
    const QString& token);
[[nodiscard]] Result<CloudRepository> ensure_private_repository(
    const GitServiceIdentity& identity,
    const QString& token,
    const QString& repository_name,
    const QString& game_name);

}  // namespace gsave::gui
