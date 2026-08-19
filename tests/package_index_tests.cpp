#include "gsave/gui/package_index.hpp"

#include <gtest/gtest.h>

#include <QCryptographicHash>
#include <QTemporaryDir>

#include <algorithm>
#include <fstream>

namespace gsave::gui {
namespace {

[[nodiscard]] QByteArray index_document(const QByteArray& entries) {
    return "{\"index_version\":1,\"updated_at\":\"2026-08-18\",\"packages\":["
        + entries + "]}";
}

[[nodiscard]] QByteArray entry(
    const QByteArray& id,
    const QByteArray& sha256,
    const QByteArray& size = "1024",
    const QByteArray& api = "1") {
    return "{\"id\":\"" + id + "\",\"name\":\"Game " + id
        + "\",\"version\":\"1.0.0\",\"package_api\":" + api
        + ",\"steam_appid\":1245620,\"process_name\":\"game.exe\""
        + ",\"summary\":\"slots\",\"download\":\"https://example.invalid/"
        + id + ".zip\",\"sha256\":\"" + sha256 + "\",\"size\":" + size + "}";
}

constexpr auto valid_digest =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

TEST(PackageIndex, ParsesEveryUsableEntryAndKeepsDeclaredIntegrityData) {
    const auto parsed = parse_package_index(
        index_document(entry("elden-ring", valid_digest, "40960")));
    ASSERT_TRUE(parsed) << parsed.error().message();
    EXPECT_EQ(parsed->index_version, 1);
    EXPECT_EQ(parsed->updated_at, QStringLiteral("2026-08-18"));
    ASSERT_EQ(parsed->packages.size(), 1U);
    const auto& package = parsed->packages.front();
    EXPECT_EQ(package.id, QStringLiteral("elden-ring"));
    EXPECT_EQ(package.name, QStringLiteral("Game elden-ring"));
    EXPECT_EQ(package.version, QStringLiteral("1.0.0"));
    EXPECT_EQ(package.process_name, QStringLiteral("game.exe"));
    EXPECT_EQ(package.steam_app_id, 1245620);
    EXPECT_EQ(package.size, 40960);
    EXPECT_EQ(package.sha256, QString::fromLatin1(valid_digest));
    EXPECT_EQ(package.download.host(), QStringLiteral("example.invalid"));
}

TEST(PackageIndex, RejectsAnIndexVersionThisBuildCannotUnderstand) {
    const auto parsed = parse_package_index(
        "{\"index_version\":2,\"packages\":[]}");
    EXPECT_FALSE(parsed);
}

TEST(PackageIndex, RejectsMalformedDocumentsInsteadOfGuessing) {
    EXPECT_FALSE(parse_package_index("not json"));
    EXPECT_FALSE(parse_package_index("[]"));
    EXPECT_FALSE(parse_package_index("{\"index_version\":1}"));
    EXPECT_FALSE(parse_package_index("{\"index_version\":1,\"packages\":{}}"));
}

TEST(PackageIndex, DropsOnlyUnusableEntriesAndKeepsTheRestOfTheIndex) {
    // A single broken entry must never hide every other support package, and an
    // entry without trustworthy integrity data must never become installable.
    const auto parsed = parse_package_index(index_document(
        entry("good", valid_digest) + ","
        + entry("future-api", valid_digest, "1024", "2") + ","
        + entry("short-digest", "abc123") + ","
        + entry("zero-size", valid_digest, "0") + ","
        + entry("no-name", valid_digest).replace("\"name\":\"Game no-name\"", "\"name\":\"\"")
        + "," + entry("also-good", valid_digest)));
    ASSERT_TRUE(parsed) << parsed.error().message();
    ASSERT_EQ(parsed->packages.size(), 2U);
    EXPECT_EQ(parsed->packages[0].id, QStringLiteral("good"));
    EXPECT_EQ(parsed->packages[1].id, QStringLiteral("also-good"));
}

TEST(PackageIndex, NormalisesUppercaseDigestsSoComparisonStaysStable) {
    const auto parsed = parse_package_index(index_document(
        entry("upper", QByteArray{valid_digest}.toUpper())));
    ASSERT_TRUE(parsed) << parsed.error().message();
    ASSERT_EQ(parsed->packages.size(), 1U);
    EXPECT_EQ(parsed->packages.front().sha256, QString::fromLatin1(valid_digest));
}

TEST(PackageIndex, BuildsSteamPosterUrlsOnlyForKnownApplicationIds) {
    EXPECT_EQ(
        steam_poster_url(1245620).toString(),
        QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/1245620"
                       "/library_600x900.jpg"));
    EXPECT_EQ(
        steam_header_url(374320).toString(),
        QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/374320"
                       "/header.jpg"));
    EXPECT_TRUE(steam_poster_url(0).isEmpty());
    EXPECT_TRUE(steam_header_url(-1).isEmpty());
}

TEST(PackageIndex, DefaultIndexUrlUsesRawGithubOnTheMainBranch) {
    const auto url = default_package_index_url();
    EXPECT_EQ(url.scheme(), QStringLiteral("https"));
    EXPECT_EQ(url.host(), QStringLiteral("raw.githubusercontent.com"));
    EXPECT_TRUE(url.path().endsWith(QStringLiteral("/main/packages/index.json")));
}

