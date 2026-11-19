#pragma once

#include <optional>
#include <string>

#include <Geode/Result.hpp>

#include <bromaida/platform/binary_info.hpp>
#include <bromaida/util/path.hpp>
#include <bromaida/exporter/exporter.hpp>
#include <bromaida/config/settings.hpp>

namespace bromaida::ida {
    [[nodiscard]] ExtractedBinary extract_from_idb(BinaryInfo const& binary, Settings const& settings);

    struct ExportSummary {
        ExportStats stats;
        std::size_t files_written = 0;
    };

    [[nodiscard]] geode::Result<ExportSummary, std::string> export_bindings(
        Settings const& settings, BinaryInfo const& binary, PathView file);

    [[nodiscard]] geode::Result<std::string, std::string> export_function(
        BinaryInfo const& binary, unsigned long long ea);
}  // namespace bromaida::ida
