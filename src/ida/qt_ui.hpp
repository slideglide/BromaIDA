#pragma once

#include <asp/detail/Function.hpp>
#include <optional>
#include <span>
#include <string_view>

#include "ida_ui.hpp"

/// Qt implementation of ida_ui.hpp. No Qt header leaks out of qt_ui.cpp.
namespace bromaida::ida::qt {
    /// True if IDA's Qt has the major.minor the plugin was built against; otherwise it could call missing symbols.
    [[nodiscard]] bool available();

    /// Index of the clicked button; closing the dialog returns 1. With `remember_label` the dialog has a checkbox
    /// whose state is stored in `*remember`.
    [[nodiscard]] std::size_t ask(
        std::string_view message, std::span<std::string_view const> buttons, Icon icon, std::size_t default_button = 0,
        std::string_view remember_label = {}, bool* remember = nullptr);
    void message(Icon icon, std::string_view text);
    [[nodiscard]] std::optional<Path> pick_broma_file(std::string_view title);
    [[nodiscard]] std::optional<Path> pick_folder(std::string_view title);
    [[nodiscard]] MainAction main_dialog(
        std::string_view platform_name, bool platform_supported, asp::FunctionRef<void()> open_settings);
    [[nodiscard]] bool settings_dialog(
        Settings& settings, bool idaclang_available, std::span<ProfileChoice const> profiles);
    void copyable_text(std::string_view title, std::string_view label, std::string_view text);
}  // namespace bromaida::ida::qt