TEST(PackageIndex, RefusesToDownloadEntriesWithUnusableIntegrityData) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto target = std::filesystem::path{directory.path().toStdWString()};

    IndexedPackage package;
    package.id = QStringLiteral("broken");
    package.name = QStringLiteral("Broken");
    package.download = QUrl{QStringLiteral("https://example.invalid/broken.zip")};
    package.size = 1024;
    package.sha256 = QStringLiteral("not-a-digest");
    EXPECT_FALSE(download_indexed_package(package, target));

    package.sha256 = QString::fromLatin1(valid_digest);
    package.size = 0;
    EXPECT_FALSE(download_indexed_package(package, target));

    // A rejected entry must not leave a partial archive behind for the importer.
    EXPECT_FALSE(std::filesystem::exists(target / L"broken.zip"));
}

TEST(PackageIndex, RefusesUnsupportedDownloadSchemes) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    IndexedPackage package;
    package.id = QStringLiteral("local");
    package.name = QStringLiteral("Local");
    package.sha256 = QString::fromLatin1(valid_digest);
    package.size = 16;
    package.download = QUrl{QStringLiteral("file:///C:/evil.zip")};
    EXPECT_FALSE(download_indexed_package(
        package, std::filesystem::path{directory.path().toStdWString()}));
}

TEST(PackageIndex, FetchReportsNetworkFailureWithoutCrashingTheLibraryPage) {
    // Reaching the index is optional: the caller keeps showing installed
    // packages, so this must fail as a normal error rather than abort.
    const auto fetched = fetch_package_index(
        QUrl{QStringLiteral("https://index.invalid.gsave.test/index.json")});
    ASSERT_FALSE(fetched);
    EXPECT_FALSE(fetched.error().message().empty());
}

// Exercises the real download path against a stable public asset so the size
// and digest verification is proven end to end, not just on rejected inputs.
// Skipped when the machine has no usable network.
TEST(PackageIndex, VerifiesRealDownloadsAndRejectsADigestMismatch) {    constexpr auto asset =
        "https://cdn.cloudflare.steamstatic.com/steam/apps/1245620/header.jpg";
    constexpr auto asset_digest =
        "d46402528e357350bdb31a00be57a7896238515e6dc8afc0669aa54573307c02";
    constexpr std::int64_t asset_size = 32689;

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto target = std::filesystem::path{directory.path().toStdWString()};

    IndexedPackage package;
    package.id = QStringLiteral("real-asset");
    package.name = QStringLiteral("Real Asset");
    package.download = QUrl{QString::fromLatin1(asset)};
    package.size = asset_size;
    package.sha256 = QString::fromLatin1(asset_digest);

    auto downloaded = download_indexed_package(package, target);
    if (!downloaded
        && downloaded.error().code == std::make_error_code(
            std::errc::network_unreachable)) {
        GTEST_SKIP() << "network is unavailable: " << downloaded.error().message();
    }
    ASSERT_TRUE(downloaded) << downloaded.error().message();
    EXPECT_TRUE(std::filesystem::is_regular_file(*downloaded));
    EXPECT_EQ(std::filesystem::file_size(*downloaded), asset_size);

    // The same bytes with a wrong digest must be refused and must not be stored.
    package.id = QStringLiteral("tampered");
    package.sha256 = QString::fromLatin1(
        "0000000000000000000000000000000000000000000000000000000000000000");
    auto tampered = download_indexed_package(package, target);
    ASSERT_FALSE(tampered);
    EXPECT_FALSE(std::filesystem::exists(target / L"tampered.zip"));

    // A wrong declared size must be refused before the digest is even compared.
    package.id = QStringLiteral("wrong-size");
    package.sha256 = QString::fromLatin1(asset_digest);
    package.size = asset_size + 1;
    auto resized = download_indexed_package(package, target);
    ASSERT_FALSE(resized);
    EXPECT_FALSE(std::filesystem::exists(target / L"wrong-size.zip"));
}

// The shipped index is what every player fetches, so a typo there is a product
// bug. This keeps it parseable and consistent with the bundled sample packages.
TEST(PackageIndex, ShippedIndexParsesAndMatchesTheBundledSamplePackages) {
    const auto index_path =
        std::filesystem::path{GSAVE_SOURCE_DIR} / "packages" / "index.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(index_path));
    std::ifstream input{index_path, std::ios::binary};
    ASSERT_TRUE(input);
    const std::string document{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};

    auto parsed = parse_package_index(QByteArray::fromStdString(document));
    ASSERT_TRUE(parsed) << parsed.error().message();
    EXPECT_EQ(parsed->packages.size(), 4U);

    const std::vector<std::pair<QString, std::int64_t>> expected{
        {QStringLiteral("dark-souls-iii"), 374320},
        {QStringLiteral("elden-ring"), 1245620},
        {QStringLiteral("dragons-dogma-dark-arisen"), 367500},
        {QStringLiteral("dragons-dogma-2"), 2054970},
    };
    for (const auto& [id, app_id] : expected) {
        SCOPED_TRACE(id.toStdString());
        const auto found = std::ranges::find_if(
            parsed->packages, [&](const auto& package) { return package.id == id; });
        ASSERT_NE(found, parsed->packages.end());
        EXPECT_EQ(found->steam_app_id, app_id);
        EXPECT_EQ(found->download.scheme(), QStringLiteral("https"));
        EXPECT_EQ(found->download.host(), QStringLiteral("github.com"));
        EXPECT_TRUE(found->download.path().endsWith(id + QStringLiteral(".zip")));
        EXPECT_EQ(found->sha256.size(), 64);
        EXPECT_GT(found->size, 0);
        // The bundled package directory must exist for every advertised entry.
        EXPECT_TRUE(std::filesystem::is_regular_file(
            std::filesystem::path{GSAVE_SOURCE_DIR} / "packages"
            / id.toStdString() / "manifest.toml"));
    }
}

}  // namespace
}  // namespace gsave::gui
