#include "gsave/gui/cloud_service.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <optional>

namespace gsave::gui {
namespace {

struct HttpResponse final {
    int status{};
    QByteArray body;
};

enum class AuthStyle {
    none,
    bearer,
    token,
    private_token,
    gitee_query,
};

[[nodiscard]] std::string utf8(const QString& value) {
    return value.toUtf8().toStdString();
}

[[nodiscard]] QString trimmed_base(QUrl value) {
    auto path = value.path();
    while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (path == QStringLiteral("/")) path.clear();
    value.setPath(path);
    value.setQuery(QString{});
    value.setFragment({});
    return value.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
}

[[nodiscard]] QUrl append_path(const QUrl& base, const QString& suffix) {
    QUrl result = base;
    QString path = result.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    path += suffix.startsWith(QLatin1Char('/')) ? suffix : QLatin1Char('/') + suffix;
    result.setPath(path);
    return result;
}

[[nodiscard]] Result<HttpResponse> send_request(
    const QByteArray& method,
    QUrl url,
    const AuthStyle auth,
    const QString& token,
    const QByteArray& body = {},
    const QByteArray& content_type = "application/json") {
    if (auth == AuthStyle::gitee_query) {
        QUrlQuery query{url};
        query.addQueryItem(QStringLiteral("access_token"), token);
        url.setQuery(query);
    }
    QNetworkAccessManager manager;
    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "G-SAVE/0.1");
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(20000);
    if (auth == AuthStyle::bearer) {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    } else if (auth == AuthStyle::token) {
        request.setRawHeader("Authorization", "token " + token.toUtf8());
    } else if (auth == AuthStyle::private_token) {
        request.setRawHeader("PRIVATE-TOKEN", token.toUtf8());
    }
    if (!body.isEmpty()) request.setRawHeader("Content-Type", content_type);

    QNetworkReply* reply = nullptr;
    if (method == QByteArrayLiteral("GET")) reply = manager.get(request);
    else if (method == QByteArrayLiteral("POST")) reply = manager.post(request, body);
    else reply = manager.sendCustomRequest(request, method, body);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto response_body = reply->readAll();
    const auto network_error = reply->error();
    const auto network_message = reply->errorString();
    delete reply;
    if (status == 0 && network_error != QNetworkReply::NoError) {
        return std::unexpected(make_error(
            std::errc::network_unreachable,
            "cannot reach Git service: " + utf8(network_message)));
    }
    return HttpResponse{status, response_body};
}

[[nodiscard]] QJsonObject json_object(const QByteArray& data) {
    const auto document = QJsonDocument::fromJson(data);
    return document.isObject() ? document.object() : QJsonObject{};
}

[[nodiscard]] Error response_error(
    const QString& action,
    const QString& service,
    const HttpResponse& response) {
    const auto context = QStringLiteral("%1 %2失败（HTTP %3）。请检查 Token 权限和服务地址。")
        .arg(service, action).arg(response.status);
    return make_error(std::errc::permission_denied, utf8(context));
}

[[nodiscard]] Result<GitServiceIdentity> authenticate(
    const GitServiceKind kind,
    const QUrl& service,
    const QUrl& api,
    const QString& token) {
    QUrl endpoint;
    AuthStyle auth = AuthStyle::none;
    QString username_field;
    switch (kind) {
    case GitServiceKind::github:
        endpoint = append_path(api, QStringLiteral("/user"));
        auth = AuthStyle::bearer;
        username_field = QStringLiteral("login");
        break;
    case GitServiceKind::gitee:
        endpoint = append_path(api, QStringLiteral("/user"));
        auth = AuthStyle::gitee_query;
        username_field = QStringLiteral("login");
        break;
    case GitServiceKind::gitlab:
        endpoint = append_path(api, QStringLiteral("/user"));
        auth = AuthStyle::private_token;
        username_field = QStringLiteral("username");
        break;
    case GitServiceKind::gitea:
        endpoint = append_path(api, QStringLiteral("/user"));
        auth = AuthStyle::token;
        username_field = QStringLiteral("login");
        break;
    }
    auto response = send_request("GET", endpoint, auth, token);
    if (!response) return std::unexpected(response.error());
    if (response->status != 200) {
        return std::unexpected(response_error(
            QStringLiteral("Token 验证"), git_service_name(kind), *response));
    }
    const auto object = json_object(response->body);
    const auto username = object.value(username_field).toString().trimmed();
    if (username.isEmpty()) {
        return std::unexpected(make_error(
            std::errc::protocol_error, "Git service did not return an account name"));
    }
    auto display = object.value(QStringLiteral("name")).toString().trimmed();
    if (display.isEmpty()) display = username;
    return GitServiceIdentity{kind, service, api, username, display};
}

[[nodiscard]] QUrl repository_endpoint(
    const GitServiceIdentity& identity,
    const QString& name) {
    const auto owner = QString::fromUtf8(QUrl::toPercentEncoding(identity.username));
    const auto repository = QString::fromUtf8(QUrl::toPercentEncoding(name));
    if (identity.kind == GitServiceKind::gitlab) {
        const auto projects = append_path(identity.api_url, QStringLiteral("/projects"));
        const auto project = QString::fromUtf8(
            QUrl::toPercentEncoding(identity.username + QLatin1Char('/') + name));
        return QUrl{projects.toString(QUrl::FullyEncoded) + QLatin1Char('/') + project};
    }
    return append_path(identity.api_url,
        QStringLiteral("/repos/%1/%2").arg(owner, repository));
}

[[nodiscard]] AuthStyle auth_style(const GitServiceKind kind) {
    switch (kind) {
    case GitServiceKind::github: return AuthStyle::bearer;
    case GitServiceKind::gitee: return AuthStyle::gitee_query;
    case GitServiceKind::gitlab: return AuthStyle::private_token;
    case GitServiceKind::gitea: return AuthStyle::token;
    }
    return AuthStyle::none;
}

[[nodiscard]] QString clone_url(
    const GitServiceIdentity& identity,
    const QJsonObject& object) {
    QString value;
    if (identity.kind == GitServiceKind::gitlab) {
        value = object.value(QStringLiteral("http_url_to_repo")).toString();
    } else if (identity.kind == GitServiceKind::gitee) {
        value = object.value(QStringLiteral("html_url")).toString();
        if (!value.isEmpty() && !value.endsWith(QStringLiteral(".git"))) value += QStringLiteral(".git");
    } else {
        value = object.value(QStringLiteral("clone_url")).toString();
    }
    QUrl url{value};
    const auto service_scheme = identity.service_url.scheme();
    if (url.isValid() && !url.host().isEmpty()
        && url.host().compare(identity.service_url.host(), Qt::CaseInsensitive) == 0
        && url.scheme() != service_scheme
        && (service_scheme == QStringLiteral("http")
            || service_scheme == QStringLiteral("https"))) {
        url.setScheme(service_scheme);
        return url.toString(QUrl::FullyEncoded);
    }
    return value;
}

[[nodiscard]] std::optional<bool> repository_is_private(
    const GitServiceKind kind,
    const QJsonObject& object) {
    if (kind == GitServiceKind::gitlab) {
        const auto visibility = object.value(QStringLiteral("visibility"));
        if (!visibility.isString()) return std::nullopt;
        return visibility.toString() == QStringLiteral("private");
    }
    const auto private_value = object.value(QStringLiteral("private"));
    if (!private_value.isBool()) return std::nullopt;
    return private_value.toBool();
}

}  // namespace

