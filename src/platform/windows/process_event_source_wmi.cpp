#include <gsave/platform/process_event_source.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <Wbemidl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <system_error>
#include <utility>
#include <vector>

namespace gsave::platform {
namespace {

[[nodiscard]] Error make_windows_error(
    const DWORD code, std::string context) {
    return make_error(
        std::error_code{static_cast<int>(code), std::system_category()},
        std::move(context));
}

[[nodiscard]] Error make_hresult_error(
    const HRESULT result, std::string context) {
    char value[11]{};
    std::snprintf(
        value, sizeof(value), "0x%08lX",
        static_cast<unsigned long>(result));
    context += " (HRESULT ";
    context += value;
    context += ')';
    return make_error(
        std::error_code{static_cast<int>(result), std::system_category()},
        std::move(context));
}

template <typename Interface>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(Interface* pointer) noexcept : pointer_(pointer) {}
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)) {}

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        return *this;
    }

    ~ComPtr() { reset(); }

    [[nodiscard]] Interface* get() const noexcept { return pointer_; }
    [[nodiscard]] Interface** put() noexcept {
        reset();
        return &pointer_;
    }
    Interface* operator->() const noexcept { return pointer_; }

    void reset() noexcept {
        if (pointer_ != nullptr) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

private:
    Interface* pointer_{};
};

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

class ScopedBstr {
public:
    explicit ScopedBstr(const wchar_t* value) : value_(SysAllocString(value)) {}
    ScopedBstr(const ScopedBstr&) = delete;
    ScopedBstr& operator=(const ScopedBstr&) = delete;
    ~ScopedBstr() { SysFreeString(value_); }

    [[nodiscard]] BSTR get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept { return value_ != nullptr; }

private:
    BSTR value_{};
};

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    ~UniqueHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE release() noexcept {
        return std::exchange(handle_, nullptr);
    }

private:
    HANDLE handle_{};
};

[[nodiscard]] std::filesystem::path normalize_image_path(
    std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.starts_with(L"\\\\?\\UNC\\")) {
        path.replace(0, 8, L"\\\\");
    } else if (path.starts_with(L"\\\\?\\")) {
        path.erase(0, 4);
    }

    auto normalized = std::filesystem::path{path}.lexically_normal().native();
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](const wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    return std::filesystem::path{std::move(normalized)};
}

[[nodiscard]] std::filesystem::path query_process_image_path(
    const DWORD process_id) {
    const UniqueHandle process{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id)};
    if (!process.valid()) {
        return {};
    }

    std::vector<wchar_t> buffer(32768);
    DWORD size = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &size)
        == FALSE) {
        return {};
    }
    return normalize_image_path(std::wstring{buffer.data(), size});
}

[[nodiscard]] std::uint32_t read_process_id(IWbemClassObject& object) {
    VARIANT value;
    VariantInit(&value);
    const auto result = object.Get(
        L"ProcessID", 0, &value, nullptr, nullptr);
    std::uint32_t process_id = 0;
    if (SUCCEEDED(result)) {
        switch (value.vt) {
        case VT_I4:
            process_id = static_cast<std::uint32_t>(value.lVal);
            break;
        case VT_UI4:
            process_id = value.ulVal;
            break;
        case VT_I2:
            process_id = static_cast<std::uint32_t>(value.iVal);
            break;
        case VT_UI2:
            process_id = value.uiVal;
            break;
        default:
            break;
        }
    }
    VariantClear(&value);
    return process_id;
}

[[nodiscard]] std::wstring read_process_name(IWbemClassObject& object) {
    VARIANT value;
    VariantInit(&value);
    const auto result = object.Get(
        L"ProcessName", 0, &value, nullptr, nullptr);
    std::wstring process_name;
    if (SUCCEEDED(result) && value.vt == VT_BSTR && value.bstrVal != nullptr) {
        process_name.assign(value.bstrVal, SysStringLen(value.bstrVal));
    }
    VariantClear(&value);
    return process_name;
}

class DispatchState {
public:
    void activate(ProcessEventSink sink) {
        const ExclusiveSrwLock lock{lock_};
        sink_ = std::move(sink);
        accepting_ = true;
    }

