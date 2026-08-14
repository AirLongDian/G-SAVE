#pragma once

#include <gsave/base/error.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace gsave::platform {

enum class ProcessEventKind {
    started,
    stopped,
};

// This is intentionally a value-only event. The WMI callback never exposes COM
// objects or handles to the Core event loop.
struct ProcessEvent {
    ProcessEventKind kind{};
    std::uint32_t process_id{};
    std::wstring process_name;
    std::filesystem::path image_path;
    bool from_initial_snapshot{};
};

using ProcessEventSink = std::function<void(const ProcessEvent&)>;

class ProcessEventSource {
public:
    ProcessEventSource() = default;
    ProcessEventSource(const ProcessEventSource&) = delete;
    ProcessEventSource& operator=(const ProcessEventSource&) = delete;
    ProcessEventSource(ProcessEventSource&&) = delete;
    ProcessEventSource& operator=(ProcessEventSource&&) = delete;
    virtual ~ProcessEventSource() = default;

    // start() installs both start/stop subscriptions and then emits a
    // de-duplicated snapshot of processes which are already running. COM owns
    // an apartment on the calling thread, so start()/stop()/destruction must
    // occur on that same owner thread. Violating this precondition terminates
    // the process instead of releasing apartment-bound COM state unsafely.
    virtual std::expected<void, Error> start(ProcessEventSink sink) = 0;

    // stop() is idempotent. No sink invocation starts after it returns.
    virtual void stop() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<ProcessEventSource>
make_wmi_process_event_source();

} // namespace gsave::platform
