#include "gsave/core/app.hpp"
#include "gsave/repository/repository_engine.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cerrno>
#include <limits>
#include <process.h>
#include <string_view>
#include <system_error>
#include <utility>

namespace gsave::core {
namespace {

using namespace std::chrono_literals;

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path absolute_from(
    const std::filesystem::path& base,
    const std::filesystem::path& value) {
    if (is_windows_absolute_path(value)) {
        return normalize_windows_path_lexically(value);
    }
    return normalize_windows_path_lexically(base / value);
}

[[nodiscard]] bool windows_text_equal(
    const std::wstring_view left,
    const std::wstring_view right) noexcept {
    if (left.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())
        || right.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    return CompareStringOrdinal(
               left.data(), static_cast<int>(left.size()),
               right.data(), static_cast<int>(right.size()), TRUE)
        == CSTR_EQUAL;
}

[[nodiscard]] bool windows_path_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    const auto normalized_left = normalize_windows_path_lexically(left).wstring();
    const auto normalized_right = normalize_windows_path_lexically(right).wstring();
    return windows_text_equal(normalized_left, normalized_right);
}

[[nodiscard]] std::wstring process_name_from_utf8(const std::string& value) {
    const auto utf8 = std::u8string{
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size())};
    return std::filesystem::path{utf8}.wstring();
}

[[nodiscard]] const char* commit_reason_name(const CommitReason reason) noexcept {
    switch (reason) {
    case CommitReason::quiet_period_elapsed:
        return "quiet";
    case CommitReason::max_interval_elapsed:
        return "periodic";
    case CommitReason::game_exit:
        return "game-exit";
    }
    return "unknown";
}

class ExclusiveSrwLock final {
public:
    explicit ExclusiveSrwLock(SRWLOCK& lock) noexcept : lock_(lock) {
        AcquireSRWLockExclusive(&lock_);
    }
    ~ExclusiveSrwLock() { ReleaseSRWLockExclusive(&lock_); }

    ExclusiveSrwLock(const ExclusiveSrwLock&) = delete;
    ExclusiveSrwLock& operator=(const ExclusiveSrwLock&) = delete;

private:
    SRWLOCK& lock_;
};

[[nodiscard]] std::chrono::milliseconds non_negative_wait_until(
    const TimePoint deadline,
    const TimePoint now) {
    if (deadline <= now) {
        return 0ms;
    }
    return std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
}

[[nodiscard]] bool is_network_error(const Error& error) noexcept {
    return error.code == std::make_error_code(std::errc::network_unreachable)
        || error.code == std::make_error_code(std::errc::host_unreachable)
        || error.code == std::make_error_code(std::errc::network_down)
        || error.code == std::make_error_code(std::errc::timed_out)
        || error.code == std::make_error_code(std::errc::connection_reset)
        || error.code == std::make_error_code(std::errc::connection_refused);
}

[[nodiscard]] bool positive(
    const std::optional<std::chrono::seconds>& duration) noexcept {
    return duration && duration->count() > 0;
}

[[nodiscard]] bool automatic_sync_ready(
    const config::GameConfig& game) noexcept {
    return game.sync.credential_reference
        && !game.sync.credential_reference->empty();
}

[[nodiscard]] Status validate_commit_policy(const CommitPolicy& policy) {
    bool valid = false;
    switch (policy.strategy) {
    case CommitStrategy::quiet:
        valid = positive(policy.quiet_interval)
            && !policy.max_interval && !policy.commit_on_exit;
        break;
    case CommitStrategy::periodic:
        valid = !policy.quiet_interval
            && positive(policy.max_interval) && !policy.commit_on_exit;
        break;
    case CommitStrategy::on_exit:
        valid = !policy.quiet_interval
            && !policy.max_interval && policy.commit_on_exit;
        break;
    case CommitStrategy::hybrid:
        valid = positive(policy.quiet_interval)
            && positive(policy.max_interval) && policy.commit_on_exit;
        break;
    }
    if (!valid) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "commit policy fields do not match its strategy"));
    }
    return {};
}

