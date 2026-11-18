#pragma once

#include <optional>
#include <string>

#include <pro.h>

#include <bromaida/binary_info.hpp>
#include <bromaida/path.hpp>

/// Thin wrappers over the IDA SDK. The core never includes IDA headers.
namespace bromaida::ida {
    [[nodiscard]] inline std::string to_std(qstring const& s) { return {s.c_str(), s.length()}; }

    /// What the open database holds. `platform` is empty if it isn't a Geometry Dash binary Broma knows.
    [[nodiscard]] BinaryInfo read_binary_info();

    /// Input file name, e.g. "GeometryDash.exe".
    [[nodiscard]] std::string root_filename();

    /// False while auto-analysis is still running: touching the database then is unsafe.
    [[nodiscard]] bool analysis_finished();

    /// Folder the plugin ships its data in (`<folder of the plugin binary>/bromaida`).
    [[nodiscard]] Path plugin_data_dir();

    /// True in IDA's graphical version; the text version has no Qt.
    [[nodiscard]] bool is_gui();

    /// Selects IDA's C++ type parser: `preferred` first, else old_clang if present, else clang. std::nullopt without IDAClang.
    [[nodiscard]] std::optional<std::string> select_clang_parser(std::string_view preferred = {});

    /// Routes the core's logger into IDA's Output window.
    void install_log_sink();
}  // namespace bromaida::ida