QString git_service_name(const GitServiceKind kind) {
    switch (kind) {
    case GitServiceKind::github: return QStringLiteral("GitHub");
    case GitServiceKind::gitee: return QStringLiteral("Gitee");
    case GitServiceKind::gitlab: return QStringLiteral("GitLab");
    case GitServiceKind::gitea: return QStringLiteral("Gitea");
    }
    return QStringLiteral("Git");
}

Result<QUrl> normalize_git_service_url(const QString& address) {
    auto source = address.trimmed();
    if (source.isEmpty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "Git service address must not be empty"));
    }
    if (!source.contains(QStringLiteral("://"))) source.prepend(QStringLiteral("https://"));
    QUrl url{source};
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "Git service address is invalid"));
    }
    url.setUserInfo({});
    url.setQuery(QString{});
    url.setFragment({});
    return QUrl{trimmed_base(url)};
}

QString automatic_repository_name(
    const QString& stable_game_id,
    const std::size_t repository_index,
    const std::size_t repository_count) {
    QString slug = stable_game_id.toLower();
    for (auto& character : slug) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-')) {
            character = QLatin1Char('-');
        }
    }
    while (slug.contains(QStringLiteral("--"))) slug.replace(QStringLiteral("--"), QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-'))) slug.remove(0, 1);
    while (slug.endsWith(QLatin1Char('-'))) slug.chop(1);
    if (slug.isEmpty()) slug = QStringLiteral("game");
    slug.prepend(QStringLiteral("gsave-"));
    const QString suffix = repository_count > 1
        ? QStringLiteral("-%1").arg(repository_index + 1) : QString{};
    const int maximum_base = 80 - suffix.size();
    if (slug.size() > maximum_base) slug.truncate(maximum_base);
    return slug + suffix;
}

