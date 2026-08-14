#pragma once

#include <gsave/base/error.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace gsave::platform {

using RepositoryKey = std::uint64_t;

enum class DirectoryEventKind {
    added,
    removed,
    modified,
    renamed_from,
    renamed_to,
    overflow,
};

struct DirectoryEvent {
    RepositoryKey repository{};
    DirectoryEventKind kind{};
    std::filesystem::path relative_path;
};

struct DirectoryWatchRequest {
    RepositoryKey repository{};
    std::filesystem::path root;
    bool recursive{true};
};

// Windows paths are case-insensitive. Avoid std::filesystem component parsing
// here so this filter can also be contract-tested on non-Windows hosts.
[[nodiscard]] constexpr bool is_git_metadata_path(
    const std::wstring_view relative_path) noexcept {
    std::size_t component_begin = 0;
    while (component_begin <= relative_path.size()) {
        auto component_end = component_begin;
        while (component_end < relative_path.size()
               && relative_path[component_end] != L'\\'
               && relative_path[component_end] != L'/') {
            ++component_end;
        }

        const auto component = relative_path.substr(
            component_begin, component_end - component_begin);
        if (component.size() == 4 && component[0] == L'.'
            && (component[1] == L'g' || component[1] == L'G')
            && (component[2] == L'i' || component[2] == L'I')
            && (component[3] == L't' || component[3] == L'T')) {
            return true;
        }

        if (component_end == relative_path.size()) {
            break;
        }
        component_begin = component_end + 1;
    }
    return false;
}

class DirectoryWatcher {
public:
    DirectoryWatcher() = default;
    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;
    DirectoryWatcher(DirectoryWatcher&&) = delete;
    DirectoryWatcher& operator=(DirectoryWatcher&&) = delete;
    virtual ~DirectoryWatcher() = default;

    virtual std::expected<void, Error> add(
        const DirectoryWatchRequest& request) = 0;
    virtual std::expected<void, Error> remove(RepositoryKey repository) = 0;

    // One Core event thread calls poll(). A timeout is a successful empty
    // result, not an error.
    [[nodiscard]] virtual std::expected<std::optional<DirectoryEvent>, Error>
    poll(std::chrono::milliseconds timeout) = 0;

    // Interrupts a blocking poll without manufacturing a filesystem event.
    // This is used after a WMI callback enqueues a process event.
    virtual Status wake() = 0;

    // stop() is idempotent and drains cancelled OVERLAPPED operations before
    // releasing their storage.
    virtual void stop() noexcept = 0;
};

[[nodiscard]] std::expected<std::unique_ptr<DirectoryWatcher>, Error>
make_iocp_directory_watcher();

} // namespace gsave::platform
