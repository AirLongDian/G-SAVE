#include <gtest/gtest.h>

#include <toml++/toml.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

struct ExpectedPackage final {
    std::string_view directory;
    std::string_view id;
    std::string_view process_name;
    std::int64_t steam_app_id;
    std::string_view repository_scope;
};

constexpr std::array packages{
    ExpectedPackage{
        "dark-souls-iii", "dark-souls-iii", "DarkSoulsIII.exe", 374320,
        "save_root"},
    ExpectedPackage{
        "dragons-dogma-dark-arisen", "dragons-dogma-dark-arisen", "DDDA.exe", 367500,
        "steam_account_remote"},
    ExpectedPackage{
        "dragons-dogma-2", "dragons-dogma-2", "DD2.exe", 2054970,
        "steam_account_win64_save"},
    ExpectedPackage{
        "elden-ring", "elden-ring", "eldenring.exe", 1245620,
        "save_root"},
};

[[nodiscard]] std::filesystem::path source_path(const std::string_view relative) {
    return std::filesystem::path{GSAVE_SOURCE_DIR} / relative;
}

TEST(PackageManifest, ApiOnePackagesExposeRequiredInstallationContract) {
    for (const auto& expected : packages) {
        SCOPED_TRACE(expected.id);
        const auto root = source_path("packages") / expected.directory;
        const auto manifest_path = root / "manifest.toml";
        ASSERT_TRUE(std::filesystem::is_regular_file(manifest_path));
        ASSERT_TRUE(std::filesystem::is_regular_file(root / "adapter.lua"));

        const auto document = toml::parse_file(manifest_path.string());
        EXPECT_EQ(document["package_api"].value<std::int64_t>(), 1);
        EXPECT_EQ(document["id"].value<std::string>(), expected.id);
        EXPECT_EQ(document["adapter"].value<std::string>(), "adapter.lua");

        const auto* game = document["game"].as_table();
        ASSERT_NE(game, nullptr);
        EXPECT_EQ((*game)["steam_app_id"].value<std::int64_t>(), expected.steam_app_id);
        EXPECT_EQ((*game)["process_name"].value<std::string>(), expected.process_name);

        const auto* save = document["save"].as_table();
        ASSERT_NE(save, nullptr);
        EXPECT_EQ(
            (*save)["repository_scope"].value<std::string>(), expected.repository_scope);

        const auto* watch = document["watch"].as_table();
        ASSERT_NE(watch, nullptr);
        const auto* includes = (*watch)["include_globs"].as_array();
        const auto* excludes = (*watch)["exclude_globs"].as_array();
        ASSERT_NE(includes, nullptr);
        ASSERT_FALSE(includes->empty());
        ASSERT_NE(excludes, nullptr);
        ASSERT_FALSE(excludes->empty());

        const auto* git = document["git"].as_table();
        ASSERT_NE(git, nullptr);
        EXPECT_EQ((*git)["autocrlf"].value<bool>(), false);

        const auto* commit = document["commit"].as_table();
        ASSERT_NE(commit, nullptr);
        EXPECT_EQ((*commit)["strategy"].value<std::string>(), "hybrid");
        EXPECT_EQ((*commit)["commit_on_exit"].value<bool>(), true);
    }
}

}  // namespace
