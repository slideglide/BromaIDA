#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <asp/fs.hpp>
#include <asp/iter.hpp>
#include <fmt/format.h>

#include <bromaida/util/log.hpp>
#include <bromaida/typegen/profile.hpp>
#include <bromaida/config/settings.hpp>
#include <bromaida/version.hpp>

#include "plugin.hpp"

#include "exporter/exporter.hpp"
#include "env/ida_env.hpp"
#include "ui/ida_ui.hpp"
#include "importer/importer.hpp"

#include <pro.h>
#include <ida.hpp>
#include <idp.hpp>
#include <kernwin.hpp>
#include <loader.hpp>

namespace bromaida::ida {
    static constexpr auto kMenuPath = "Edit/Plugins/BromaIDA/";
    static constexpr auto kPopupPath = "BromaIDA/";
    static constexpr auto kExportFunctionAction = "bromaida:export_function";

    static constexpr Log log{"Plugin"};

    Plugin::Plugin() {
        install_log_sink();
        m_settings_file = user_config_dir() / "settings.json";
        if (auto loaded = Settings::load(m_settings_file)) {
            m_settings = std::move(loaded).unwrap();
        } else {
            // keep the broken file, else the next save would overwrite it with defaults
            auto backup = m_settings_file;
            backup += ".bad";
            asp::fs::rename(m_settings_file, backup);
            log.error("{}", loaded.unwrapErr());
            show_warning(
                fmt::format(
                    "BromaIDA couldn't read its settings, so defaults are used:\n{}\n\nThe file was kept as {}.",
                    loaded.unwrapErr(), backup.string()));
        }
        if (m_settings.debug_info) set_log_level(LogLevel::Debug);

        add_action("bromaida:import", "Import Broma bindings...", true, [this] { import_bindings(); });
        add_action("bromaida:export", "Export Broma bindings...", true, [this] { export_bindings(); });
        add_action(kExportFunctionAction, "Export current function", false, [this] { export_current_function(); });
        add_action("bromaida:settings", "Settings...", true, [this] { open_main(MainTab::Settings); });
        add_action("bromaida:about", "About", true, [this] { about(); });

        hook_event_listener(HT_UI, this);
        msg("%s v%s initialized.\n", kPluginName.data(), kVersion.data());
    }

    Plugin::~Plugin() {
        unhook_event_listener(HT_UI, this);
        for (auto const& name : m_menu_actions)
            detach_action_from_menu(kMenuPath, name.c_str());
        for (auto const& name : m_actions)
            unregister_action(name.c_str());
        msg("%s v%s unloaded.\n", kPluginName.data(), kVersion.data());
    }

    bool idaapi Plugin::run(size_t arg) {
        if (arg == 1 || arg == 2) {
            qstring file;
            if (!qgetenv("BROMAIDA_FILE", &file) || file.empty()) {
                msg("BromaIDA: set BROMAIDA_FILE to a .bro file first.\n");
                return false;
            }
            return arg == 1 ? import_bindings(Path{file.c_str()}) : export_bindings(Path{file.c_str()});
        }

        open_main(MainTab::Import);
        return true;
    }

    ssize_t idaapi Plugin::on_event(ssize_t code, va_list va) {
        if (code == ui_populating_widget_popup) {
            auto* widget = va_arg(va, TWidget*);
            auto* popup = va_arg(va, TPopupMenu*);
            auto const type = get_widget_type(widget);
            if (type == BWN_DISASM || type == BWN_PSEUDOCODE) {
                attach_action_to_popup(widget, popup, kExportFunctionAction, kPopupPath);
            }
        }
        return 0;
    }

    void Plugin::add_action(std::string name, char const* label, bool in_menu, asp::MoveOnlyFunction<void()> fn) {
        m_handlers.push_back(std::make_unique<FunctionAction>(std::move(fn)));
        action_desc_t const desc =
            ACTION_DESC_LITERAL_PLUGMOD(name.c_str(), label, m_handlers.back().get(), this, nullptr, nullptr, -1);
        if (!register_action(desc)) return;
        if (in_menu && attach_action_to_menu(kMenuPath, name.c_str(), SETMENU_APP)) m_menu_actions.push_back(name);
        m_actions.push_back(std::move(name));
    }

    void Plugin::reload_profiles() {
        m_profiles = load_profiles(plugin_data_dir() / "profiles");
        m_profiles.merge(load_profiles(user_config_dir() / "profiles"));
        for (auto const& error : m_profiles.errors)
            log.warn("Profile skipped: {}", error);
    }

    void Plugin::save_settings() {
        if (auto saved = m_settings.save(m_settings_file); !saved) {
            show_warning(fmt::format("BromaIDA could not save its settings:\n{}", saved.unwrapErr()));
        }
    }

