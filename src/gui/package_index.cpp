#include "gsave/gui/package_index.hpp"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace gsave::gui {
namespace {

constexpr std::int64_t supported_index_version = 1;
constexpr std::int64_t supported_package_api = 1;
constexpr std::int64_t maximum_index_bytes = 4 * 1024 * 1024;
constexpr std::int64_t maximum_archive_bytes = 64 * 1024 * 1024;

[[nodiscard]] Error invalid_index(const QString& detail) {
    return make_error(
        std::errc::invalid_argument,
        ("在线支持包清单无效：" + detail).toStdString());
}

[[nodiscard]] bool is_sha256_text(const QString& value) {
    if (value.size() != 64) return false;
    return std::ranges::all_of(value, [](const QChar character) {
        return character.isDigit()
            || (character >= QLatin1Char('a') && character <= QLatin1Char('f'))
            || (character >= QLatin1Char('A') && character <= QLatin1Char('F'));
    });
}

struct HttpDownload final {
    int status{};
    QByteArray body;
};

[[nodiscard]] Result<HttpDownload> get(
    const QUrl& url,
    const std::int64_t maximum_bytes) {
    if (!url.isValid() || url.isEmpty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "下载地址无效。"));
    }
    if (url.scheme() != QStringLiteral("https")
        && url.scheme() != QStringLiteral("http")) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "下载地址必须使用 HTTP 或 HTTPS。"));
    }

    QNetworkAccessManager manager;
    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "G-SAVE/0.1");
    request.setTransferTimeout(30000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // Abort oversized bodies while they stream instead of buffering them whole.
    QObject::connect(
        reply, &QNetworkReply::downloadProgress, reply,
        [reply, maximum_bytes](const qint64 received, const qint64 total) {
            if (received > maximum_bytes || total > maximum_bytes) reply->abort();
        });
    loop.exec();

    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = reply->readAll();
    const auto network_error = reply->error();
    const auto network_message = reply->errorString();
    delete reply;

    if (network_error != QNetworkReply::NoError) {
        return std::unexpected(make_error(
            std::errc::network_unreachable,
            ("无法访问 " + url.host() + "：" + network_message).toStdString()));
    }
    if (status < 200 || status >= 300) {
        return std::unexpected(make_error(
            std::errc::network_unreachable,
            (url.host() + " 返回 HTTP " + QString::number(status)).toStdString()));
    }
    if (body.size() > maximum_bytes) {
        return std::unexpected(make_error(
            std::errc::file_too_large, "下载内容超出允许大小。"));
    }
    return HttpDownload{.status = status, .body = std::move(body)};
}

}  // namespace

QUrl default_package_index_url() {
    return QUrl{QStringLiteral(
        "https://raw.githubusercontent.com/AirLongDian/G-SAVE/main/packages/index.json")};
}

QUrl steam_poster_url(const std::int64_t steam_app_id) {
    if (steam_app_id <= 0) return {};
    return QUrl{QStringLiteral(
        "https://cdn.cloudflare.steamstatic.com/steam/apps/%1/library_600x900.jpg")
        .arg(steam_app_id)};
}

QUrl steam_header_url(const std::int64_t steam_app_id) {
    if (steam_app_id <= 0) return {};
    return QUrl{QStringLiteral(
        "https://cdn.cloudflare.steamstatic.com/steam/apps/%1/header.jpg")
        .arg(steam_app_id)};
}

