#pragma once

#include <optional>
#include <string>

#include <bromaida/binary_info.hpp>
#include <bromaida/path.hpp>

/// Thin wrappers over the IDA SDK. Everything IDA-specific lives in src/ida;
/// the core never sees an IDA header.
namespace bromaida::ida {

/// What the open database holds. `platform` is empty if it isn't a Geometry Dash binary Broma knows.
[[nodiscard]] BinaryInfo read_binary_info();

/// Name of the input file ("GeometryDash.exe"), lower-case-insensitive comparisons are up to the caller.
[[nodiscard]] std::string root_filename();

/// False while auto-analysis is still running: touching the database then is unsafe.
[[nodiscard]] bool analysis_finished();

/// Folder the plugin ships its data in (`<folder of the plugin binary>/bromaida`).
[[nodiscard]] Path plugin_data_dir();

/// True in IDA's graphical version. The text version has no Qt and no dialogs of its own.
[[nodiscard]] bool is_gui();

/// Selects IDAClang's parser and returns its name; std::nullopt if this IDA doesn't have IDAClang.
/// The parser is called "clang" before IDA 9.2 and "old_clang" since the new parser took that name.
/// Selects IDA's C++ type parser. `preferred` ("clang" or "old_clang") is tried first; empty means automatic, which
/// prefers old_clang (it copes with the layout-only STL stand-ins) and falls back to clang. Returns the name of the one selected.
[[nodiscard]] std::optional<std::string> select_clang_parser(std::string_view preferred = {});

/// Routes the core's logger into IDA's Output window.
void install_log_sink();

}  // namespace bromaida::ida
