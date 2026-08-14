#pragma once

#include "gsave/base/error.hpp"
#include "gsave/base/unique_handle.hpp"
#include "gsave/config/config.hpp"
#include "gsave/core/types.hpp"
#include "gsave/platform/directory_watcher.hpp"
#include "gsave/platform/process_event_source.hpp"
#include "gsave/repository/repository_engine.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gsave::core {

using CommitCallback = Result<repository::CommitOutcome> (*)(
    void*, const repository::CommitOptions&);
using PushCallback = Status (*)(void*, const repository::PushOptions&);
using NetworkWarningCallback = void (*)(void*, const Error&);

struct CoreAppOptions final {
    config::Config config;
    std::filesystem::path config_directory;
    void* repository_context{};
    CommitCallback commit_callback{};
    PushCallback push_callback{};
    NetworkWarningCallback network_warning_callback{};
};

// Owns the single-threaded Core state machine. Platform callbacks never run
// repository work: they only append a value event and wake the shared IOCP.
class CoreApp final {
public:
    CoreApp(
        CoreAppOptions options,
        std::unique_ptr<platform::ProcessEventSource> process_events,
        std::unique_ptr<platform::DirectoryWatcher> directory_watcher);
    ~CoreApp();

    CoreApp(const CoreApp&) = delete;
    CoreApp& operator=(const CoreApp&) = delete;
    CoreApp(CoreApp&&) = delete;
    CoreApp& operator=(CoreApp&&) = delete;

    // start(), run_iteration(), shutdown(), and destruction must occur on one
    // owner thread because the WMI source owns a COM apartment there.
    [[nodiscard]] Status start();
    [[nodiscard]] Status run();
    [[nodiscard]] Status run_iteration(std::chrono::milliseconds maximum_wait);
    // Thread-safe, data-free control signal used by the GUI's session-local
    // named event. It only wakes the owner loop; shutdown stays on that loop.
    void request_stop() noexcept;
    void shutdown() noexcept;

private:
    struct GameRuntime final {
        std::size_t config_index{};
        std::size_t active_processes{};
        std::wstring process_name;
        std::vector<platform::RepositoryKey> repositories;
    };

    struct RepositoryRuntime final {
        std::filesystem::path path;
        const config::SaveConfig* save_config{};
        std::size_t game_index{};
        platform::RepositoryKey key{};
        bool watched{};
        bool dirty{};
        TimePoint first_change{};
        TimePoint last_change{};
        bool commit_running{};
        bool commit_pending{};
        CommitReason pending_reason{CommitReason::quiet_period_elapsed};
        CommitReason running_reason{CommitReason::quiet_period_elapsed};
        std::uint8_t consecutive_commit_failures{};
        std::optional<TimePoint> commit_retry_deadline;
        bool push_running{};
        bool push_pending{};
        std::optional<Error> deferred_network_warning;
    };

    enum class TaskOperation {
        commit,
        push,
    };

    enum class TaskResult : std::uint8_t {
        pending,
        created,
        no_changes,
        worktree_unstable,
        completed,
        failed,
    };

    struct ActiveTask final {
        CoreApp* owner{};
        TaskOperation operation{TaskOperation::commit};
        platform::RepositoryKey repository{};
        std::atomic<TaskResult> result{TaskResult::pending};
        std::optional<Error> error;
        std::optional<repository::CommitOptions> commit;
        std::optional<repository::PushOptions> push;
        base::unique_handle thread;
    };

    static unsigned __stdcall execute_task(void* context) noexcept;

    [[nodiscard]] Status validate_and_build_runtime();
    [[nodiscard]] Status handle_process_event(
        const platform::ProcessEvent& event,
        TimePoint now);
    [[nodiscard]] Status handle_process_start(
        const platform::ProcessEvent& event,
        std::size_t game_index,
        TimePoint now);
    [[nodiscard]] Status handle_process_stop(std::uint32_t process_id);
    [[nodiscard]] Status handle_directory_event(
        const platform::DirectoryEvent& event,
        TimePoint now);
    [[nodiscard]] Status drain_process_events(TimePoint now);
    [[nodiscard]] Status collect_due_commits(TimePoint now);
    [[nodiscard]] Status service_repository_tasks(TimePoint now);
    void mark_dirty(RepositoryRuntime& repository, TimePoint now) noexcept;
    [[nodiscard]] std::optional<std::pair<TimePoint, CommitReason>>
    commit_deadline(const RepositoryRuntime& repository) const;

    void enqueue_process_event(const platform::ProcessEvent& event) noexcept;
    [[nodiscard]] Status request_commit(
        platform::RepositoryKey repository,
        CommitReason reason);
    [[nodiscard]] Status request_push(platform::RepositoryKey repository);
    [[nodiscard]] Status launch_commit(
        RepositoryRuntime& repository,
        CommitReason reason);
    [[nodiscard]] Status launch_push(RepositoryRuntime& repository);
    [[nodiscard]] Status finish_commit(
        RepositoryRuntime& repository,
        TaskResult result,
        std::optional<Error> error,
        TimePoint now);
    [[nodiscard]] Status finish_push(
        RepositoryRuntime& repository,
        TaskResult result,
        std::optional<Error> error);
    [[nodiscard]] Status schedule_periodic_pushes(TimePoint now);
    void deliver_network_warning(RepositoryRuntime& repository);
    [[nodiscard]] std::chrono::milliseconds calculate_wait(
        std::chrono::milliseconds maximum_wait,
        TimePoint now) const;
    [[nodiscard]] std::optional<std::size_t> match_game(
        const platform::ProcessEvent& event) const;
    [[nodiscard]] Status check_owner_thread() const;

    CoreAppOptions options_;
    std::unique_ptr<platform::ProcessEventSource> process_events_;
    std::unique_ptr<platform::DirectoryWatcher> directory_watcher_;
    std::vector<GameRuntime> games_;
    std::unordered_map<std::string, std::size_t> game_by_process_path_;
    std::unordered_map<platform::RepositoryKey, RepositoryRuntime> repositories_;
    std::unordered_map<std::uint32_t, std::size_t> game_by_process_id_;

    std::list<ActiveTask> active_tasks_;
    std::unordered_map<platform::RepositoryKey, TimePoint> periodic_push_deadlines_;
    std::unordered_set<platform::RepositoryKey> exit_push_pending_;

    mutable SRWLOCK callback_lock_ = SRWLOCK_INIT;
    std::deque<platform::ProcessEvent> process_event_queue_;
    std::optional<Error> callback_error_;
    bool accept_process_events_{};

    DWORD owner_thread_id_{};
    bool start_attempted_{};
    bool started_{};
    std::atomic_bool stop_requested_{};
};

}  // namespace gsave::core
