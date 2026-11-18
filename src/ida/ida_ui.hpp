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

    /// All user interaction: Qt dialogs in IDA's GUI, IDA's own dialogs otherwise.

    /// Modal question with two buttons; `Choice::First` is the default unless `second_is_default`. Closing counts as the second.
    [[nodiscard]] Choice ask_two(
        std::string_view message, std::string_view first, std::string_view second, Icon icon = Icon::Question,
        bool second_is_default = false);
    /// Same, with a checkbox labelled `remember_label`; its state goes to `remember` (always false in the text version).
    [[nodiscard]] Choice ask_two_remember(
        std::string_view message, std::string_view first, std::string_view second, std::string_view remember_label,
        bool& remember, Icon icon = Icon::Question);
    /// Same with three. Closing the dialog counts as the second.
    [[nodiscard]] Choice ask_three(
        std::string_view message, std::string_view first, std::string_view second, std::string_view third,
        Icon icon = Icon::Question);

    void show_warning(std::string_view message);
    void show_info(std::string_view message);

    /// Asks for the .bro file to export into; it may not exist yet. std::nullopt when cancelled.
    [[nodiscard]] std::optional<Path> ask_broma_file(std::string_view title);

    /// Asks for the folder that holds the .bro files to import. std::nullopt when cancelled.
    [[nodiscard]] std::optional<Path> ask_broma_folder(std::string_view title);

    /// A profile the user can pick in the settings.
    struct ProfileChoice {
        std::string name;
        std::string title;
    };

    /// The window the plugin opens from: import, export, settings and about tabs. Confirming applies the settings
    /// and runs the current tab's action. The text version has a mode list instead of tabs.
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