QString service_address_from_remote(const QString& remote_url) {
    QUrl url{remote_url};
    if (!url.isValid() || url.host().isEmpty()) return {};
    const auto host = url.host().toLower();
    if (host == QStringLiteral("github.com")) return QStringLiteral("https://github.com");
    if (host == QStringLiteral("gitee.com")) return QStringLiteral("https://gitee.com");
    QStringList components = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (components.size() >= 2) {
        components.removeLast();
        components.removeLast();
    }
    url.setPath(components.isEmpty()
        ? QString{} : QLatin1Char('/') + components.join(QLatin1Char('/')));
    url.setQuery(QString{});
    url.setFragment({});
    return trimmed_base(url);
}

[[nodiscard]] Result<GitServiceIdentity> detect_gitea(
    const QUrl& service,
    const QUrl& api,
    const QString& token) {
    auto version = send_request(
        "GET", append_path(api, QStringLiteral("/version")), AuthStyle::none, {});
    if (!version) return std::unexpected(version.error());
    if (version->status == 200
        && !json_object(version->body).value(QStringLiteral("version")).toString().isEmpty()) {
        return authenticate(GitServiceKind::gitea, service, api, token);
    }
    return std::unexpected(make_error(
        std::errc::protocol_not_supported, "cannot identify this address as Gitea"));
}

[[nodiscard]] Result<GitServiceIdentity> detect_gitea_with_http_fallback(
    const QUrl& service,
    const QString& token) {
    const QUrl api = append_path(service, QStringLiteral("/api/v1"));
    auto direct = detect_gitea(service, api, token);
    if (direct || service.scheme() != QStringLiteral("https")) return direct;
    QUrl http_service = service;
    http_service.setScheme(QStringLiteral("http"));
    const QUrl http_api = append_path(http_service, QStringLiteral("/api/v1"));
    auto fallback = detect_gitea(http_service, http_api, token);
    if (fallback) return fallback;
    if (fallback.error().code == std::errc::protocol_not_supported) return fallback;
    return direct;
}

[[nodiscard]] Result<GitServiceIdentity> detect_gitlab(
    const QUrl& service,
    const QUrl& api,
    const QString& token) {
    auto version = send_request(
        "GET", append_path(api, QStringLiteral("/version")), AuthStyle::none, {});
    if (!version) return std::unexpected(version.error());
    if (version->status == 200
        && !json_object(version->body).value(QStringLiteral("version")).toString().isEmpty()) {
        return authenticate(GitServiceKind::gitlab, service, api, token);
    }
    return std::unexpected(make_error(
        std::errc::protocol_not_supported, "cannot identify this address as GitLab"));
}

[[nodiscard]] Result<GitServiceIdentity> detect_gitlab_with_http_fallback(
    const QUrl& service,
    const QString& token) {
    const QUrl api = append_path(service, QStringLiteral("/api/v4"));
    auto direct = detect_gitlab(service, api, token);
    if (direct || service.scheme() != QStringLiteral("https")) return direct;
    QUrl http_service = service;
    http_service.setScheme(QStringLiteral("http"));
    const QUrl http_api = append_path(http_service, QStringLiteral("/api/v4"));
    auto fallback = detect_gitlab(http_service, http_api, token);
    if (fallback) return fallback;
    if (fallback.error().code == std::errc::protocol_not_supported) return fallback;
    return direct;
}

Result<GitServiceIdentity> detect_git_service(
    const QString& address,
    const QString& token) {
    if (token.trimmed().isEmpty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "Git service Token must not be empty"));
    }
    auto normalized = normalize_git_service_url(address);
    if (!normalized) return std::unexpected(normalized.error());
    const auto host = normalized->host().toLower();
    if (host == QStringLiteral("github.com") || host == QStringLiteral("www.github.com")
        || host == QStringLiteral("api.github.com")) {
        return authenticate(GitServiceKind::github, QUrl{QStringLiteral("https://github.com")},
            QUrl{QStringLiteral("https://api.github.com")}, token);
    }
    if (host == QStringLiteral("gitee.com") || host == QStringLiteral("www.gitee.com")) {
        return authenticate(GitServiceKind::gitee, QUrl{QStringLiteral("https://gitee.com")},
            QUrl{QStringLiteral("https://gitee.com/api/v5")}, token);
    }
    if (host == QStringLiteral("gitlab.com") || host == QStringLiteral("www.gitlab.com")) {
        const QUrl service{QStringLiteral("https://gitlab.com")};
        return authenticate(GitServiceKind::gitlab, service,
            append_path(service, QStringLiteral("/api/v4")), token);
    }

    auto gitea = detect_gitea_with_http_fallback(*normalized, token);
    if (gitea) return gitea;
    auto gitlab = detect_gitlab_with_http_fallback(*normalized, token);
    if (gitlab) return gitlab;
    // Some GitLab installations restrict the public version endpoint. A
    // successful authenticated /user response is still an unambiguous signal.
    auto gitlab_user = authenticate(
        GitServiceKind::gitlab, *normalized,
        append_path(*normalized, QStringLiteral("/api/v4")), token);
    if (gitlab_user) return gitlab_user;
    if (normalized->scheme() == QStringLiteral("https")) {
        QUrl http_service = *normalized;
        http_service.setScheme(QStringLiteral("http"));
        auto http_gitlab_user = authenticate(
            GitServiceKind::gitlab, http_service,
            append_path(http_service, QStringLiteral("/api/v4")), token);
        if (http_gitlab_user) return http_gitlab_user;
    }
    return std::unexpected(make_error(
        std::errc::protocol_not_supported,
        "cannot identify this address as GitHub, Gitee, GitLab, or Gitea"));
}

