#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <bromaida/util/path.hpp>
#include <bromaida/config/settings.hpp>
#include <bromaida/typegen/profile.hpp>

namespace bromaida::ida {
    enum class Choice { First, Second, Third };
    enum class Icon { None, Info, Question, Warning, Error };
    enum class MainAction { Cancel, Import, Export };
    enum class MainTab { Import, Export, Settings, About };

    struct MainResult {
        MainAction action = MainAction::Cancel;
        bool settings_changed = false;
    };

    // All user interaction goes through here: Qt dialogs in IDA's GUI, IDA's own dialogs otherwise

    [[nodiscard]] Choice ask_two(
        std::string_view message, std::string_view first, std::string_view second, Icon icon = Icon::Question,
        bool second_is_default = false);
    [[nodiscard]] Choice ask_two_remember(
        std::string_view message, std::string_view first, std::string_view second, std::string_view remember_label,
        bool& remember, Icon icon = Icon::Question);
    [[nodiscard]] Choice ask_three(
        std::string_view message, std::string_view first, std::string_view second, std::string_view third,
        Icon icon = Icon::Question);

    void show_warning(std::string_view message);
    void show_info(std::string_view message);

    [[nodiscard]] std::optional<Path> ask_broma_file(std::string_view title);

    [[nodiscard]] std::optional<Path> ask_broma_folder(std::string_view title);

    [[nodiscard]] MainResult show_main_dialog(
        std::string_view platform_name, bool platform_supported, Settings& settings, std::span<Profile const> profiles,
        MainTab initial_tab = MainTab::Import);

    void show_copyable(std::string_view title, std::string_view label, std::string_view text);

    class WaitBox {
    public:
        explicit WaitBox(std::string_view message);
        ~WaitBox();
        WaitBox(WaitBox const&) = delete;
        WaitBox& operator=(WaitBox const&) = delete;
    };

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
