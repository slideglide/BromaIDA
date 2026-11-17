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

/// Decided once: the GUI and a Qt of the version we were built against.
static bool use_qt() {
    static bool const usable = is_gui() && qt::available();
    return usable;
}

// ---- IDA's own dialogs (text version, or a Qt we can't trust) --------------------------------------------

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

/// ask_buttons with the plugin's title and icon. Returns an ASKBTN_ code.
static int native_buttons(
    std::string_view message, char const* first, char const* second, char const* third, Icon icon) {
    std::string const text{message};
    std::string const header = fmt::format(
        "TITLE {}\nICON {}\nAUTOHIDE NONE\n{}{}", kPluginName, native_icon(icon),
        third == nullptr ? "HIDECANCEL\n" : "", second == nullptr ? "HIDENO\n" : "");
    return ask_buttons(first, second, third, ASKBTN_YES, "%s%s", header.c_str(), text.c_str());
}

Choice ask_two(std::string_view message, std::string_view first, std::string_view second, Icon icon) {
    if (use_qt()) {
        std::array const buttons{first, second};
        return qt::ask(message, buttons, icon) == 0 ? Choice::First : Choice::Second;
    }
    std::string const a{first}, b{second};
    return native_buttons(message, a.c_str(), b.c_str(), nullptr, icon) == ASKBTN_YES ? Choice::First : Choice::Second;
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
    // one OK button, like a plain message box
    (void)native_buttons(message, "OK", nullptr, nullptr, Icon::None);
}

std::optional<Path> ask_broma_file(std::string_view title) {
    if (use_qt()) return qt::pick_broma_file(title);

    std::string const caption{title};
    // for_saving stays false: the file has to be read first, and it may not exist yet
    char const* path = ask_file(false, "GeometryDash.bro", "%s", caption.c_str());
    if (path == nullptr || *path == '\0') return std::nullopt;
    return Path{path};
}

std::optional<Path> ask_broma_folder(std::string_view title) {
    if (use_qt()) return qt::pick_folder(title);

    // IDA has no folder picker of its own; a form with a path field does the job (the Python version used the same)
    std::string const form =
        fmt::format("BUTTON YES* Done\nBUTTON CANCEL Cancel\n{}\n<{}:F::64::>", kPluginName, title);
    char folder[QMAXPATH] = {};
    if (ask_form(form.c_str(), folder) <= 0 || folder[0] == '\0') return std::nullopt;
    return Path{folder};
}

MainAction show_main_dialog(
    std::string_view platform_name, bool platform_supported, std::function<void()> const& open_settings) {
    if (use_qt()) return qt::main_dialog(platform_name, platform_supported, open_settings);

    // the text version has no buttons to put "Settings" next to Import and Export: a choice list does
    ushort choice = 0;
    std::string const form = fmt::format(
        "BUTTON YES* Run\nBUTTON CANCEL Cancel\n{}\n\n{} v{}.\n{}\nCurrent Platform: {}\n\nSelect Mode:\n"
        "<~I~mport:R>\n<~E~xport:R>\n<~S~ettings:R>>\n",
        kPluginName, kPluginName, kVersion, kPluginDescription, platform_supported ? platform_name : "unsupported");
    if (ask_form(form.c_str(), &choice) <= 0) return MainAction::Cancel;

    switch (choice) {
        case 0: return MainAction::Import;
        case 1: return MainAction::Export;
        default: open_settings(); return MainAction::Cancel;
    }
}

bool edit_settings(Settings& settings) {
    bool const idaclang = select_clang_parser().has_value();
    if (use_qt()) return qt::settings_dialog(settings, idaclang);

    bool const accepted = edit_settings_native(settings);
    if (accepted && !idaclang) settings.import_types = false;
    return accepted;
}

void show_copyable(std::string_view title, std::string_view label, std::string_view text) {
    if (use_qt()) return qt::copyable_text(title, label, text);

    qstring value{std::string{text}.c_str()};
    std::string const prompt{label};
    ask_str(&value, HIST_CMT, "%s", prompt.c_str());
}

// ---- wait box and jump ---------------------------------------------------------------------------------

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
