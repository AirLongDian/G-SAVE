#include "gsave/repository/lua_metadata.hpp"

#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace gsave::repository {
namespace {

[[nodiscard]] std::filesystem::path environment_path(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
    if (written == 0 || written >= size) return {};
    value.resize(written);
    return value;
}

[[nodiscard]] std::size_t occurrences(
    const std::string_view text,
    const std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t position = 0;
         (position = text.find(needle, position)) != std::string_view::npos;
         position += needle.size()) {
        ++count;
    }
    return count;
}

[[nodiscard]] std::filesystem::path parser_path(const wchar_t* package) {
    return std::filesystem::path{GSAVE_SOURCE_DIR}
        / L"packages" / package / L"adapter.lua";
}

TEST(RealPackageAdapters, DragonsDogmaDarkArisenMetadata) {
    const auto repository = environment_path(L"GSAVE_DDDA_SAMPLE_DIR");
    if (!std::filesystem::is_directory(repository)) {
        GTEST_SKIP() << "set GSAVE_DDDA_SAMPLE_DIR to the copied Steam remote directory";
    }
    const auto metadata = parse_metadata({
        .repository = repository,
        .parser = parser_path(L"dragons-dogma-dark-arisen"),
        .changed_files = {"DDDA.sav"},
    });
    ASSERT_TRUE(metadata) << metadata.error().message();
    EXPECT_NE(metadata->find("\"game_id\":\"dragons-dogma-dark-arisen\""),
              std::string::npos);
    EXPECT_NE(metadata->find("\"label\":\"当前进度\""), std::string::npos);
    EXPECT_NE(metadata->find("\"occupied\":true"), std::string::npos);
    EXPECT_NE(metadata->find("\"file_size\":524288"), std::string::npos);
}

TEST(RealPackageAdapters, DragonsDogma2Metadata) {
    const auto repository = environment_path(L"GSAVE_DD2_SAMPLE_DIR");
    if (!std::filesystem::is_directory(repository)) {
        GTEST_SKIP() << "set GSAVE_DD2_SAMPLE_DIR to the copied win64_save directory";
    }
    const auto metadata = parse_metadata({
        .repository = repository,
        .parser = parser_path(L"dragons-dogma-2"),
        .changed_files = {"data000.bin", "data001Slot.bin"},
    });
    ASSERT_TRUE(metadata) << metadata.error().message();
    EXPECT_NE(metadata->find("\"game_id\":\"dragons-dogma-2\""),
              std::string::npos);
    EXPECT_NE(metadata->find("\"label\":\"最近存档\""), std::string::npos);
    EXPECT_NE(metadata->find("\"label\":\"旅店存档\""), std::string::npos);
    EXPECT_EQ(occurrences(*metadata, "\"occupied\":true"), 2U);
    EXPECT_NE(metadata->find("\"file_size\":4254684"), std::string::npos);
}

TEST(RealPackageAdapters, EldenRingOriginalOrSeamlessMetadata) {
    const auto repository = environment_path(L"GSAVE_ER_SAMPLE_DIR");
    if (!std::filesystem::is_directory(repository)) {
        GTEST_SKIP() << "set GSAVE_ER_SAMPLE_DIR to the copied EldenRing save root";
    }
    const auto metadata = parse_metadata({
        .repository = repository,
        .parser = parser_path(L"elden-ring"),
        .changed_files = {"76561198000000000/ER0000.co2"},
    });
    ASSERT_TRUE(metadata) << metadata.error().message();
    EXPECT_NE(metadata->find("\"game_id\":\"elden-ring\""), std::string::npos);
    EXPECT_NE(metadata->find("\"save_kind\":\"seamless_coop\""),
              std::string::npos);
    EXPECT_NE(metadata->find("\"valid\":true"), std::string::npos);
    EXPECT_EQ(occurrences(*metadata, "\"occupied\":true"), 3U);
    EXPECT_EQ(occurrences(*metadata, "\"index\":"), 10U);
    EXPECT_EQ(metadata->find("profile_checksum_mismatch"), std::string::npos);
}

}  // namespace
}  // namespace gsave::repository