void show_network_warning(const Error& error) noexcept {
    using MessageBoxFunction = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
    const auto text = error.message();
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (length <= 0) return;
    std::wstring message(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        message.data(), length);
    const auto user32 = LoadLibraryExW(
        L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user32 == nullptr) return;
    const auto message_box = std::bit_cast<MessageBoxFunction>(
        GetProcAddress(user32, "MessageBoxW"));
    if (message_box != nullptr) {
        message_box(
            nullptr, message.c_str(), L"G-SAVE network synchronization failed",
            MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    }
    FreeLibrary(user32);
}

}  // namespace

CoreApp::CoreApp(
    CoreAppOptions options,
    std::unique_ptr<platform::ProcessEventSource> process_events,
    std::unique_ptr<platform::DirectoryWatcher> directory_watcher)
    : options_(std::move(options)),
      process_events_(std::move(process_events)),
      directory_watcher_(std::move(directory_watcher)) {}

CoreApp::~CoreApp() {
    if (started_ && owner_thread_id_ != GetCurrentThreadId()) {
        // ProcessEventSource owns COM state on owner_thread_id_. Destroying it
        // elsewhere would be unsafe and its platform contract is fail-fast.
        std::terminate();
    }
    shutdown();
}

Status CoreApp::check_owner_thread() const {
    if (owner_thread_id_ != GetCurrentThreadId()) {
        return std::unexpected(make_error(
            std::errc::operation_not_permitted,
            "CoreApp operation must run on its WMI owner thread"));
    }
    return {};
}

Status CoreApp::validate_and_build_runtime() {
    if (!process_events_ || !directory_watcher_) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "CoreApp platform dependencies must not be null"));
    }
    if (options_.config_directory.empty()) {
        return std::unexpected(make_error(
            std::errc::invalid_argument,
            "configuration directory must not be empty"));
    }

    options_.config_directory = normalize_windows_path_lexically(
        options_.config_directory);
    games_.clear();
    game_by_process_path_.clear();
    repositories_.clear();

    std::error_code filesystem_error;
    platform::RepositoryKey next_repository = 1;
    for (std::size_t config_index = 0;
         config_index < options_.config.games.size(); ++config_index) {
        auto& game = options_.config.games[config_index];
        if (!game.enabled) {
            continue;
        }
        if (game.sync.backend != config::SyncBackend::git) {
            return std::unexpected(make_error(
                std::errc::not_supported,
                "Core currently supports only Git push for " + game.id));
        }
        if (auto policy = validate_commit_policy(game.commit); !policy) {
            return std::unexpected(make_error(
                policy.error().code,
                policy.error().context + " for " + game.id));
        }

        game.process_path = absolute_from(options_.config_directory, game.process_path);
        game.parser = absolute_from(options_.config_directory, game.parser);
        filesystem_error.clear();
        if (!std::filesystem::is_regular_file(game.process_path, filesystem_error)) {
            return std::unexpected(make_error(
                filesystem_error ? filesystem_error : std::make_error_code(std::errc::no_such_file_or_directory),
                "configured game executable is missing for " + game.id + ": "
                    + path_text(game.process_path)));
        }
        filesystem_error.clear();
        if (!std::filesystem::is_regular_file(game.parser, filesystem_error)) {
            return std::unexpected(make_error(
                filesystem_error ? filesystem_error : std::make_error_code(std::errc::no_such_file_or_directory),
                "configured parser is missing for " + game.id + ": "
                    + path_text(game.parser)));
        }

        const auto runtime_index = games_.size();
        GameRuntime runtime{
            .config_index = config_index,
            .active_processes = 0,
            .process_name = process_name_from_utf8(game.process_name),
            .repositories = {},
        };
        runtime.repositories.reserve(game.saves.size());

        const auto process_key = windows_path_key(game.process_path);
        if (!game_by_process_path_.emplace(process_key, runtime_index).second) {
            return std::unexpected(make_error(
                std::errc::file_exists,
                "enabled games have the same process image path"));
        }
        if (std::ranges::any_of(games_, [&](const GameRuntime& existing) {
                return options_.config.games[existing.config_index].id == game.id;
            })) {
            return std::unexpected(make_error(
                std::errc::file_exists,
                "enabled games have the same game id"));
        }

        for (auto& save : game.saves) {
            save.path = absolute_from(options_.config_directory, save.path);
            filesystem_error.clear();
            if (!std::filesystem::is_directory(save.path, filesystem_error)) {
                return std::unexpected(make_error(
                    filesystem_error ? filesystem_error : std::make_error_code(std::errc::not_a_directory),
                    "save repository is unavailable for " + game.id + ": "
                        + path_text(save.path)));
            }
            const auto git_directory = save.path / L".git";
            filesystem_error.clear();
            if (!std::filesystem::is_directory(git_directory, filesystem_error)
                || !std::filesystem::is_regular_file(git_directory / L"HEAD", filesystem_error)) {
                return std::unexpected(make_error(
                    filesystem_error ? filesystem_error : std::make_error_code(std::errc::invalid_argument),
                    "save directory is not an initialized in-place Git repository: "
                        + path_text(save.path)));
            }

            if (std::ranges::any_of(
                    repositories_, [&save](const auto& entry) {
                        return windows_path_equal(entry.second.path, save.path);
                    })) {
                return std::unexpected(make_error(
                    std::errc::file_exists,
                    "save repository is configured more than once: "
                        + path_text(save.path)));
            }

            runtime.repositories.push_back(next_repository);
            RepositoryRuntime repository{};
            repository.path = save.path;
            repository.save_config = &save;
            repository.game_index = runtime_index;
            repository.key = next_repository;
            repositories_.emplace(next_repository, std::move(repository));
            ++next_repository;
        }
        games_.push_back(std::move(runtime));
    }
    return {};
}

