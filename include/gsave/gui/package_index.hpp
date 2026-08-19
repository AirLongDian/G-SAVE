#pragma once

#include "gsave/base/error.hpp"

#include <QString>
#include <QUrl>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gsave::gui {

// One entry of the online support package index. The index only carries what the
// library card needs before download plus the integrity data needed to trust the
// archive; poster URLs are derived from steam_app_id instead of being stored.
struct IndexedPackage final {
    QString id;
    QString name;
    QString version;
    QString summary;
    QString process_name;
    QUrl download;
    QString sha256;
    std::int64_t package_api{};
    std::int64_t steam_app_id{};
    std::int64_t size{};
};

struct PackageIndex final {
    std::int64_t index_version{};
    QString updated_at;
    std::vector<IndexedPackage> packages;
};

[[nodiscard]] QUrl default_package_index_url();
[[nodiscard]] QUrl steam_poster_url(std::int64_t steam_app_id);
[[nodiscard]] QUrl steam_header_url(std::int64_t steam_app_id);

[[nodiscard]] Result<PackageIndex> parse_package_index(const QByteArray& document);

// GUI-only and synchronous by design. Each call creates a short-lived Qt network
// manager and leaves no worker or network state behind. Failing to reach the
// index is not an error for the library page: already installed packages must
// still be listed.
[[nodiscard]] Result<PackageIndex> fetch_package_index(const QUrl& url);

// Downloads an indexed package into a caller-owned directory and verifies the
// declared size and SHA-256 before returning. A tampered or truncated archive is
// rejected before it can reach the package importer.
[[nodiscard]] Result<std::filesystem::path> download_indexed_package(
    const IndexedPackage& package,
    const std::filesystem::path& directory);

}  // namespace gsave::gui
