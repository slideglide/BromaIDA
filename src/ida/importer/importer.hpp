#pragma once

#include <span>
#include <string>

#include <Geode/Result.hpp>

#include <bromaida/platform/binary_info.hpp>
#include <bromaida/util/path.hpp>
#include <bromaida/typegen/profile.hpp>
#include <bromaida/config/settings.hpp>

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

    [[nodiscard]] geode::Result<ImportSummary, std::string> import_bindings(
        Settings& settings, BinaryInfo const& binary, PathView source, PathView data_dir,
        std::span<Profile const> profiles);
}  // namespace bromaida::ida