Status CoreApp::start() {
    if (start_attempted_) {
        return std::unexpected(make_error(
            std::errc::operation_in_progress,
            "CoreApp cannot be started more than once"));
    }
    start_attempted_ = true;
    owner_thread_id_ = GetCurrentThreadId();

    if (auto validation = validate_and_build_runtime(); !validation) {
        return validation;
    }

    {
        const ExclusiveSrwLock lock(callback_lock_);
        accept_process_events_ = true;
    }
    started_ = true;
    auto started = process_events_->start(
        [this](const platform::ProcessEvent& event) {
            if (event.kind == platform::ProcessEventKind::started) {
                if (!match_game(event)) return;
            } else if (!event.process_name.empty()) {
                const bool relevant = std::ranges::any_of(
                    games_, [&event](const GameRuntime& runtime) {
                        return windows_text_equal(runtime.process_name, event.process_name);
                    });
                if (!relevant) return;
            }
            enqueue_process_event(event);
        });
    if (!started) {
        process_events_->stop();
        {
            const ExclusiveSrwLock lock(callback_lock_);
            accept_process_events_ = false;
            process_event_queue_.clear();
        }
        started_ = false;
        return std::unexpected(started.error());
    }
    return {};
}

Status CoreApp::run() {
    if (!started_) {
        if (auto status = start(); !status) {
            return status;
        }
    }
    if (auto owner = check_owner_thread(); !owner) {
        return owner;
    }

    while (!stop_requested_.load(std::memory_order_acquire)) {
        auto iteration = run_iteration(std::chrono::milliseconds::max());
        if (!iteration) {
            shutdown();
            return iteration;
        }
    }
    shutdown();
    return {};
}

void CoreApp::request_stop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
    static_cast<void>(directory_watcher_->wake());
}

void CoreApp::enqueue_process_event(const platform::ProcessEvent& event) noexcept {
    try {
        {
            const ExclusiveSrwLock lock(callback_lock_);
            if (!accept_process_events_) {
                return;
            }
            process_event_queue_.push_back(event);
        }
        auto wake = directory_watcher_->wake();
        if (!wake) {
            const ExclusiveSrwLock lock(callback_lock_);
            if (!callback_error_) {
                callback_error_ = std::move(wake.error());
            }
        }
    } catch (...) {
        // A dropped process event can silently disable backups. Out-of-memory
        // or an unexpected callback exception is therefore unrecoverable.
        std::terminate();
    }
}