Result<PackageIndex> parse_package_index(const QByteArray& document) {
    QJsonParseError error{};
    const auto parsed = QJsonDocument::fromJson(document, &error);
    if (error.error != QJsonParseError::NoError) {
        return std::unexpected(invalid_index(error.errorString()));
    }
    if (!parsed.isObject()) {
        return std::unexpected(invalid_index(QStringLiteral("顶层必须是一个对象")));
    }
    const auto root = parsed.object();

    PackageIndex result;
    result.index_version = static_cast<std::int64_t>(
        root.value(QStringLiteral("index_version")).toInteger(0));
    if (result.index_version != supported_index_version) {
        return std::unexpected(invalid_index(
            QStringLiteral("index_version %1 不受支持，请升级 G-SAVE")
                .arg(result.index_version)));
    }
    result.updated_at = root.value(QStringLiteral("updated_at")).toString();

    const auto packages = root.value(QStringLiteral("packages"));
    if (!packages.isArray()) {
        return std::unexpected(invalid_index(QStringLiteral("packages 必须是一个数组")));
    }
    for (const auto value : packages.toArray()) {
        if (!value.isObject()) continue;
        const auto entry = value.toObject();
        IndexedPackage package;
        package.id = entry.value(QStringLiteral("id")).toString().trimmed();
        package.name = entry.value(QStringLiteral("name")).toString().trimmed();
        package.version = entry.value(QStringLiteral("version")).toString().trimmed();
        package.summary = entry.value(QStringLiteral("summary")).toString().trimmed();
        package.process_name = entry.value(
            QStringLiteral("process_name")).toString().trimmed();
        package.download = QUrl{entry.value(QStringLiteral("download")).toString()};
        package.sha256 = entry.value(QStringLiteral("sha256")).toString().trimmed();
        package.package_api = static_cast<std::int64_t>(
            entry.value(QStringLiteral("package_api")).toInteger(0));
        package.steam_app_id = static_cast<std::int64_t>(
            entry.value(QStringLiteral("steam_appid")).toInteger(0));
        package.size = static_cast<std::int64_t>(
            entry.value(QStringLiteral("size")).toInteger(0));

        // A malformed entry must not hide the rest of the index, and an entry
        // without usable integrity data must never become an install candidate.
        if (package.id.isEmpty() || package.name.isEmpty()) continue;
        if (package.package_api != supported_package_api) continue;
        if (!package.download.isValid() || package.download.isEmpty()) continue;
        if (!is_sha256_text(package.sha256)) continue;
        if (package.size <= 0 || package.size > maximum_archive_bytes) continue;
        package.sha256 = package.sha256.toLower();
        result.packages.push_back(std::move(package));
    }
    return result;
}

Result<PackageIndex> fetch_package_index(const QUrl& url) {
    auto response = get(url, maximum_index_bytes);
    if (!response) return std::unexpected(response.error());
    return parse_package_index(response->body);
}

Result<std::filesystem::path> download_indexed_package(
    const IndexedPackage& package,
    const std::filesystem::path& directory) {
    if (!is_sha256_text(package.sha256)) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "支持包缺少有效的 SHA-256 校验值。"));
    }
    if (package.size <= 0 || package.size > maximum_archive_bytes) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "支持包声明的大小无效。"));
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        return std::unexpected(make_error(
            filesystem_error, "cannot create support package download directory"));
    }

    auto response = get(package.download, package.size);
    if (!response) return std::unexpected(response.error());

    if (response->body.size() != package.size) {
        return std::unexpected(make_error(
            std::errc::illegal_byte_sequence,
            QStringLiteral("支持包「%1」下载不完整：应为 %2 字节，实际 %3 字节。")
                .arg(package.name)
                .arg(package.size)
                .arg(response->body.size())
                .toStdString()));
    }
    const auto digest = QCryptographicHash::hash(
        response->body, QCryptographicHash::Sha256).toHex();
    if (QString::fromLatin1(digest) != package.sha256) {
        return std::unexpected(make_error(
            std::errc::illegal_byte_sequence,
            QStringLiteral("支持包「%1」校验失败，文件可能已被篡改或损坏，已放弃安装。")
                .arg(package.name).toStdString()));
    }

    const auto archive = directory / (package.id.toStdWString() + L".zip");
    QFile output{QString::fromStdWString(archive.wstring())};
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return std::unexpected(make_error(
            std::errc::io_error, "cannot write downloaded support package"));
    }
    if (output.write(response->body) != response->body.size() || !output.flush()) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(archive, ignored);
        return std::unexpected(make_error(
            std::errc::io_error, "cannot store downloaded support package"));
    }
    output.close();
    return archive;
}

}  // namespace gsave::gui
