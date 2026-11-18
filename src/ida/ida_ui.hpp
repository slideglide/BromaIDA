#pragma once

#include <functional>
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

/// Everything the user sees goes through here. In IDA's GUI the dialogs are Qt widgets; in the text
/// version, or when IDA's Qt isn't the version the plugin was built against, IDA's own dialogs are used.

/// Modal question with two buttons; `Choice::First` is the left one and the default, unless `second_is_default`
/// (for a question whose first answer is the risky one). Closing the dialog counts as the second.
[[nodiscard]] Choice ask_two(
    std::string_view message, std::string_view first, std::string_view second, Icon icon = Icon::Question,
    bool second_is_default = false);
/// Same with three. Closing the dialog counts as the second.
[[nodiscard]] Choice ask_three(
    std::string_view message, std::string_view first, std::string_view second, std::string_view third,
    Icon icon = Icon::Question);

void show_warning(std::string_view message);
void show_info(std::string_view message);

/// Asks for the .bro file to export into. It may not exist yet, and nothing asks about overwriting it: the
/// exporter merges into what is there. std::nullopt when cancelled.
[[nodiscard]] std::optional<Path> ask_broma_file(std::string_view title);

/// Asks for the folder that holds the .bro files to import. std::nullopt when cancelled.
[[nodiscard]] std::optional<Path> ask_broma_folder(std::string_view title);

/// The window the plugin opens from. Settings can be opened from it without leaving it.
[[nodiscard]] MainAction show_main_dialog(
    std::string_view platform_name, bool platform_supported, std::function<void()> const& open_settings);

/// A profile the user can pick in the settings.
struct ProfileChoice {
    std::string name;
    std::string title;
};

/// The settings dialog, generated from kSettingDefinitions, plus the choice of profile. True (and `settings`
/// updated) on OK. The text version of IDA has no room for the profile list and keeps the current choice.
[[nodiscard]] bool edit_settings(Settings& settings, std::span<ProfileChoice const> profiles);

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