std::optional<std::size_t> CoreApp::match_game(
    const platform::ProcessEvent& event) const {
    if (event.image_path.empty()) {
        return std::nullopt;
    }
    const auto entry = game_by_process_path_.find(windows_path_key(event.image_path));
    if (entry == game_by_process_path_.end()) {
        return std::nullopt;
    }

    const auto& runtime = games_[entry->second];
    const auto& game = options_.config.games[runtime.config_index];
    const auto observed_name = event.process_name.empty()
        ? event.image_path.filename().wstring()
        : event.process_name;
    if (!windows_text_equal(runtime.process_name, observed_name)
        || !windows_path_equal(game.process_path, event.image_path)) {
        return std::nullopt;
    }
    return entry->second;
}

Status CoreApp::handle_process_start(
    const platform::ProcessEvent& event,
    const std::size_t game_index,
    const TimePoint now) {
    if (game_by_process_id_.contains(event.process_id)) {
        return {};
    }
    auto& runtime = games_[game_index];
    if (runtime.active_processes == 0) {
        std::vector<platform::RepositoryKey> attached;
        attached.reserve(runtime.repositories.size());
        for (const auto repository_key : runtime.repositories) {
            auto& repository = repositories_.at(repository_key);
            auto watched = directory_watcher_->add(platform::DirectoryWatchRequest{
                .repository = repository.key,
                .root = repository.path,
                .recursive = true,
            });
            if (!watched) {
                for (const auto attached_key : attached) {
                    static_cast<void>(directory_watcher_->remove(attached_key));
                    repositories_.at(attached_key).watched = false;
                }
                return std::unexpected(watched.error());
            }
            repository.watched = true;
            attached.push_back(repository_key);

            // Conservatively reconcile writes which may have occurred between
            // process creation and watch attachment. The transient task performs the
            // actual status check and creates no commit when the tree matches.
            mark_dirty(repository, now);
        }
    }

    game_by_process_id_.emplace(event.process_id, game_index);
    ++runtime.active_processes;
    return {};
}

Status CoreApp::handle_process_stop(const std::uint32_t process_id) {
    const auto process = game_by_process_id_.find(process_id);
    if (process == game_by_process_id_.end()) {
        return {};
    }
    auto& runtime = games_[process->second];
    game_by_process_id_.erase(process);
    if (runtime.active_processes == 0) {
        return std::unexpected(make_error(
            std::errc::state_not_recoverable,
            "process reference count underflow"));
    }
    --runtime.active_processes;
    if (runtime.active_processes != 0) {
        return {};
    }

    const auto& game = options_.config.games[runtime.config_index];
    if (automatic_sync_ready(game)
        && game.sync.trigger == config::SyncTrigger::on_exit) {
        for (const auto repository : runtime.repositories) {
            exit_push_pending_.insert(repository);
        }
    }

    if (game.commit.commit_on_exit) {
        for (const auto repository_key : runtime.repositories) {
            auto& repository = repositories_.at(repository_key);
            if (repository.dirty) {
                repository.dirty = false;
                if (auto queued = request_commit(
                        repository_key, CommitReason::game_exit); !queued) {
                    return queued;
                }
            }
        }
    }

    for (const auto repository_key : runtime.repositories) {
        auto& repository = repositories_.at(repository_key);
        if (repository.watched) {
            if (auto removed = directory_watcher_->remove(repository_key); !removed) {
                return removed;
            }
            repository.watched = false;
        }

        if (exit_push_pending_.contains(repository_key)
            && !repository.dirty
            && !repository.commit_running
            && !repository.commit_pending) {
            if (auto pushed = request_push(repository_key); !pushed) {
                return pushed;
            }
            exit_push_pending_.erase(repository_key);
        }
        // A later push may already be in flight. Let finish_push() either clear
        // the stale warning on success or deliver the final failure after the
        // temporary task settles.
        if (!repository.push_running) {
            deliver_network_warning(repository);
        }
    }
    return {};
}

