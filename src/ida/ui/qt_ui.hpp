#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "ui/ida_ui.hpp"

namespace bromaida::ida::qt {
    [[nodiscard]] bool available();

    [[nodiscard]] std::size_t ask(
        std::string_view message, std::span<std::string_view const> buttons, Icon icon, std::size_t default_button = 0,
        std::string_view remember_label = {}, bool* remember = nullptr);
    void message(Icon icon, std::string_view text);
    [[nodiscard]] std::optional<Path> pick_broma_file(std::string_view title);
    [[nodiscard]] std::optional<Path> pick_folder(std::string_view title);
    [[nodiscard]] MainResult main_dialog(
        std::string_view platform_name, bool platform_supported, Settings& settings, bool idaclang_available,
        std::span<Profile const> profiles, MainTab initial_tab);
    void copyable_text(std::string_view title, std::string_view label, std::string_view text);
}  // namespace bromaida::ida::qt