    void deactivate() noexcept {
        ProcessEventSink retired_sink;
        {
            const ExclusiveSrwLock lock{lock_};
            accepting_ = false;
            retired_sink = std::move(sink_);

            // A snapshot is emitted synchronously by start(), so its sink is
            // allowed to call stop() on the owner thread. Wait only for
            // callbacks executing on other threads.
            const auto caller_callbacks = callback_depth_on_this_thread();
            while (in_flight_ != caller_callbacks) {
                SleepConditionVariableSRW(
                    &callbacks_finished_, &lock_, INFINITE, 0);
            }
        }

        // Destroy the callable without holding mutex_: captured state may have
        // an arbitrary destructor and must not be able to re-enter this lock.
        retired_sink = {};
    }

    void started(
        const std::uint32_t process_id, std::wstring process_name,
        std::filesystem::path image_path, const bool from_snapshot) {
        ProcessEvent event{
            ProcessEventKind::started, process_id, std::move(process_name),
            std::move(image_path), from_snapshot};

        ProcessEventSink sink;
        {
            const ExclusiveSrwLock lock{lock_};
            if (!accepting_) {
                return;
            }

            // Copy first. A throwing std::function target must not leave an
            // in-flight count which can never be retired by invoke().
            sink = sink_;

            ++in_flight_;
        }

        invoke(std::move(sink), event);
    }

    void stopped(
        const std::uint32_t process_id,
        std::wstring process_name) {
        ProcessEvent event{
            ProcessEventKind::stopped, process_id, std::move(process_name), {},
            false};

        ProcessEventSink sink;
        {
            const ExclusiveSrwLock lock{lock_};
            if (!accepting_) {
                return;
            }
            sink = sink_;
            ++in_flight_;
        }

        invoke(std::move(sink), event);
    }

private:
    struct CallbackFrame {
        const DispatchState* state;
        CallbackFrame* previous;
    };

    [[nodiscard]] std::size_t callback_depth_on_this_thread() const noexcept {
        std::size_t depth = 0;
        for (auto* frame = current_callback_; frame != nullptr;
             frame = frame->previous) {
            if (frame->state == this) {
                ++depth;
            }
        }
        return depth;
    }

    void invoke(ProcessEventSink sink, const ProcessEvent& event) noexcept {
        CallbackFrame frame{this, current_callback_};
        current_callback_ = &frame;
        try {
            if (sink) {
                sink(event);
            }
        } catch (...) {
            // Exceptions cannot cross the COM callback boundary. The Core sink
            // is expected to enqueue a value and remain noexcept in practice.
        }
        current_callback_ = frame.previous;

        const ExclusiveSrwLock lock{lock_};
        --in_flight_;
        WakeAllConditionVariable(&callbacks_finished_);
    }

    SRWLOCK lock_ = SRWLOCK_INIT;
    CONDITION_VARIABLE callbacks_finished_ = CONDITION_VARIABLE_INIT;
    ProcessEventSink sink_;
    std::size_t in_flight_{};
    bool accepting_{};
    inline static thread_local CallbackFrame* current_callback_{};
};

class WmiProcessSink final : public IWbemObjectSink {
public:
    WmiProcessSink(
        const ProcessEventKind kind,
        std::shared_ptr<DispatchState> dispatch) noexcept
        : kind_(kind), dispatch_(std::move(dispatch)) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID interface_id, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (interface_id == IID_IUnknown
            || interface_id == IID_IWbemObjectSink) {
            *object = static_cast<IWbemObjectSink*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Indicate(
        const LONG object_count,
        IWbemClassObject** objects) override {
        if (objects == nullptr) {
            return static_cast<HRESULT>(WBEM_E_INVALID_PARAMETER);
        }

        try {
            for (LONG index = 0; index < object_count; ++index) {
                if (objects[index] == nullptr) {
                    continue;
                }
                const auto process_id = read_process_id(*objects[index]);
                if (process_id == 0) {
                    continue;
                }
                auto process_name = read_process_name(*objects[index]);
                if (kind_ == ProcessEventKind::started) {
                    auto image_path = query_process_image_path(process_id);
                    if (process_name.empty() && !image_path.empty()) {
                        process_name = image_path.filename().native();
                    }
                    dispatch_->started(
                        process_id, std::move(process_name),
                        std::move(image_path), false);
                } else {
                    dispatch_->stopped(process_id, std::move(process_name));
                }
            }
        } catch (...) {
            return static_cast<HRESULT>(WBEM_E_FAILED);
        }
        return WBEM_S_NO_ERROR;
    }

    HRESULT STDMETHODCALLTYPE SetStatus(
        LONG, HRESULT, BSTR, IWbemClassObject*) override {
        return WBEM_S_NO_ERROR;
    }

private:
    std::atomic<ULONG> references_{1};
    ProcessEventKind kind_;
    std::shared_ptr<DispatchState> dispatch_;
};

class WmiProcessEventSource final : public ProcessEventSource {
public:
    ~WmiProcessEventSource() override {
        enforce_owner_thread();
        stop();
    }