Status CoreApp::handle_process_event(
    const platform::ProcessEvent& event,
    const TimePoint now) {
    if (event.kind == platform::ProcessEventKind::stopped) {
        return handle_process_stop(event.process_id);
    }
    const auto game = match_game(event);
    if (!game) {
        return {};
    }
    return handle_process_start(event, *game, now);
}

Status CoreApp::drain_process_events(const TimePoint now) {
    std::deque<platform::ProcessEvent> events;
    {
        const ExclusiveSrwLock lock(callback_lock_);
        if (callback_error_) {
            auto error = std::move(*callback_error_);
            callback_error_.reset();
            return std::unexpected(std::move(error));
        }
        events.swap(process_event_queue_);
    }
    for (const auto& event : events) {
        if (auto handled = handle_process_event(event, now); !handled) {
            return handled;
        }
    }
    return {};
}

Status CoreApp::handle_directory_event(
    const platform::DirectoryEvent& event,
    const TimePoint now) {
    const auto repository = repositories_.find(event.repository);
    if (repository == repositories_.end() || !repository->second.watched) {
        return {};
    }
    if (event.kind != platform::DirectoryEventKind::overflow
        && platform::is_git_metadata_path(event.relative_path.wstring())) {
        return {};
    }
    if (event.kind != platform::DirectoryEventKind::overflow) {
        const auto relative = path_text(event.relative_path);
        const auto& save = *repository->second.save_config;
        if (std::ranges::any_of(save.exclude_globs, [&](const auto& pattern) {
                return path_glob_matches(pattern, relative);
            })) {
            return {};
        }
        if (!save.include_globs.empty()
            && std::ranges::none_of(save.include_globs, [&](const auto& pattern) {
                return path_glob_matches(pattern, relative);
            })) {
            return {};
        }
    }
    mark_dirty(repository->second, now);
    return {};
}

void CoreApp::mark_dirty(
    RepositoryRuntime& repository,
    const TimePoint now) noexcept {
    if (!repository.dirty) {
        repository.dirty = true;
        repository.first_change = now;
    }
    repository.last_change = now;
}

std::optional<std::pair<TimePoint, CommitReason>> CoreApp::commit_deadline(
    const RepositoryRuntime& repository) const {
    if (!repository.dirty) return std::nullopt;
    const auto& game = options_.config.games.at(
        games_.at(repository.game_index).config_index);
    std::optional<std::pair<TimePoint, CommitReason>> due;
    if (game.commit.quiet_interval) {
        due.emplace(
            repository.last_change + *game.commit.quiet_interval,
            CommitReason::quiet_period_elapsed);
    }
    if (game.commit.max_interval) {
        const auto maximum = repository.first_change + *game.commit.max_interval;
        if (!due || maximum < due->first) {
            due.emplace(maximum, CommitReason::max_interval_elapsed);
        }
    }
    return due;
}

Status CoreApp::collect_due_commits(const TimePoint now) {
    for (auto& [key, repository] : repositories_) {
        if (repository.commit_retry_deadline
            && *repository.commit_retry_deadline <= now
            && !repository.commit_running) {
            repository.commit_retry_deadline.reset();
            repository.dirty = false;
            if (auto queued = request_commit(key, repository.running_reason); !queued) {
                return queued;
            }
            continue;
        }
        const auto due = commit_deadline(repository);
        if (due && due->first <= now) {
            repository.dirty = false;
            if (auto queued = request_commit(key, due->second); !queued) {
                return queued;
            }
        }
    }
    return {};
}

Status CoreApp::schedule_periodic_pushes(const TimePoint now) {
    for (auto position = periodic_push_deadlines_.begin();
         position != periodic_push_deadlines_.end();) {
        if (position->second <= now) {
            if (auto pushed = request_push(position->first); !pushed) {
                return pushed;
            }
            position = periodic_push_deadlines_.erase(position);
        } else {
            ++position;
        }
    }
    return {};
}

