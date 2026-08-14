#pragma once

#include "gsave/gui/gui_model.hpp"

#include <filesystem>

namespace gsave::gui {

// Extracts one support package into an already-created temporary directory.
// The caller owns the directory lifetime and must keep it alive until install()
// has copied the validated package to the stable configuration directory.
[[nodiscard]] Result<PackageManifest> extract_package_archive(
    const std::filesystem::path& archive,
    const std::filesystem::path& destination);

}  // namespace gsave::gui