    std::expected<void, Error> start(ProcessEventSink sink) override {
        if (started_) {
            return std::unexpected(make_error(
                std::errc::operation_in_progress,
                "WMI process event source is already started"));
        }
        if (!sink) {
            return std::unexpected(make_error(
                std::errc::invalid_argument,
                "process event sink is empty"));
        }

        const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
            return std::unexpected(make_hresult_error(
                com_result, "initializing COM for WMI failed"));
        }
        owns_com_apartment_ = com_result == S_OK || com_result == S_FALSE;
        struct ApartmentRollback {
            explicit ApartmentRollback(bool& owned) noexcept : owns(owned) {}

            bool& owns;
            bool dismissed{};
            ~ApartmentRollback() {
                if (owns && !dismissed) {
                    CoUninitialize();
                    owns = false;
                }
            }
        } apartment_rollback(owns_com_apartment_);

        HANDLE duplicated_owner_thread = nullptr;
        if (DuplicateHandle(
                GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                &duplicated_owner_thread, SYNCHRONIZE, FALSE, 0)
            == FALSE) {
            return std::unexpected(make_windows_error(
                GetLastError(), "capturing WMI owner thread failed"));
        }
        UniqueHandle owner_thread{duplicated_owner_thread};

        const auto security_result = CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if (FAILED(security_result) && security_result != RPC_E_TOO_LATE) {
            return std::unexpected(make_hresult_error(
                security_result, "initializing COM security for WMI failed"));
        }

        ComPtr<IWbemLocator> locator;
        auto result = CoCreateInstance(
            CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(locator.put()));
        if (FAILED(result)) {
            return std::unexpected(make_hresult_error(
                result, "creating WMI locator failed"));
        }

        ScopedBstr wmi_namespace{L"ROOT\\CIMV2"};
        if (!wmi_namespace.valid()) {
            return std::unexpected(make_error(
                std::errc::not_enough_memory,
                "allocating WMI namespace string failed"));
        }

        ComPtr<IWbemServices> services;
        result = locator->ConnectServer(
            wmi_namespace.get(), nullptr, nullptr, nullptr, 0, nullptr,
            nullptr, services.put());
        if (FAILED(result)) {
            return std::unexpected(make_hresult_error(
                result, "connecting to ROOT\\CIMV2 failed"));
        }

        result = CoSetProxyBlanket(
            services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
            EOAC_NONE);
        if (FAILED(result)) {
            return std::unexpected(make_hresult_error(
                result, "setting WMI proxy security failed"));
        }

        auto dispatch = std::make_shared<DispatchState>();
        dispatch->activate(std::move(sink));
        ComPtr<IWbemObjectSink> start_sink{
            new WmiProcessSink{ProcessEventKind::started, dispatch}};
        ComPtr<IWbemObjectSink> stop_sink{
            new WmiProcessSink{ProcessEventKind::stopped, dispatch}};

        ScopedBstr query_language{L"WQL"};
        ScopedBstr start_query{L"SELECT * FROM Win32_ProcessStartTrace"};
        ScopedBstr stop_query{L"SELECT * FROM Win32_ProcessStopTrace"};
        if (!query_language.valid() || !start_query.valid()
            || !stop_query.valid()) {
            dispatch->deactivate();
            return std::unexpected(make_error(
                std::errc::not_enough_memory,
                "allocating WMI query string failed"));
        }

        // The sink intentionally ignores intermediate progress reports. A
        // zero flag still delivers events and the final SetStatus call while
        // avoiding an unnecessary status-reporting request.
        result = services->ExecNotificationQueryAsync(
            query_language.get(), start_query.get(), 0,
            nullptr, start_sink.get());
        if (FAILED(result)) {
            dispatch->deactivate();
            return std::unexpected(make_hresult_error(
                result, "subscribing to Win32_ProcessStartTrace failed"));
        }

        result = services->ExecNotificationQueryAsync(
            query_language.get(), stop_query.get(), 0,
            nullptr, stop_sink.get());
        if (FAILED(result)) {
            dispatch->deactivate();
            static_cast<void>(services->CancelAsyncCall(start_sink.get()));
            return std::unexpected(make_hresult_error(
                result, "subscribing to Win32_ProcessStopTrace failed"));
        }

