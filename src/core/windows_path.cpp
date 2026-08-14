#include "gsave/core/types.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace gsave::core {
namespace {

enum class WindowsRootKind {
    relative,
    drive_relative,
    drive_absolute,
    rooted,
    unc,
};

[[nodiscard]] constexpr bool is_windows_separator(const char character) noexcept {
    return character == '\\' || character == '/';
}

[[nodiscard]] constexpr bool has_windows_drive_prefix(const std::string_view value) noexcept {
    return value.size() >= 2
        && ((value[0] >= 'A' && value[0] <= 'Z')
            || (value[0] >= 'a' && value[0] <= 'z'))
        && value[1] == ':';
}

[[nodiscard]] std::size_t skip_windows_separators(
    const std::string_view value,
    std::size_t offset) noexcept {
    while (offset < value.size() && is_windows_separator(value[offset])) {
        ++offset;
    }
    return offset;
}

[[nodiscard]] std::string normalized_windows_path_text(const std::string_view source) {
    if (source.empty()) {
        return {};
    }

    auto root_kind = WindowsRootKind::relative;
    std::string prefix;
    std::size_t offset = 0;
    std::size_t protected_components = 0;
    bool anchored = false;

    if (source.size() >= 2
        && is_windows_separator(source[0])
        && is_windows_separator(source[1])) {
        root_kind = WindowsRootKind::unc;
        prefix = R"(\\)";
        offset = skip_windows_separators(source, 2);
        protected_components = 2;  // The server and share form the UNC root.
        anchored = true;
    } else if (has_windows_drive_prefix(source)) {
        prefix.assign(source.substr(0, 2));
        offset = 2;
        if (offset < source.size() && is_windows_separator(source[offset])) {
            root_kind = WindowsRootKind::drive_absolute;
            prefix.push_back('\\');
            offset = skip_windows_separators(source, offset);
            anchored = true;
        } else {
            root_kind = WindowsRootKind::drive_relative;
        }
    } else if (is_windows_separator(source.front())) {
        root_kind = WindowsRootKind::rooted;
        prefix.push_back('\\');
        offset = skip_windows_separators(source, 0);
        anchored = true;
    }

    std::vector<std::string> components;
    while (offset < source.size()) {
        const auto component_begin = offset;
        while (offset < source.size() && !is_windows_separator(source[offset])) {
            ++offset;
        }
        const auto component = source.substr(component_begin, offset - component_begin);
        offset = skip_windows_separators(source, offset);

        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            if (components.size() > protected_components && components.back() != "..") {
                components.pop_back();
            } else if (!anchored) {
                components.emplace_back(component);
            }
            continue;
        }
        components.emplace_back(component);
    }

    std::string result = std::move(prefix);
    for (const auto& component : components) {
        const bool is_first_drive_relative_component =
            root_kind == WindowsRootKind::drive_relative && result.size() == 2;
        if (!result.empty() && result.back() != '\\' && !is_first_drive_relative_component) {
            result.push_back('\\');
        }
        result += component;
    }

    if (result.empty()) {
        return ".";
    }
    return result;
}

[[nodiscard]] std::string path_utf8_text(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path{std::u8string{first, first + value.size()}};
}

[[nodiscard]] constexpr char folded_path_character(const char value) noexcept {
    if (is_windows_separator(value)) return '/';
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] bool glob_match(
    std::string_view pattern,
    std::string_view path) noexcept {
    while (!pattern.empty()) {
        if (pattern.front() == '*') {
            const bool recursive = pattern.size() >= 2 && pattern[1] == '*';
            std::size_t stars = 1;
            while (stars < pattern.size() && pattern[stars] == '*') ++stars;
            pattern.remove_prefix(stars);
            if (pattern.empty()) {
                return recursive
                    || std::ranges::none_of(path, is_windows_separator);
            }
            for (std::size_t consumed = 0;; ++consumed) {
                if (glob_match(pattern, path.substr(consumed))) return true;
                if (consumed == path.size()) break;
                if (!recursive && is_windows_separator(path[consumed])) break;
            }
            return false;
        }
        if (path.empty()) return false;
        if (pattern.front() == '?') {
            if (is_windows_separator(path.front())) return false;
        } else if (folded_path_character(pattern.front())
                   != folded_path_character(path.front())) {
            return false;
        }
        pattern.remove_prefix(1);
        path.remove_prefix(1);
    }
    return path.empty();
}

}  // namespace

std::filesystem::path normalize_windows_path_lexically(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    return path_from_utf8(normalized_windows_path_text(path_utf8_text(path)));
}

std::string windows_path_key(const std::filesystem::path& path) {
    auto value = path_utf8_text(normalize_windows_path_lexically(path));
    std::ranges::transform(value, value.begin(), [](const char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return character;
    });
    return value;
}

bool is_windows_absolute_path(const std::filesystem::path& path) {
    const auto value = normalize_windows_path_lexically(path).string();
    if (has_windows_drive_prefix(value)) {
        return value.size() >= 3 && is_windows_separator(value[2]);
    }
    if (value.size() < 2
        || !is_windows_separator(value[0])
        || !is_windows_separator(value[1])) {
        return false;
    }

    auto offset = skip_windows_separators(value, 2);
    for (int component = 0; component < 2; ++component) {
        const auto begin = offset;
        while (offset < value.size() && !is_windows_separator(value[offset])) {
            ++offset;
        }
        if (begin == offset) {
            return false;
        }
        offset = skip_windows_separators(value, offset);
    }
    return true;
}

bool path_glob_matches(
    const std::string_view pattern,
    const std::string_view relative_path) noexcept {
    return !pattern.empty() && glob_match(pattern, relative_path);
}

}  // namespace gsave::core
