#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "ida_ui.hpp"

/// The Qt implementation of the dialogs in ida_ui.hpp. Nothing here includes a Qt header, so the rest of the
/// plugin never sees Qt; only qt_ui.cpp does.
namespace bromaida::ida::qt {

/// True if IDA runs a Qt of the major.minor the plugin was compiled against. Anything else could call into
/// symbols IDA's Qt doesn't have, so the plugin falls back to IDA's own dialogs.
[[nodiscard]] bool available();

/// Index of the clicked button; closing the dialog returns 1.
[[nodiscard]] std::size_t ask(std::string_view message, std::span<std::string_view const> buttons, Icon icon);
void message(Icon icon, std::string_view text);
[[nodiscard]] std::optional<Path> pick_broma_file(std::string_view title);
[[nodiscard]] std::optional<Path> pick_folder(std::string_view title);
[[nodiscard]] MainAction main_dialog(
    std::string_view platform_name, bool platform_supported, std::function<void()> const& open_settings);
[[nodiscard]] bool settings_dialog(Settings& settings, bool idaclang_available);
void copyable_text(std::string_view title, std::string_view label, std::string_view text);

}  // namespace bromaida::ida::qt
