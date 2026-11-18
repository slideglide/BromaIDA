#pragma once

#include <optional>
#include <string>

#include <Geode/Result.hpp>

#include <bromaida/binary_info.hpp>
#include <bromaida/path.hpp>
#include <bromaida/exporter.hpp>
#include <bromaida/settings.hpp>

namespace bromaida::ida {
    /// Reads the database into the form the core exporter takes.
    /// @param binary What the database holds.
    /// @param settings Decides what is read, such as return types and argument names.
    /// @return The classes and functions found.
    [[nodiscard]] ExtractedBinary extract_from_idb(BinaryInfo const& binary, Settings const& settings);

    struct ExportSummary {
        ExportStats stats;
        std::size_t files_written = 0;
    };

    /// Exports the database's bindings into a .bro file, merging into it if it exists.
    /// @param file Template to merge into. A missing file is created.
    /// @return What was written, or an error message.
    [[nodiscard]] geode::Result<ExportSummary, std::string> export_bindings(
        Settings const& settings, BinaryInfo const& binary, PathView file);

    /// Writes the Broma declaration of one function, e.g. for the clipboard.
    /// @param ea Function address.
    /// @return The declaration line, or an error message.
    [[nodiscard]] geode::Result<std::string, std::string> export_function(
        BinaryInfo const& binary, unsigned long long ea);
}  // namespace bromaida::ida
