#include "ida_ui.hpp"

#include <array>
#include <string>

#include <fmt/format.h>

#include <bromaida/version.hpp>

#include "ida_env.hpp"
#include "qt_ui.hpp"
#include "settings_ui.hpp"

#include <pro.h>
#include <kernwin.hpp>

namespace bromaida::ida {
    static bool use_qt() {
        static bool const usable = is_gui() && qt::available();
        return usable;
    }

    static char const* native_icon(Icon icon) {
        switch (icon) {
            case Icon::None: return "NONE";
            case Icon::Info: return "INFO";
            case Icon::Question: return "QUESTION";
            case Icon::Warning: return "WARNING";
            case Icon::Error: return "ERROR";
        }
        return "NONE";
    }

    static int native_buttons(
        std::string_view message, char const* first, char const* second, char const* third, Icon icon,
        bool second_is_default = false) {
        std::string const text{message};
        std::string const header = fmt::format(
            "TITLE {}\nICON {}\nAUTOHIDE NONE\n{}{}", kPluginName, native_icon(icon),
            third == nullptr ? "HIDECANCEL\n" : "", second == nullptr ? "HIDENO\n" : "");
        return ask_buttons(
            first, second, third, second_is_default ? ASKBTN_NO : ASKBTN_YES, "%s%s", header.c_str(), text.c_str());
    }

    Choice ask_two(
        std::string_view message, std::string_view first, std::string_view second, Icon icon, bool second_is_default) {
        if (use_qt()) {
            std::array const buttons{first, second};
            return qt::ask(message, buttons, icon, second_is_default ? 1 : 0) == 0 ? Choice::First : Choice::Second;
        }
        std::string const a{first}, b{second};
        return native_buttons(message, a.c_str(), b.c_str(), nullptr, icon, second_is_default) == ASKBTN_YES
                   ? Choice::First
                   : Choice::Second;
    }

    Choice ask_two_remember(
        std::string_view message, std::string_view first, std::string_view second, std::string_view remember_label,
        bool& remember, Icon icon) {
        remember = false;
        if (use_qt()) {
            std::array const buttons{first, second};
            return qt::ask(message, buttons, icon, 0, remember_label, &remember) == 0 ? Choice::First : Choice::Second;
        }
        return ask_two(message, first, second, icon);
    }

    Choice ask_three(
        std::string_view message, std::string_view first, std::string_view second, std::string_view third, Icon icon) {
        if (use_qt()) {
            std::array const buttons{first, second, third};
            switch (qt::ask(message, buttons, icon)) {
                case 0: return Choice::First;
                case 2: return Choice::Third;
                default: return Choice::Second;
            }
        }
        std::string const a{first}, b{second}, c{third};
        switch (native_buttons(message, a.c_str(), b.c_str(), c.c_str(), icon)) {
            case ASKBTN_YES: return Choice::First;
            case ASKBTN_CANCEL: return Choice::Third;
            default: return Choice::Second;
        }
    }

    void show_warning(std::string_view message) {
        if (use_qt()) return qt::message(Icon::Warning, message);
        std::string const text{message};
        warning("%s", text.c_str());
    }

    void show_info(std::string_view message) {
        if (use_qt()) return qt::message(Icon::Info, message);
        (void)native_buttons(message, "OK", nullptr, nullptr, Icon::None);
    }

    std::optional<Path> ask_broma_file(std::string_view title) {
        if (use_qt()) return qt::pick_broma_file(title);

        std::string const caption{title};
        char const* path = ask_file(false, "GeometryDash.bro", "%s", caption.c_str());
        if (path == nullptr || *path == '\0') return std::nullopt;
        return Path{path};
    }

    std::optional<Path> ask_broma_folder(std::string_view title) {
        if (use_qt()) return qt::pick_folder(title);

        std::string const form =
            fmt::format("BUTTON YES* Done\nBUTTON CANCEL Cancel\n{}\n<{}:F::64::>", kPluginName, title);
        char folder[QMAXPATH] = {};
        if (ask_form(form.c_str(), folder) <= 0 || folder[0] == '\0') return std::nullopt;
        return Path{folder};
    }

    MainResult show_main_dialog(
        std::string_view platform_name, bool platform_supported, Settings& settings,
        std::span<ProfileChoice const> profiles, MainTab initial_tab) {
        bool const idaclang = select_clang_parser(settings.type_parser).has_value();
        if (use_qt()) {
            return qt::main_dialog(platform_name, platform_supported, settings, idaclang, profiles, initial_tab);
        }

        auto const edit_native = [&] {
            bool const accepted = edit_settings_native(settings);
            if (accepted && !idaclang) settings.import_types = false;
            return accepted;
        };
        if (initial_tab == MainTab::Settings) return {MainAction::Cancel, edit_native()};

        ushort choice = 0;
        std::string const form = fmt::format(
            "BUTTON YES* Run\nBUTTON CANCEL Cancel\n{}\n\n{} v{}.\n{}\nCurrent Platform: {}\n\nSelect Mode:\n"
            "<~I~mport:R>\n<~E~xport:R>\n<~S~ettings:R>>\n",
            kPluginName, kPluginName, kVersion, kPluginDescription, platform_supported ? platform_name : "unsupported");
        if (ask_form(form.c_str(), &choice) <= 0) return {};

        switch (choice) {
            case 0: return {MainAction::Import};
            case 1: return {MainAction::Export};
            default: return {MainAction::Cancel, edit_native()};
        }
    }

    void show_copyable(std::string_view title, std::string_view label, std::string_view text) {
        if (use_qt()) return qt::copyable_text(title, label, text);

        qstring value{std::string{text}.c_str()};
        std::string const prompt{label};
        ask_str(&value, HIST_CMT, "%s", prompt.c_str());
    }

    WaitBox::WaitBox(std::string_view message) {
        std::string const text{message};
        show_wait_box("HIDECANCEL\n%s", text.c_str());
    }

    WaitBox::~WaitBox() { hide_wait_box(); }

    TemporaryJump::TemporaryJump(unsigned long long ea)
        : m_origin(get_screen_ea()) {
        jumpto(static_cast<ea_t>(ea));
    }

    TemporaryJump::~TemporaryJump() { jumpto(static_cast<ea_t>(m_origin)); }
}  // namespace bromaida::ida
