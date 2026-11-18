#pragma once

#include <span>
#include <string>

#include <Geode/Result.hpp>

#include <bromaida/binary_info.hpp>
#include <bromaida/path.hpp>
#include <bromaida/profile.hpp>
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

    /// Applies the Broma files of `source` (a folder or one .bro file) to the open database: types, then names,
    /// signatures and comments. `settings` may change: "always skip" turns type import off.
    [[nodiscard]] geode::Result<ImportSummary, std::string> import_bindings(
        Settings& settings, BinaryInfo const& binary, PathView source, PathView data_dir,
        std::span<Profile const> profiles);
}  // namespace bromaida::ida
