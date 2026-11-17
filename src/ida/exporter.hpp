#pragma once

#include <optional>
#include <string>

#include <Geode/Result.hpp>

#include <bromaida/binary_info.hpp>
#include <bromaida/path.hpp>
#include <bromaida/exporter.hpp>
#include <bromaida/settings.hpp>

namespace bromaida::ida {

/// What the database holds, ready for the core exporter.
[[nodiscard]] ExtractedBinary extract_from_idb(BinaryInfo const& binary, Settings const& settings);

struct ExportSummary {
    ExportStats stats;
    std::size_t files_written = 0;
};

/// Exports the open database's bindings into `file`: an existing .bro (and everything it includes) is used as
/// the template, a missing one is created.
[[nodiscard]] geode::Result<ExportSummary, std::string> export_bindings(
    Settings const& settings, BinaryInfo const& binary, PathView file);

/// A Broma line for the function at `ea`, e.g. for the clipboard.
[[nodiscard]] geode::Result<std::string, std::string> export_function(BinaryInfo const& binary, unsigned long long ea);

}  // namespace bromaida::ida