        locator_ = std::move(locator);
        services_ = std::move(services);
        start_sink_ = std::move(start_sink);
        stop_sink_ = std::move(stop_sink);
        dispatch_ = std::move(dispatch);
        owner_thread_id_ = GetCurrentThreadId();
        owner_thread_handle_ = owner_thread.release();
        started_ = true;
        apartment_rollback.dismissed = true;
        if (const auto snapshot = emit_initial_snapshot(); !snapshot) {
            auto error = snapshot.error();
            stop();
            return std::unexpected(std::move(error));
        }
        if (!started_) {
            return std::unexpected(make_error(
                std::errc::operation_canceled,
                "WMI process event source was stopped during its snapshot"));
        }
        return {};
    }

    void stop() noexcept override {
        if (!started_) {
            return;
        }
        enforce_owner_thread();
        started_ = false;

        // Keep the state alive if stop() is called by the synchronous snapshot
        // callback. WMI sinks independently retain the same shared state while
        // asynchronous Indicate() calls are in progress.
        const auto dispatch = dispatch_;
        if (dispatch) {
            dispatch->deactivate();
        }
        if (services_.get() != nullptr) {
            if (start_sink_.get() != nullptr) {
                static_cast<void>(
                    services_->CancelAsyncCall(start_sink_.get()));
            }
            if (stop_sink_.get() != nullptr) {
                static_cast<void>(
                    services_->CancelAsyncCall(stop_sink_.get()));
            }
        }

        stop_sink_.reset();
        start_sink_.reset();
        services_.reset();
        locator_.reset();
        dispatch_.reset();
        release_com_apartment();
        CloseHandle(owner_thread_handle_);
        owner_thread_handle_ = nullptr;
        owner_thread_id_ = 0;
    }

private:
    std::expected<void, Error> emit_initial_snapshot() {
        const auto dispatch = dispatch_;
        if (!dispatch) {
            return std::unexpected(make_error(
                std::errc::operation_canceled,
                "WMI process event source has no dispatch state"));
        }

        const UniqueHandle snapshot{
            CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
        if (!snapshot.valid()) {
            return std::unexpected(make_windows_error(
                GetLastError(), "creating initial process snapshot failed"));
        }

        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);
        if (Process32FirstW(snapshot.get(), &process) == FALSE) {
            const auto error = GetLastError();
            if (error == ERROR_NO_MORE_FILES) {
                return {};
            }
            return std::unexpected(make_windows_error(
                error, "reading initial process snapshot failed"));
        }

        do {
            auto image_path = query_process_image_path(process.th32ProcessID);
            std::wstring process_name{process.szExeFile};
            if (process_name.empty() && !image_path.empty()) {
                process_name = image_path.filename().native();
            }
            dispatch->started(
                process.th32ProcessID, std::move(process_name),
                std::move(image_path), true);
            if (!started_) {
                return std::unexpected(make_error(
                    std::errc::operation_canceled,
                    "WMI process event source was stopped during its snapshot"));
            }
        } while (Process32NextW(snapshot.get(), &process) != FALSE);

        const auto error = GetLastError();
        if (error != ERROR_NO_MORE_FILES) {
            return std::unexpected(make_windows_error(
                error, "enumerating initial process snapshot failed"));
        }
        return {};
    }

    void release_com_apartment() noexcept {
        if (owns_com_apartment_) {
            CoUninitialize();
            owns_com_apartment_ = false;
        }
    }

    void enforce_owner_thread() const noexcept {
        if (started_
            && (owner_thread_id_ != GetCurrentThreadId()
                || owner_thread_handle_ == nullptr
                || WaitForSingleObject(owner_thread_handle_, 0)
                    != WAIT_TIMEOUT)) {
            // COM initialization is balanced per-thread, and these WMI
            // interfaces were obtained in the owner's apartment. Continuing
            // teardown on a different thread would be undefined and could
            // leave callbacks targeting destroyed state.
            std::terminate();
        }
    }

    ComPtr<IWbemLocator> locator_;
    ComPtr<IWbemServices> services_;
    ComPtr<IWbemObjectSink> start_sink_;
    ComPtr<IWbemObjectSink> stop_sink_;
    std::shared_ptr<DispatchState> dispatch_;
    HANDLE owner_thread_handle_{};
    DWORD owner_thread_id_{};
    bool owns_com_apartment_{};
    bool started_{};
};

} // namespace

std::unique_ptr<ProcessEventSource> make_wmi_process_event_source() {
    return std::make_unique<WmiProcessEventSource>();
}

} // namespace gsave::platform
