#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/path.hpp>

namespace bromaida {
    /// Files directly inside `folder` with the given extension (ignoring case), sorted.
    [[nodiscard]] geode::Result<std::vector<Path>, std::string> list_files(PathView folder, std::string_view extension);

    /// Creates the parent folders, writes a `.tmp` sibling and renames it over `file`.
    [[nodiscard]] geode::Result<void, std::string> write_atomic(PathView file, std::string_view content);
}  // namespace bromaida