Result<CloudRepository> ensure_private_repository(
    const GitServiceIdentity& identity,
    const QString& token,
    const QString& repository_name,
    const QString& game_name) {
    if (repository_name.isEmpty() || token.isEmpty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "repository name and Token must not be empty"));
    }
    const auto auth = auth_style(identity.kind);
    auto existing = send_request(
        "GET", repository_endpoint(identity, repository_name), auth, token);
    if (!existing) return std::unexpected(existing.error());
    if (existing->status == 200) {
        const auto object = json_object(existing->body);
        const auto privacy = repository_is_private(identity.kind, object);
        if (!privacy) {
            return std::unexpected(make_error(
                std::errc::protocol_error,
                "Git service did not report whether the existing repository is private"));
        }
        if (!*privacy) {
            return std::unexpected(make_error(
                std::errc::permission_denied,
                "a public repository already uses the automatic G-SAVE name; make it private or rename it"));
        }
        const auto url = clone_url(identity, object);
        if (url.isEmpty()) {
            return std::unexpected(make_error(
                std::errc::protocol_error, "Git service repository has no HTTPS clone URL"));
        }
        return CloudRepository{repository_name, url, false};
    }
    if (existing->status != 404) {
        return std::unexpected(response_error(
            QStringLiteral("仓库检测"), git_service_name(identity.kind), *existing));
    }

    QUrl create_url;
    QByteArray body;
    QByteArray content_type = "application/json";
    AuthStyle create_auth = auth;
    const auto description = QStringLiteral("G-SAVE 自动备份 · %1").arg(game_name);
    if (identity.kind == GitServiceKind::github) {
        create_url = append_path(identity.api_url, QStringLiteral("/user/repos"));
        body = QJsonDocument{QJsonObject{
            {QStringLiteral("name"), repository_name},
            {QStringLiteral("description"), description},
            {QStringLiteral("private"), true},
            {QStringLiteral("auto_init"), false},
        }}.toJson(QJsonDocument::Compact);
    } else if (identity.kind == GitServiceKind::gitea) {
        create_url = append_path(identity.api_url, QStringLiteral("/user/repos"));
        body = QJsonDocument{QJsonObject{
            {QStringLiteral("name"), repository_name},
            {QStringLiteral("description"), description},
            {QStringLiteral("private"), true},
            {QStringLiteral("auto_init"), false},
        }}.toJson(QJsonDocument::Compact);
    } else if (identity.kind == GitServiceKind::gitlab) {
        create_url = append_path(identity.api_url, QStringLiteral("/projects"));
        body = QJsonDocument{QJsonObject{
            {QStringLiteral("name"), game_name},
            {QStringLiteral("path"), repository_name},
            {QStringLiteral("description"), description},
            {QStringLiteral("visibility"), QStringLiteral("private")},
            {QStringLiteral("initialize_with_readme"), false},
        }}.toJson(QJsonDocument::Compact);
    } else {
        create_url = append_path(identity.api_url, QStringLiteral("/user/repos"));
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("access_token"), token);
        form.addQueryItem(QStringLiteral("name"), repository_name);
        form.addQueryItem(QStringLiteral("path"), repository_name);
        form.addQueryItem(QStringLiteral("description"), description);
        form.addQueryItem(QStringLiteral("private"), QStringLiteral("true"));
        form.addQueryItem(QStringLiteral("auto_init"), QStringLiteral("false"));
        body = form.query(QUrl::FullyEncoded).toUtf8();
        content_type = "application/x-www-form-urlencoded";
        create_auth = AuthStyle::none;
    }
    auto created = send_request(
        "POST", create_url, create_auth, token, body, content_type);
    if (!created) return std::unexpected(created.error());
    if (created->status != 201) {
        return std::unexpected(response_error(
            QStringLiteral("私有仓库创建"), git_service_name(identity.kind), *created));
    }
    const auto url = clone_url(identity, json_object(created->body));
    if (url.isEmpty()) {
        return std::unexpected(make_error(
            std::errc::protocol_error, "created Git repository has no HTTPS clone URL"));
    }
    return CloudRepository{repository_name, url, true};
}

}  // namespace gsave::gui
