#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <bromaida/path.hpp>
#include <bromaida/settings.hpp>

namespace bromaida::ida {
    enum class Choice { First, Second, Third };
    enum class Icon { None, Info, Question, Warning, Error };
    enum class MainAction { Cancel, Import, Export };
    enum class MainTab { Import, Export, Settings, About };

    struct MainResult {
        MainAction action = MainAction::Cancel;
        /// The user confirmed edited `settings`, whatever the action. The caller should save them.
        bool settings_changed = false;
    };

    // All user interaction goes through here: Qt dialogs in IDA's GUI, IDA's own dialogs otherwise

    /// Modal question with two buttons. Closing the dialog counts as the second.
    /// @param second_is_default Make the second button the default instead of the first.
    /// @return The button clicked.
    [[nodiscard]] Choice ask_two(
        std::string_view message, std::string_view first, std::string_view second, Icon icon = Icon::Question,
        bool second_is_default = false);
    /// Same, with a checkbox.
    /// @param remember_label Text next to the checkbox.
    /// @param remember Receives the checkbox state; always false in the text version.
    [[nodiscard]] Choice ask_two_remember(
        std::string_view message, std::string_view first, std::string_view second, std::string_view remember_label,
        bool& remember, Icon icon = Icon::Question);
    /// Same with three buttons. Closing the dialog counts as the second.
    [[nodiscard]] Choice ask_three(
        std::string_view message, std::string_view first, std::string_view second, std::string_view third,
        Icon icon = Icon::Question);

    void show_warning(std::string_view message);
    void show_info(std::string_view message);

    /// Asks for the .bro file to export into. It may not exist yet.
    /// @return The path, or nothing if the user cancelled.
    [[nodiscard]] std::optional<Path> ask_broma_file(std::string_view title);

    /// Asks for the folder that holds the .bro files to import.
    /// @return The path, or nothing if the user cancelled.
    [[nodiscard]] std::optional<Path> ask_broma_folder(std::string_view title);

    struct ProfileChoice {
        std::string name;
        std::string title;
    };

    /// The window the plugin opens from: import, export, settings and about tabs. The text version has a mode list
    /// instead of tabs.
    /// @param platform_name Platform detected from the binary, for display.
    /// @param platform_supported False disables the actions.
    /// @param settings Edited in place when the user confirms.
    /// @param profiles Choices for the profile setting.
    /// @param initial_tab Tab shown first.
    /// @return The action to run (the current tab's) and whether `settings` changed.
    [[nodiscard]] MainResult show_main_dialog(
        std::string_view platform_name, bool platform_supported, Settings& settings,
        std::span<ProfileChoice const> profiles, MainTab initial_tab = MainTab::Import);

    /// A line of text the user will want to copy.
    void show_copyable(std::string_view title, std::string_view label, std::string_view text);

    /// Shows IDA's wait box for as long as it lives.
    class WaitBox {
    public:
        explicit WaitBox(std::string_view message);
        ~WaitBox();
        WaitBox(WaitBox const&) = delete;
        WaitBox& operator=(WaitBox const&) = delete;
    };

    /// Jumps to an address and returns to where the user was on destruction.
    class TemporaryJump {
    public:
        explicit TemporaryJump(unsigned long long ea);
        ~TemporaryJump();
        TemporaryJump(TemporaryJump const&) = delete;
        TemporaryJump& operator=(TemporaryJump const&) = delete;

    private:
        unsigned long long m_origin;
    };
}  // namespace bromaida::ida
