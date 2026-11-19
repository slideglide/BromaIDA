#include <bromaida/util/files.hpp>
#include <bromaida/config/settings.hpp>

#include <array>
#include <cstdlib>
#include <optional>

#include <asp/fs.hpp>
#include <fmt/format.h>
#include <glaze/glaze.hpp>

#include <bromaida/version.hpp>

namespace bromaida {
    std::string_view title(SettingGroup group) noexcept {
        switch (group) {
            case SettingGroup::General: return "General";
            case SettingGroup::Importer: return "Importer";
            case SettingGroup::Exporter: return "Exporter";
            case SettingGroup::Profile: return "Profile";
            case SettingGroup::Advanced: return "Advanced";
        }
        return {};
    }

    static constexpr auto kSettingDefinitions = std::to_array<SettingDefinition>({
        {"always_overwrite_merge_information", "Always Overwrite Function Comments with Merge Information",
         "Skip the prompt when merge information in function comments differs.",
         SettingGroup::General, &Settings::always_overwrite_merge_information},
        {"always_overwrite_idb", "Always Overwrite IDB",
         "Skip the prompt when the IDB and the Broma files disagree.\n"
         "Overwrites names and comments without asking.",
         SettingGroup::General, &Settings::always_overwrite_idb},
        {"disable_input_hash_check", "Disable Input File Hashes Check",
         "Skips the check that decides whether types need to be imported again.\n"
         "The generated header's hash is saved after a successful import; if identical, type import is skipped.",
         SettingGroup::Advanced, &Settings::disable_input_hash_check},
        {"export_return_types", "Export Return Types",
         "Exports return types, except 'TodoReturn'.\n"
         "Enable this only after types are imported.",
         SettingGroup::Exporter, &Settings::export_return_types},
        {"export_args_names", "Export Function Argument Names",
         "If function argument names should be exported.\n"
         "If enabled, only names that don't match \"a[0-9]+\" and \"p[0-9]+\" are exported.",
         SettingGroup::Exporter, &Settings::export_args_names},
        {"export_only_template_classes", "Only Export Classes Found In The Template",
         "Classes that don't exist in the selected .bro file (or anything it includes) are skipped.",
         SettingGroup::Exporter, &Settings::export_only_template_classes},
        {"ignore_unmarked_functions", "Ignore Unmarked Functions",
         "Ignore addresses that IDA has not marked as a function.\n"
         "If enabled, BromaIDA skips the binding and will not mark the address as a function.",
         SettingGroup::General, &Settings::ignore_unmarked_functions},
        {"debug_info", "Enable Debug Info", "Log extra information while importing and exporting.",
         SettingGroup::Advanced, &Settings::debug_info},
        {"import_types", "Import Types",
         "Imports the classes defined in the Broma files, plus bundled headers for "
         "Cocos2d-x and other components.\nGives IDA vtables and members so pseudocode resolves them.",
         SettingGroup::Importer, &Settings::import_types},
        {"set_default_parser_args", "Set Default Parser Arguments",
         "Sets IDAClang's parser arguments to automatically suit the current binary's platform.",
         SettingGroup::Importer, &Settings::set_default_parser_args},
        {"ignore_mismatched_structs", "Ignore Mismatched Structs",
         "Disable checking if BromaIDA's structs match from a previous import. (NOT RECOMMENDED)\n"
         "If enabled, no warning will be issued for importing types over previously imported types.\n"
         "Overwriting types can fail without an error.",
         SettingGroup::Importer, &Settings::ignore_mismatched_structs},
    });

    std::span<SettingDefinition const> setting_definitions() noexcept { return kSettingDefinitions; }

    static constexpr glz::opts kReadOptions{.error_on_unknown_keys = false};

    static std::optional<Path> env_path(char const* name) {
        if (auto const* value = std::getenv(name); value && *value) return Path{value};
        return std::nullopt;
    }

    static Path home_dir() {
        if (auto home = env_path("HOME")) return *home;
        return Path::home().unwrapOr(Path{"."});
    }

    Path user_config_dir() {
#if defined(_WIN32)
        auto base = env_path("LOCALAPPDATA").value_or(home_dir() / "AppData" / "Local");
#elif defined(__APPLE__)
        auto base = home_dir() / "Library" / "Application Support";
#else
        auto base = env_path("XDG_CONFIG_HOME").value_or(home_dir() / ".config");
#endif
        return base / kPluginName;
    }

    Path user_cache_dir() {
#if defined(_WIN32)
        auto base = env_path("LOCALAPPDATA").value_or(home_dir() / "AppData" / "Local");
        return base / kPluginName / "Cache";
#elif defined(__APPLE__)
        return home_dir() / "Library" / "Caches" / kPluginName;
#else
        return env_path("XDG_CACHE_HOME").value_or(home_dir() / ".cache") / kPluginName;
#endif
    }

    geode::Result<Settings, std::string> Settings::load(PathView file) {
        if (!file.exists()) return geode::Ok(Settings{});

        auto text = asp::fs::readToString(file);
        if (text.isErr())
            return geode::Err(fmt::format("cannot read {}: {}", file.string(), text.unwrapErr().message()));

        Settings settings;
        if (auto err = glz::read<kReadOptions>(settings, text.unwrap())) {
            return geode::Err(fmt::format("{}: {}", file.string(), glz::format_error(err, text.unwrap())));
        }
        return geode::Ok(std::move(settings));
    }

    geode::Result<void, std::string> Settings::save(PathView file) const {
        std::string json;
        if (auto err = glz::write<glz::opts{.prettify = true}>(*this, json)) {
            return geode::Err(fmt::format("cannot serialize settings: {}", glz::format_error(err, json)));
        }

        return write_atomic(file, json);
    }
}  // namespace bromaida