unsigned __stdcall CoreApp::execute_task(void* const context) noexcept {
    auto& task = *static_cast<ActiveTask*>(context);
    auto& owner = *task.owner;
    // THREAD_MODE_BACKGROUND_BEGIN also forces very-low I/O and memory
    // priority.  That can throttle ordinary multi-megabyte save commits for
    // tens of seconds.  Yield CPU time to the game without throttling the
    // repository's finite disk work.
    static_cast<void>(SetThreadPriority(
        GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL));
    auto result = TaskResult::failed;
    try {
        if (task.operation == TaskOperation::commit) {
            const auto& request = *task.commit;
            auto committed = owner.options_.commit_callback == nullptr
                ? repository::commit_repository(request)
                : owner.options_.commit_callback(
                    owner.options_.repository_context, request);
            if (committed) {
                switch (*committed) {
                case repository::CommitOutcome::created:
                    result = TaskResult::created;
                    break;
                case repository::CommitOutcome::no_changes:
                    result = TaskResult::no_changes;
                    break;
                case repository::CommitOutcome::worktree_unstable:
                    result = TaskResult::worktree_unstable;
                    break;
                }
            } else {
                task.error = committed.error();
            }
        } else {
            const auto& request = *task.push;
            constexpr DWORD retry_delays[] = {1000, 3000};
            for (std::size_t attempt = 0; attempt < 3; ++attempt) {
                auto pushed = owner.options_.push_callback == nullptr
                    ? repository::push_repository(request)
                    : owner.options_.push_callback(
                        owner.options_.repository_context, request);
                if (pushed) {
                    result = TaskResult::completed;
                    break;
                }
                task.error = pushed.error();
                if (!is_network_error(*task.error) || attempt == 2) break;
                Sleep(retry_delays[attempt]);
            }
        }
    } catch (...) {
        task.error = make_error(
            std::errc::io_error, "repository task raised an exception");
    }
    task.result.store(result, std::memory_order_release);
    static_cast<void>(owner.directory_watcher_->wake());
    return 0;
}

Status CoreApp::launch_commit(
    RepositoryRuntime& repository,
    const CommitReason reason) {
    const auto& game = options_.config.games.at(
        games_.at(repository.game_index).config_index);
    auto& task = active_tasks_.emplace_back();
    task.owner = this;
    task.operation = TaskOperation::commit;
    task.repository = repository.key;
    task.commit.emplace(repository::CommitOptions{
        .repository = repository.path,
        .game_id = game.id,
        .parser = game.parser,
        .reason = commit_reason_name(reason),
    });
    repository.commit_running = true;
    repository.commit_pending = false;
    repository.running_reason = reason;

    errno = 0;
    const auto thread = _beginthreadex(
        nullptr, 0, &CoreApp::execute_task, &task, 0, nullptr);
    if (thread == 0) {
        const auto code = errno == 0
            ? std::make_error_code(std::errc::resource_unavailable_try_again)
            : std::error_code{errno, std::generic_category()};
        repository.commit_running = false;
        active_tasks_.pop_back();
        return std::unexpected(make_error(
            code, "cannot create temporary commit thread"));
    }
    task.thread.reset(reinterpret_cast<HANDLE>(thread));
    return {};
}

Status CoreApp::launch_push(RepositoryRuntime& repository) {
    const auto& game = options_.config.games.at(
        games_.at(repository.game_index).config_index);
    auto& task = active_tasks_.emplace_back();
    task.owner = this;
    task.operation = TaskOperation::push;
    task.repository = repository.key;
    task.push.emplace(repository::PushOptions{
        .repository = repository.path,
        .remote = game.sync.remote,
        .credential_reference = {},
    });
    if (game.sync.credential_reference) {
        const auto& value = *game.sync.credential_reference;
        task.push->credential_reference = std::filesystem::path{std::u8string{
            reinterpret_cast<const char8_t*>(value.data()),
            reinterpret_cast<const char8_t*>(value.data() + value.size())}}.wstring();
    }
    repository.push_running = true;
    repository.push_pending = false;

    errno = 0;
    const auto thread = _beginthreadex(
        nullptr, 0, &CoreApp::execute_task, &task, 0, nullptr);
    if (thread == 0) {
        const auto code = errno == 0
            ? std::make_error_code(std::errc::resource_unavailable_try_again)
            : std::error_code{errno, std::generic_category()};
        repository.push_running = false;
        active_tasks_.pop_back();
        return std::unexpected(make_error(
            code, "cannot create temporary push thread"));
    }
    task.thread.reset(reinterpret_cast<HANDLE>(thread));
    return {};
}

