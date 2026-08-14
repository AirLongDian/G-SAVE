#include "gsave/gui/package_archive.hpp"

#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>
#include <gtest/gtest.h>

#include <filesystem>

namespace gsave::gui {
namespace {

TEST(PackageArchive, ExtractsOneValidatedPackageFromZip) {
    QTemporaryDir layout;
    ASSERT_TRUE(layout.isValid());
    const auto archive = std::filesystem::path{layout.path().toStdWString()} / L"game.zip";
    const auto extracted = std::filesystem::path{layout.path().toStdWString()} / L"out";
    std::filesystem::create_directory(extracted);
    QZipWriter writer{QString::fromStdWString(archive.wstring())};
    writer.addFile(QStringLiteral("game/manifest.toml"), QByteArray{
        "package_api=1\nid='zip-game'\nname='ZIP Game'\nversion='1'\n"
        "adapter='adapter.lua'\n[game]\nprocess_name='game.exe'\n"});
    writer.addFile(QStringLiteral("game/adapter.lua"), QByteArray{
        "function install() return {} end\nfunction parse() return {} end\n"});
    writer.close();
    ASSERT_EQ(writer.status(), QZipWriter::NoError);

    auto package = extract_package_archive(archive, extracted);

    ASSERT_TRUE(package) << package.error().message();
    EXPECT_EQ(package->id, "zip-game");
    EXPECT_EQ(package->name, "ZIP Game");
    EXPECT_TRUE(std::filesystem::is_regular_file(package->adapter));
}

TEST(PackageArchive, RejectsSymbolicLinksAndDoesNotWriteOutsideDestination) {
    QTemporaryDir layout;
    ASSERT_TRUE(layout.isValid());
    const auto root = std::filesystem::path{layout.path().toStdWString()};
    const auto archive = root / L"unsafe.zip";
    const auto extracted = root / L"out";
    std::filesystem::create_directory(extracted);
    QZipWriter writer{QString::fromStdWString(archive.wstring())};
    writer.addSymLink(QStringLiteral("escaped.txt"), QStringLiteral("../escaped.txt"));
    writer.addFile(QStringLiteral("manifest.toml"), QByteArray{
        "package_api=1\nid='unsafe'\nname='Unsafe'\nversion='1'\n"
        "adapter='adapter.lua'\n[game]\nprocess_name='game.exe'\n"});
    writer.addFile(QStringLiteral("adapter.lua"), QByteArray{"function parse() return {} end"});
    writer.close();

    auto package = extract_package_archive(archive, extracted);

    EXPECT_FALSE(package);
    EXPECT_FALSE(std::filesystem::exists(root / L"escaped.txt"));
}


TEST(PackageArchive, ExtractsZipWithBackslashSeparators) {
    QTemporaryDir layout;
    ASSERT_TRUE(layout.isValid());
    const auto archive = std::filesystem::path{layout.path().toStdWString()} / L"backslash.zip";
    const auto extracted = std::filesystem::path{layout.path().toStdWString()} / L"out";
    std::filesystem::create_directory(extracted);
    QZipWriter writer{QString::fromStdWString(archive.wstring())};
    writer.addFile(QStringLiteral("game\\manifest.toml"), QByteArray{
        "package_api=1\nid='backslash-game'\nname='Backslash Game'\nversion='1'\n"
        "adapter='adapter.lua'\n[game]\nprocess_name='game.exe'\n"});
    writer.addFile(QStringLiteral("game\\adapter.lua"), QByteArray{
        "function install() return {} end\nfunction parse() return {} end\n"});
    writer.close();
    ASSERT_EQ(writer.status(), QZipWriter::NoError);

    auto package = extract_package_archive(archive, extracted);

    ASSERT_TRUE(package) << package.error().message();
    EXPECT_EQ(package->id, "backslash-game");
    EXPECT_TRUE(std::filesystem::is_regular_file(package->adapter));
}

TEST(PackageArchive, ExtractsConfiguredDistributedPackageZip) {
    const auto configured = qEnvironmentVariable("GSAVE_TEST_PACKAGE_ZIP");
    if (configured.isEmpty()) {
        GTEST_SKIP() << "GSAVE_TEST_PACKAGE_ZIP is not set";
    }
    QTemporaryDir layout;
    ASSERT_TRUE(layout.isValid());
    const auto archive = std::filesystem::path{configured.toStdWString()};
    const auto extracted = std::filesystem::path{layout.path().toStdWString()} / L"out";
    std::filesystem::create_directory(extracted);
    ASSERT_TRUE(std::filesystem::is_regular_file(archive)) << archive;
    auto package = extract_package_archive(archive, extracted);
    ASSERT_TRUE(package) << package.error().message();
    const auto expected_id_u8 = archive.stem().u8string();
    const std::string expected_id{
        reinterpret_cast<const char*>(expected_id_u8.data()), expected_id_u8.size()};
    EXPECT_EQ(package->id, expected_id);
    EXPECT_FALSE(package->name.empty());
    EXPECT_TRUE(std::filesystem::is_regular_file(package->adapter));
    EXPECT_GT(std::filesystem::file_size(package->adapter), 0U);
}

}  // namespace
}  // namespace gsave::gui
