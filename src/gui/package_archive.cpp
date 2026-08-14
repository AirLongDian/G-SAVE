#include "gsave/gui/package_archive.hpp"

#include <QDir>
#include <QString>
#include <QtCore/private/qzipreader_p.h>

#include <cstdint>
#include <fstream>
#include <vector>

namespace gsave::gui {
namespace {

constexpr std::size_t maximum_entries = 512;
constexpr std::uint64_t maximum_uncompressed_bytes = 64U * 1024U * 1024U;

[[nodiscard]] bool safe_archive_path(const QString& raw) {
    const QString normalized = QDir::fromNativeSeparators(raw);
    if (normalized.isEmpty() || normalized.startsWith(QLatin1Char('/'))
        || normalized.contains(QLatin1Char(':'))) return false;
    const QString clean = QDir::cleanPath(normalized);
    return clean != QStringLiteral("..")
        && !clean.startsWith(QStringLiteral("../"));
}

}  // namespace

Result<PackageManifest> extract_package_archive(
    const std::filesystem::path& archive,
    const std::filesystem::path& destination) {
    if (QString::fromStdWString(archive.extension().wstring()).compare(
            QStringLiteral(".zip"), Qt::CaseInsensitive) != 0) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "support package must be a ZIP archive"));
    }
    std::error_code error;
    if (!std::filesystem::is_directory(destination, error)) {
        return std::unexpected(make_error(
            std::errc::not_a_directory, "temporary package directory is missing"));
    }

    QZipReader reader{QString::fromStdWString(archive.wstring())};
    if (!reader.exists() || !reader.isReadable() || reader.status() != QZipReader::NoError) {
        return std::unexpected(make_error(
            std::errc::invalid_argument, "support package ZIP cannot be read"));
    }
    const auto entries = reader.fileInfoList();
    if (entries.empty() || static_cast<std::size_t>(entries.size()) > maximum_entries) {
        return std::unexpected(make_error(
            std::errc::file_too_large, "support package ZIP has an invalid entry count"));
    }
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        if (!entry.isValid() || entry.isSymLink || !safe_archive_path(entry.filePath)
            || entry.size < 0) {
            return std::unexpected(make_error(
                std::errc::invalid_argument,
                "support package ZIP contains an unsafe path or symbolic link"));
        }
        total += static_cast<std::uint64_t>(entry.size);
        if (total > maximum_uncompressed_bytes) {
            return std::unexpected(make_error(
                std::errc::file_too_large,
                "support package ZIP expands beyond the 64 MiB safety limit"));
        }
    }
    for (const auto& entry : entries) {
        if (entry.isDir) continue;
        const QString relative = QDir::fromNativeSeparators(entry.filePath);
        if (!safe_archive_path(relative)) {
            return std::unexpected(make_error(
                std::errc::invalid_argument,
                "support package ZIP contains an unsafe path"));
        }
        auto data = reader.fileData(entry.filePath);
        if (data.isEmpty() && entry.size > 0) {
            QString raw_name = entry.filePath;
            raw_name.replace(QLatin1Char('/'), QLatin1Char('\\'));
            data = reader.fileData(raw_name);
        }
        if (data.isEmpty() && entry.size > 0) {
            return std::unexpected(make_error(
                std::errc::io_error, "support package ZIP entry cannot be read"));
        }
        const auto target = destination
            / std::filesystem::path{QDir::toNativeSeparators(relative).toStdWString()};
        std::error_code directory_error;
        std::filesystem::create_directories(target.parent_path(), directory_error);
        if (directory_error) {
            return std::unexpected(make_error(
                directory_error, "cannot create extracted support package directory"));
        }
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output) {
            return std::unexpected(make_error(
                std::errc::io_error, "cannot write extracted support package file"));
        }
        output.write(data.constData(), data.size());
        if (!output) {
            return std::unexpected(make_error(
                std::errc::io_error, "cannot write extracted support package file"));
        }
    }
    reader.close();

    std::vector<std::filesystem::path> manifests;
    for (std::filesystem::recursive_directory_iterator iterator{destination, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file(error) && !error
            && iterator->path().filename() == L"manifest.toml") {
            manifests.push_back(iterator->path());
        }
    }
    if (error) {
        return std::unexpected(make_error(error, "cannot inspect extracted support package"));
    }
    if (manifests.size() != 1) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "support package ZIP must contain exactly one manifest.toml"));
    }
    return load_package_manifest(manifests.front());
}

}  // namespace gsave::gui
