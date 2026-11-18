#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/path.hpp>

namespace bromaida {
    /// Lists the files directly inside a folder.
    /// @param folder Folder to look in; subfolders are not searched.
    /// @param extension Extension to match, ignoring case.
    /// @return The matching paths, sorted, or an error message if the folder can't be read.
    [[nodiscard]] geode::Result<std::vector<Path>, std::string> list_files(PathView folder, std::string_view extension);

    /// Writes a file atomically, creating parent folders as needed.
    /// @param file Destination.
    /// @param content Full new contents.
    /// @return Nothing, or an error message if the write failed.
    [[nodiscard]] geode::Result<void, std::string> write_atomic(PathView file, std::string_view content);
}  // namespace bromaida
