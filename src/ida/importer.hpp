#pragma once

#include <string>

#include <Geode/Result.hpp>

#include <bromaida/binary_info.hpp>
#include <bromaida/path.hpp>
#include <bromaida/settings.hpp>

namespace bromaida::ida {

struct ImportSummary {
    bool types_imported = false;
    std::size_t bindings = 0;
    std::size_t resolved = 0;
    std::size_t duplicates = 0;
    std::size_t linked = 0;
    std::size_t linked_resolved = 0;
    Platform platform = Platform::Windows;
};

/// Reads the Broma files of `source` (a folder of .bro files, or a single .bro file) and applies them to the open
/// database:
/// types through IDAClang, then names, signatures and comments for every binding.
/// `settings` may change: answering "always skip" turns type import off.
[[nodiscard]] geode::Result<ImportSummary, std::string> import_bindings(
    Settings& settings, BinaryInfo const& binary, PathView source, PathView headers_dir);

}  // namespace bromaida::ida
