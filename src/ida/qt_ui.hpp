#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "ida_ui.hpp"

/// Qt implementation of ida_ui.hpp. No Qt header leaks out of qt_ui.cpp.
namespace bromaida::ida::qt {
    /// True if IDA's Qt has the major.minor the plugin was built against. Otherwise calls may hit missing symbols.
    [[nodiscard]] bool available();

    /// Index of the clicked button; closing returns 1. With `remember_label`, the checkbox state goes to `*remember`.
    [[nodiscard]] std::size_t ask(
        std::string_view message, std::span<std::string_view const> buttons, Icon icon, std::size_t default_button = 0,
        std::string_view remember_label = {}, bool* remember = nullptr);
    void message(Icon icon, std::string_view text);
    [[nodiscard]] std::optional<Path> pick_broma_file(std::string_view title);
    [[nodiscard]] std::optional<Path> pick_folder(std::string_view title);
    [[nodiscard]] MainResult main_dialog(
        std::string_view platform_name, bool platform_supported, Settings& settings, bool idaclang_available,
        std::span<ProfileChoice const> profiles, MainTab initial_tab);
    void copyable_text(std::string_view title, std::string_view label, std::string_view text);
}  // namespace bromaida::ida::qt
