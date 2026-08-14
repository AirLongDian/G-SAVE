#pragma once

#include "gsave/base/error.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gsave::repository {

struct MetadataRequest final {
    std::filesystem::path repository;
    std::filesystem::path parser;
    std::vector<std::string> changed_files;
};

// Runs the package parser in a memory/instruction-bounded Lua state.  Only
// repository-scoped reads plus the cryptographic primitives needed by package
// parsers are exposed; process, network and arbitrary filesystem APIs are not.
[[nodiscard]] Result<std::string> parse_metadata(const MetadataRequest& request);

}  // namespace gsave::repository
