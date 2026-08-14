#include <gsave/platform/directory_watcher.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <optional>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gsave::platform {
namespace {

constexpr DWORD notification_filter = FILE_NOTIFY_CHANGE_FILE_NAME
    | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE
    | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
constexpr std::size_t notification_buffer_size = 64U * 1024U;
constexpr ULONG_PTR wake_completion_key =
    (std::numeric_limits<ULONG_PTR>::max)();
constexpr ULONG_PTR stop_completion_key = wake_completion_key - 1;

[[nodiscard]] Error make_windows_error(
    const DWORD code, std::string context) {
    return make_error(
        std::error_code{static_cast<int>(code), std::system_category()},
        std::move(context));
}

[[nodiscard]] DirectoryEventKind event_kind_from_action(
    const DWORD action) noexcept {
    switch (action) {
    case FILE_ACTION_ADDED:
        return DirectoryEventKind::added;
    case FILE_ACTION_REMOVED:
        return DirectoryEventKind::removed;
    case FILE_ACTION_RENAMED_OLD_NAME:
        return DirectoryEventKind::renamed_from;
    case FILE_ACTION_RENAMED_NEW_NAME:
        return DirectoryEventKind::renamed_to;
    case FILE_ACTION_MODIFIED:
    default:
        return DirectoryEventKind::modified;
    }
}

class IocpDirectoryWatcher final : public DirectoryWatcher {
public:
    explicit IocpDirectoryWatcher(HANDLE completion_port) noexcept
        : completion_port_(completion_port) {}

    ~IocpDirectoryWatcher() override { stop(); }

