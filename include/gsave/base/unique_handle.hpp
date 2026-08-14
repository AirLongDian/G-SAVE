#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <utility>

namespace gsave::base {

class unique_handle final {
public:
    using native_handle_type = HANDLE;

    constexpr unique_handle() noexcept = default;
    explicit constexpr unique_handle(native_handle_type handle) noexcept : handle_(handle) {}

    ~unique_handle() noexcept {
        reset();
    }

    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;

    constexpr unique_handle(unique_handle&& other) noexcept : handle_(other.release()) {}

    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] constexpr native_handle_type get() const noexcept {
        return handle_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_valid(handle_);
    }

    [[nodiscard]] constexpr native_handle_type release() noexcept {
        return std::exchange(handle_, nullptr);
    }

    void reset(native_handle_type replacement = nullptr) noexcept {
        const auto previous = std::exchange(handle_, replacement);
        if (is_valid(previous) && previous != replacement) {
            ::CloseHandle(previous);
        }
    }

    void swap(unique_handle& other) noexcept {
        std::swap(handle_, other.handle_);
    }

private:
    [[nodiscard]] static constexpr bool is_valid(native_handle_type handle) noexcept {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    native_handle_type handle_{nullptr};
};

inline void swap(unique_handle& left, unique_handle& right) noexcept {
    left.swap(right);
}

}  // namespace gsave::base

#endif  // defined(_WIN32)