Status CoreApp::request_commit(
    const platform::RepositoryKey key,
    const CommitReason reason) {
    auto& repository = repositories_.at(key);
    if (repository.commit_running) {
        if (!repository.commit_pending) repository.pending_reason = reason;
        repository.commit_pending = true;
        if (reason == CommitReason::game_exit) {
            repository.pending_reason = reason;
        }
        return {};
    }
    return launch_commit(repository, reason);
}

Status CoreApp::request_push(const platform::RepositoryKey key) {
    auto& repository = repositories_.at(key);
    if (repository.push_running) {
        repository.push_pending = true;
        return {};
    }
    return launch_push(repository);
}

Status CoreApp::finish_commit(
    RepositoryRuntime& repository,
    const TaskResult result,
    std::optional<Error> error,
    const TimePoint now) {
    repository.commit_running = false;
    const bool created = result == TaskResult::created;
    if (result == TaskResult::worktree_unstable) {
        repository.commit_pending = false;
        mark_dirty(repository, now);
        repository.commit_retry_deadline = now + std::chrono::seconds{1};
        return {};
    }
    if (result != TaskResult::created && result != TaskResult::no_changes) {
        ++repository.consecutive_commit_failures;
        if (repository.consecutive_commit_failures >= 3) {
            return std::unexpected(error.value_or(make_error(
                std::errc::io_error, "Git commit failed three consecutive times")));
        }
        repository.commit_pending = false;
        mark_dirty(repository, now);
        repository.commit_retry_deadline = now + (
            repository.consecutive_commit_failures == 1
                ? std::chrono::seconds{1}
                : std::chrono::seconds{3});
        return {};
    }
    repository.consecutive_commit_failures = 0;
    repository.commit_retry_deadline.reset();

    {
        const auto& game = options_.config.games.at(
            games_.at(repository.game_index).config_index);
        if (!automatic_sync_ready(game)) return {};
        switch (game.sync.trigger) {
        case config::SyncTrigger::on_commit:
            if (created) {
                if (auto pushed = request_push(repository.key); !pushed) return pushed;
            }
            break;
        case config::SyncTrigger::on_exit:
            if (!repository.dirty
                && !repository.commit_pending
                && exit_push_pending_.erase(repository.key) != 0) {
                if (auto pushed = request_push(repository.key); !pushed) return pushed;
            }
            break;
        case config::SyncTrigger::periodic:
            if (created && game.sync.interval) {
                periodic_push_deadlines_.insert_or_assign(
                    repository.key, now + *game.sync.interval);
            }
            break;
        case config::SyncTrigger::manual:
            break;
        }
    }
    return {};
}

Status CoreApp::finish_push(
    RepositoryRuntime& repository,
    const TaskResult result,
    std::optional<Error> error) {
    repository.push_running = false;
    if (result == TaskResult::completed) {
        repository.deferred_network_warning.reset();
        return {};
    }
    auto failure = error.value_or(make_error(
        std::errc::io_error, "Git synchronization failed"));
    if (is_network_error(failure)) {
        repository.deferred_network_warning = std::move(failure);
        if (games_.at(repository.game_index).active_processes == 0) {
            deliver_network_warning(repository);
        }
        return {};
    }
    return std::unexpected(std::move(failure));
}

void CoreApp::deliver_network_warning(RepositoryRuntime& repository) {
    if (!repository.deferred_network_warning) return;
    if (options_.network_warning_callback != nullptr) {
        options_.network_warning_callback(
            options_.repository_context, *repository.deferred_network_warning);
    } else {
        show_network_warning(*repository.deferred_network_warning);
    }
    repository.deferred_network_warning.reset();
}