    [[nodiscard]] bool Plugin::analysis_is_safe() const {
        if (analysis_finished()) return true;
        show_warning(
            fmt::format(
                "IDA analysis is still running.\n\n"
                "Modifying the database while analysis runs can corrupt it or leave types and symbols "
                "missing.\n\n"
                "Wait for the 'AU: idle' indicator at the bottom-left of IDA, then run {} again.",
                kPluginName));
        return false;
    }

    void Plugin::open_main(MainTab tab) {
        reload_profiles();
        auto const binary = read_binary_info();
        auto const result = show_main_dialog(
            binary.platform ? info(*binary.platform).printable : std::string_view{}, binary.platform.has_value(),
            m_settings, m_profiles.profiles, tab);
        if (result.settings_changed) {
            set_log_level(m_settings.debug_info ? LogLevel::Debug : LogLevel::Info);
            save_settings();
        }
        switch (result.action) {
            case MainAction::Import: import_bindings(); break;
            case MainAction::Export: export_bindings(); break;
            case MainAction::Cancel: break;
        }
    }

    void Plugin::import_bindings() {
        if (!analysis_is_safe()) return;
        if (auto folder = ask_broma_folder("Select folder containing .bro files")) import_bindings(*folder);
    }

    bool Plugin::import_bindings(PathView source) {
        if (!analysis_is_safe()) return false;
        auto const binary = read_binary_info();
        if (!binary.platform) {
            show_warning("No supported target platform was found for the currently open binary.");
            return false;
        }

        reload_profiles();
        auto summary = ida::import_bindings(m_settings, binary, source, plugin_data_dir(), m_profiles.profiles);
        save_settings();  // "Always Skip" changes a setting

        if (!summary) {
            log.error("{}", summary.unwrapErr());
            show_warning(summary.unwrapErr());
            return false;
        }
        auto const& done = summary.unwrap();
        show_info(
            fmt::format(
                "Finished importing {} bindings {}from Broma files.\n\nResolved {}/{} bindings.",
                info(done.platform).printable, done.types_imported ? "and types " : "", done.resolved, done.bindings));
        return true;
    }

    void Plugin::export_bindings() {
        if (!analysis_is_safe()) return;
        if (auto file = ask_broma_file("Select the Broma file to export into")) {
            export_bindings(*file);
        }
    }

    bool Plugin::export_bindings(PathView file) {
        if (!analysis_is_safe()) return false;
        auto const binary = read_binary_info();
        if (!binary.platform) {
            show_warning("No supported target platform was found for the currently open binary.");
            return false;
        }

        auto summary = ida::export_bindings(m_settings, binary, file);
        if (!summary) {
            log.error("{}", summary.unwrapErr());
            show_warning(summary.unwrapErr());
            return false;
        }
        auto const& stats = summary.unwrap().stats;
        auto const addresses = stats.bindings_added + stats.bindings_updated + stats.bindings_created;
        auto message = fmt::format(
            "Finished exporting to {} file(s):\n\n"
            "{} new classes with {} new methods\n"
            "{} addresses written ({} of them replacing an existing one)\n"
            "{} return types and {} argument names",
            summary.unwrap().files_written, stats.classes_created, stats.methods_created, addresses,
            stats.bindings_updated, stats.return_types, stats.param_names);
        if (binary.platform && is_android(*binary.platform) && stats.bindings_created == 0 &&
            stats.methods_created != 0) {
            message += "\n\nAndroid classes are linked by symbol, so they have no addresses to write.";
        }
        if (stats.classes_skipped != 0) {
            message += fmt::format("\n{} classes were skipped (see the Output window).", stats.classes_skipped);
        }
        log.info("{}", message);
        show_info(message);
        return true;
    }

    void Plugin::export_current_function() {
        auto const binary = read_binary_info();
        auto line = ida::export_function(binary, get_screen_ea());
        if (!line) {
            show_warning(line.unwrapErr());
            return;
        }
        log.info("{}", line.unwrap());
        show_copyable(kPluginName, "Broma declaration:", line.unwrap());
    }

    void Plugin::about() {
        show_info(fmt::format("{} {}\n\n{}\n{}", kPluginName, kVersion, kPluginDescription, kPluginGithub));
    }

    static plugmod_t* idaapi init() { return new Plugin; }
}  // namespace bromaida::ida

plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI,
    bromaida::ida::init,
    nullptr,
    nullptr,
    "Applies Broma bindings to a Geometry Dash database and exports them back",
    "Press Ctrl+Shift+B to import or export Broma bindings.",
    "BromaIDA",
    "Ctrl-Shift-B",
};
