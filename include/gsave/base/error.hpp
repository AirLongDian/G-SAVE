#pragma once

#include <expected>
#include <string>
#include <system_error>
#include <utility>

namespace gsave {

struct Error final {
    std::error_code code;
    std::string context;

    [[nodiscard]] std::string message() const {
        if (context.empty()) {
            return code.message();
        }
        if (!code) {
            return context;
        }
        return context + ": " + code.message();
    }
};

[[nodiscard]] inline Error make_error(std::error_code code, std::string context) {
    return Error{code, std::move(context)};
}

[[nodiscard]] inline Error make_error(std::errc code, std::string context) {
    return make_error(std::make_error_code(code), std::move(context));
}

template <typename T>
using Result = std::expected<T, Error>;

using Status = Result<void>;

}  // namespace gsave