Status CoreApp::service_repository_tasks(const TimePoint now) {
    for (auto position = active_tasks_.begin(); position != active_tasks_.end();) {
        const auto result = position->result.load(std::memory_order_acquire);
        if (result == TaskResult::pending) {
            ++position;
            continue;
        }
        static_cast<void>(WaitForSingleObject(position->thread.get(), INFINITE));
        const auto operation = position->operation;
        const auto key = position->repository;
        auto error = std::move(position->error);
        position = active_tasks_.erase(position);
        auto& repository = repositories_.at(key);
        if (operation == TaskOperation::commit) {
            if (auto finished = finish_commit(
                    repository, result, std::move(error), now); !finished) {
                return finished;
            }
        } else {
            if (auto finished = finish_push(
                    repository, result, std::move(error)); !finished) {
                return finished;
            }
        }
    }

    for (auto& [_, repository] : repositories_) {
        if (repository.commit_pending && !repository.commit_running) {
            const auto reason = repository.pending_reason;
            if (auto launched = launch_commit(repository, reason); !launched) return launched;
        }
        if (repository.push_pending && !repository.push_running) {
            if (auto launched = launch_push(repository); !launched) return launched;
        }
    }
    return {};
}

std::chrono::milliseconds CoreApp::calculate_wait(
    std::chrono::milliseconds maximum_wait,
    const TimePoint now) const {
    using namespace std::chrono_literals;
    if (maximum_wait < 0ms) {
        maximum_wait = 0ms;
    }
    auto wait = maximum_wait;
    const auto shorten = [&wait, now](const TimePoint deadline) {
        wait = std::min(wait, non_negative_wait_until(deadline, now));
    };

    for (const auto& [_, repository] : repositories_) {
        if (const auto deadline = commit_deadline(repository)) {
            shorten(deadline->first);
        }
        if (repository.commit_retry_deadline) {
            shorten(*repository.commit_retry_deadline);
        }
    }
    for (const auto& [_, deadline] : periodic_push_deadlines_) {
        shorten(deadline);
    }
    {
        const ExclusiveSrwLock lock(callback_lock_);
        if (!process_event_queue_.empty() || callback_error_) {
            wait = 0ms;
        }
    }
    return wait;
}

Status CoreApp::run_iteration(const std::chrono::milliseconds maximum_wait) {
    if (!started_) {
        return std::unexpected(make_error(
            std::errc::operation_not_permitted,
            "CoreApp must be started before running an iteration"));
    }
    if (auto owner = check_owner_thread(); !owner) {
        return owner;
    }

    auto now = CoreClock::now();
    if (auto drained = drain_process_events(now); !drained) {
        return drained;
    }
    if (auto collected = collect_due_commits(now); !collected) {
        return collected;
    }
    if (auto scheduled = schedule_periodic_pushes(now); !scheduled) {
        return scheduled;
    }
    if (auto tasks = service_repository_tasks(now); !tasks) {
        return tasks;
    }

    auto event = directory_watcher_->poll(calculate_wait(maximum_wait, now));
    if (!event) {
        return std::unexpected(event.error());
    }

    now = CoreClock::now();
    if (*event) {
        if (auto handled = handle_directory_event(**event, now); !handled) {
            return handled;
        }
    }
    if (auto drained = drain_process_events(now); !drained) {
        return drained;
    }
    if (auto collected = collect_due_commits(now); !collected) {
        return collected;
    }
    if (auto scheduled = schedule_periodic_pushes(now); !scheduled) {
        return scheduled;
    }
    return service_repository_tasks(now);
}

void CoreApp::shutdown() noexcept {
    if (!started_) {
        return;
    }
    if (owner_thread_id_ != GetCurrentThreadId()) {
        std::terminate();
    }

    process_events_->stop();
    {
        const ExclusiveSrwLock lock(callback_lock_);
        accept_process_events_ = false;
        process_event_queue_.clear();
    }
    for (auto& task : active_tasks_) {
        static_cast<void>(WaitForSingleObject(task.thread.get(), INFINITE));
    }
    active_tasks_.clear();
    directory_watcher_->stop();
    for (auto& [_, repository] : repositories_) {
        repository.watched = false;
    }
    game_by_process_id_.clear();
    started_ = false;
}

}  // namespace gsave::core