    std::expected<void, Error> add(
        const DirectoryWatchRequest& request) override {
        if (completion_port_ == nullptr) {
            return std::unexpected(make_error(
                std::errc::operation_canceled,
                "directory watcher has been stopped"));
        }
        if (request.root.empty()) {
            return std::unexpected(make_error(
                std::errc::invalid_argument,
                "directory watch root is empty"));
        }
        if (watches_.contains(request.repository)) {
            return std::unexpected(make_error(
                std::errc::file_exists,
                "repository key is already watched"));
        }
        if (request.repository
            >= static_cast<RepositoryKey>(stop_completion_key)) {
            return std::unexpected(make_error(
                std::errc::value_too_large,
                "repository key collides with a reserved IOCP key"));
        }

        const auto directory = CreateFileW(
            request.root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (directory == INVALID_HANDLE_VALUE) {
            return std::unexpected(make_windows_error(
                GetLastError(), "opening directory watch root failed"));
        }

        auto watch = std::make_unique<Watch>();
        watch->repository = request.repository;
        watch->directory = directory;
        watch->recursive = request.recursive;

        const auto associated_port = CreateIoCompletionPort(
            directory, completion_port_,
            static_cast<ULONG_PTR>(request.repository), 0);
        if (associated_port == nullptr) {
            const auto error = GetLastError();
            CloseHandle(directory);
            return std::unexpected(make_windows_error(
                error, "associating directory with IOCP failed"));
        }

        if (const auto armed = arm(*watch); !armed) {
            const auto error = armed.error();
            return std::unexpected(error);
        }

        watches_.emplace(request.repository, std::move(watch));
        return {};
    }

    std::expected<void, Error> remove(
        const RepositoryKey repository) override {
        const auto position = watches_.find(repository);
        if (position == watches_.end()) {
            return {};
        }

        auto& watch = *position->second;
        watch.active = false;
        std::optional<Error> cancellation_error;
        if (watch.pending
            && CancelIoEx(watch.directory, &watch.overlapped) == FALSE) {
            const auto error = GetLastError();
            if (error != ERROR_NOT_FOUND) {
                cancellation_error = make_windows_error(
                    error, "cancelling directory notification failed");
            }
        }

        // Closing the directory guarantees that no further operation can be
        // armed for this Watch. It also requests cancellation if CancelIoEx
        // raced a completion. The Watch itself remains allocated until IOCP
        // yields the packet which contains &watch.overlapped.
        if (watch.directory != INVALID_HANDLE_VALUE) {
            if (CloseHandle(watch.directory) == FALSE) {
                if (!cancellation_error.has_value()) {
                    cancellation_error = make_windows_error(
                        GetLastError(),
                        "closing directory watch handle failed");
                }
            } else {
                watch.directory = INVALID_HANDLE_VALUE;
            }
        }

        if (!watch.pending) {
            watches_.erase(position);
        }

        // OVERLAPPED storage must remain alive until its completion packet has
        // been removed from the port. Other repositories remain armed while we
        // drain this one cancellation.
        while (watches_.contains(repository)) {
            DWORD bytes = 0;
            ULONG_PTR completion_key = 0;
            OVERLAPPED* overlapped = nullptr;
            const auto succeeded = GetQueuedCompletionStatus(
                completion_port_, &bytes, &completion_key, &overlapped,
                INFINITE);
            const auto completion_error =
                succeeded != FALSE ? ERROR_SUCCESS : GetLastError();
            if (overlapped == nullptr) {
                if (succeeded != FALSE
                    && (completion_key == wake_completion_key
                        || completion_key == stop_completion_key)) {
                    continue;
                }
                return std::unexpected(make_windows_error(
                    completion_error,
                    "waiting for cancelled directory notification failed"));
            }
            handle_completion(
                static_cast<RepositoryKey>(completion_key), overlapped, bytes,
                completion_error);
        }
        if (cancellation_error.has_value()) {
            return std::unexpected(std::move(*cancellation_error));
        }
        return {};
    }

    std::expected<std::optional<DirectoryEvent>, Error> poll(
        const std::chrono::milliseconds timeout) override {
        if (!errors_.empty()) {
            auto error = std::move(errors_.front());
            errors_.pop_front();
            return std::unexpected(std::move(error));
        }
        if (!events_.empty()) {
            auto event = std::move(events_.front());
            events_.pop_front();
            return std::optional<DirectoryEvent>{std::move(event)};
        }
        if (completion_port_ == nullptr) {
            return std::unexpected(make_error(
                std::errc::operation_canceled,
                "directory watcher has been stopped"));
        }

        const std::chrono::milliseconds::rep timeout_count = timeout.count();
        using TimeoutRep = std::remove_cv_t<decltype(timeout_count)>;
        const auto wait_milliseconds = timeout_count <= 0
            ? DWORD{0}
            : timeout_count >= static_cast<TimeoutRep>(INFINITE - 1)
            ? INFINITE - 1
            : static_cast<DWORD>(timeout_count);

        DWORD bytes = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED* overlapped = nullptr;
        const auto succeeded = GetQueuedCompletionStatus(
            completion_port_, &bytes, &completion_key, &overlapped,
            wait_milliseconds);
        const auto completion_error =
            succeeded != FALSE ? ERROR_SUCCESS : GetLastError();

        if (overlapped == nullptr) {
            if (succeeded != FALSE && completion_key == wake_completion_key) {
                return std::optional<DirectoryEvent>{};
            }
            if (succeeded != FALSE && completion_key == stop_completion_key) {
                return std::unexpected(make_error(
                    std::errc::operation_canceled,
                    "directory watcher has been stopped"));
            }
            if (completion_error == WAIT_TIMEOUT) {
                return std::optional<DirectoryEvent>{};
            }
            return std::unexpected(make_windows_error(
                completion_error, "waiting for directory notification failed"));
        }

        handle_completion(
            static_cast<RepositoryKey>(completion_key), overlapped, bytes,
            completion_error);
        if (!errors_.empty()) {
            auto error = std::move(errors_.front());
            errors_.pop_front();
            return std::unexpected(std::move(error));
        }
        if (events_.empty()) {
            return std::optional<DirectoryEvent>{};
        }

        auto event = std::move(events_.front());
        events_.pop_front();
        return std::optional<DirectoryEvent>{std::move(event)};
    }

    Status wake() override {
        if (completion_port_ == nullptr) {
            return std::unexpected(make_error(
                std::errc::operation_canceled,
                "directory watcher has been stopped"));
        }
        if (PostQueuedCompletionStatus(
                completion_port_, 0, wake_completion_key, nullptr)
            == FALSE) {
            return std::unexpected(make_windows_error(
                GetLastError(), "posting directory watcher wake packet failed"));
        }
        return {};
    }

    void stop() noexcept override {
        if (completion_port_ == nullptr) {
            return;
        }

        static_cast<void>(PostQueuedCompletionStatus(
            completion_port_, 0, stop_completion_key, nullptr));

        for (auto& [repository, watch] : watches_) {
            static_cast<void>(repository);
            watch->active = false;
            if (watch->pending) {
                static_cast<void>(CancelIoEx(
                    watch->directory, &watch->overlapped));
            }
            if (watch->directory != INVALID_HANDLE_VALUE) {
                if (CloseHandle(watch->directory) != FALSE) {
                    watch->directory = INVALID_HANDLE_VALUE;
                }
            }
        }

        for (auto position = watches_.begin(); position != watches_.end();) {
            if (!position->second->pending) {
                position = watches_.erase(position);
            } else {
                ++position;
            }
        }

        while (!watches_.empty()) {
            DWORD bytes = 0;
            ULONG_PTR completion_key = 0;
            OVERLAPPED* overlapped = nullptr;
            const auto succeeded = GetQueuedCompletionStatus(
                completion_port_, &bytes, &completion_key, &overlapped,
                INFINITE);
            if (overlapped == nullptr) {
                if (succeeded != FALSE
                    && (completion_key == wake_completion_key
                        || completion_key == stop_completion_key)) {
                    continue;
                }
                // With an infinite wait on an open IOCP, cancelled operations
                // complete with a non-null OVERLAPPED. Do not free any Watch
                // storage on an unexpected null result: a later packet can
                // still carry its address.
                continue;
            }
            const auto completion_error =
                succeeded != FALSE ? ERROR_SUCCESS : GetLastError();
            handle_completion(
                static_cast<RepositoryKey>(completion_key), overlapped, bytes,
                completion_error);
        }

        CloseHandle(completion_port_);
        completion_port_ = nullptr;
        events_.clear();
        errors_.clear();
    }

private:
    struct Watch {
        RepositoryKey repository{};
        HANDLE directory{INVALID_HANDLE_VALUE};
        bool recursive{true};
        bool active{true};
        bool pending{};
        OVERLAPPED overlapped{};
        std::vector<std::byte> buffer =
            std::vector<std::byte>(notification_buffer_size);

        ~Watch() {
            if (directory != INVALID_HANDLE_VALUE) {
                CloseHandle(directory);
            }
        }
    };

    [[nodiscard]] std::expected<void, Error> arm(Watch& watch) {
        watch.overlapped = {};
        if (ReadDirectoryChangesW(
                watch.directory, watch.buffer.data(),
                static_cast<DWORD>(watch.buffer.size()), watch.recursive,
                notification_filter, nullptr, &watch.overlapped, nullptr)
            == FALSE) {
            return std::unexpected(make_windows_error(
                GetLastError(), "arming directory notification failed"));
        }
        watch.pending = true;
        return {};
    }

    void handle_completion(
        const RepositoryKey repository, OVERLAPPED* const overlapped,
        const DWORD bytes, const DWORD completion_error) {
        const auto position = watches_.find(repository);
        if (position == watches_.end()
            || overlapped != &position->second->overlapped) {
            errors_.push_back(make_error(
                std::errc::state_not_recoverable,
                "IOCP returned an unknown directory completion"));
            return;
        }

        auto& watch = *position->second;
        watch.pending = false;
        if (!watch.active) {
            watches_.erase(position);
            return;
        }

        if (completion_error == ERROR_NOTIFY_ENUM_DIR || bytes == 0) {
            events_.push_back(DirectoryEvent{
                repository, DirectoryEventKind::overflow, {}});
        } else if (completion_error != ERROR_SUCCESS) {
            errors_.push_back(make_windows_error(
                completion_error, "directory notification completed with error"));
            watch.active = false;
            watches_.erase(position);
            return;
        } else {
            parse_notifications(watch, bytes);
        }

        if (const auto armed = arm(watch); !armed) {
            errors_.push_back(armed.error());
            watches_.erase(position);
        }
    }

    void parse_notifications(const Watch& watch, const DWORD bytes) {
        std::size_t offset = 0;
        bool malformed = false;
        constexpr auto fixed_size = offsetof(FILE_NOTIFY_INFORMATION, FileName);

        while (offset + fixed_size <= bytes) {
            const auto* information =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    watch.buffer.data() + offset);
            const auto record_size = fixed_size + information->FileNameLength;
            if (information->FileNameLength % sizeof(wchar_t) != 0
                || record_size > bytes - offset) {
                malformed = true;
                break;
            }

            const std::wstring_view relative_path{
                information->FileName,
                information->FileNameLength / sizeof(wchar_t)};
            if (!is_git_metadata_path(relative_path)) {
                events_.push_back(DirectoryEvent{
                    watch.repository,
                    event_kind_from_action(information->Action),
                    std::filesystem::path{relative_path}});
            }

            if (information->NextEntryOffset == 0) {
                offset = bytes;
                break;
            }
            if (information->NextEntryOffset < fixed_size
                || information->NextEntryOffset > bytes - offset) {
                malformed = true;
                break;
            }
            offset += information->NextEntryOffset;
        }

        if (malformed || offset != bytes) {
            events_.push_back(DirectoryEvent{
                watch.repository, DirectoryEventKind::overflow, {}});
        }
    }

    HANDLE completion_port_{nullptr};
    std::unordered_map<RepositoryKey, std::unique_ptr<Watch>> watches_;
    std::deque<DirectoryEvent> events_;
    std::deque<Error> errors_;
};

} // namespace

std::expected<std::unique_ptr<DirectoryWatcher>, Error>
make_iocp_directory_watcher() {
    const auto completion_port = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (completion_port == nullptr) {
        return std::unexpected(make_windows_error(
            GetLastError(), "creating directory IOCP failed"));
    }
    return std::make_unique<IocpDirectoryWatcher>(completion_port);
}

} // namespace gsave::platform
