#pragma once

#include <array>
#include <string>
#include <string_view>

#include <Geode/Result.hpp>

#include <bromaida/path.hpp>

namespace bromaida {
    /// User-facing options. Plain data, no globals.
    struct Settings {
        // general
        bool always_overwrite_merge_information = false;
        bool always_overwrite_idb = false;
        bool disable_input_hash_check = false;
        bool ignore_unmarked_functions = true;
        bool debug_info = false;

        // exporter
        bool export_return_types = false;
        bool export_args_names = false;
        bool export_only_template_classes = false;

        // type import
        bool import_types = true;
        bool set_default_parser_args = true;
        bool ignore_mismatched_structs = false;

        /// Profile for type import (see profile.hpp). Empty picks one from the bindings folder's version.
        std::string profile;

        /// IDA's type parser: "clang" (the new one), "old_clang", or empty for automatic: old_clang if it exists.
        std::string type_parser;

        [[nodiscard]] static geode::Result<Settings, std::string> load(PathView file);
        [[nodiscard]] geode::Result<void, std::string> save(PathView file) const;

        friend bool operator==(Settings const&, Settings const&) = default;
    };

    /// The settings tabs.
    enum class SettingGroup : unsigned char { General, Importer, Exporter, Profile, Advanced };

    [[nodiscard]] constexpr std::string_view title(SettingGroup group) noexcept {
        switch (group) {
            case SettingGroup::General: return "General";
            case SettingGroup::Importer: return "Importer";
            case SettingGroup::Exporter: return "Exporter";
            case SettingGroup::Profile: return "Profile";
            case SettingGroup::Advanced: return "Advanced";
        }
        return {};
    }

    /// Describes one boolean setting; the UI is generated from this table.
    struct SettingDefinition {
        std::string_view key;
        std::string_view label;
        std::string_view tooltip;
        SettingGroup group;
        bool Settings::* member;
    };

    inline constexpr auto kSettingDefinitions = std::to_array<SettingDefinition>({
        {"always_overwrite_merge_information", "Always Overwrite Function Comments with Merge Information",
         "Do not prompt when there is a mismatch in merge information stored in function comments.",
         SettingGroup::General, &Settings::always_overwrite_merge_information},
        {"always_overwrite_idb", "Always Overwrite IDB",
         "Do not prompt when there is a mismatch between IDB and the Broma files.\n"
         "BromaIDA will silently overwrite the IDB.",
         SettingGroup::General, &Settings::always_overwrite_idb},
        {"disable_input_hash_check", "Disable Input File Hashes Check",
         "Skips the check that decides whether types need to be imported again.\n"
         "The generated header's hash is saved after a successful import; if identical, type import is skipped.",
         SettingGroup::Advanced, &Settings::disable_input_hash_check},
        {"export_return_types", "Export Return Types",
         "If return types should be exported.\nIf enabled, only types that aren't 'TodoReturn' are exported.\n"
         "Because of that, it is only recommended to enable this if types are already imported beforehand.",
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
         "Enables importing of the defined classes from the input Broma files, alongside some pre-made headers for "
         "Cocos2d-x and other components.\nWill yield VTables and members for IDA to digest and resolve in pseudocode.",
         SettingGroup::Importer, &Settings::import_types},
        {"set_default_parser_args", "Set Default Parser Arguments",
         "Sets IDAClang's parser arguments to automatically suit the current binary's platform.",
         SettingGroup::Importer, &Settings::set_default_parser_args},
        {"ignore_mismatched_structs", "Ignore Mismatched Structs",
         "Disable checking if BromaIDA's structs match from a previous import. (NOT RECOMMENDED)\n"
         "If enabled, no warning will be issued for importing types over previously imported types.\n"
         "Overwriting types may potentially silently fail.",
         SettingGroup::Importer, &Settings::ignore_mismatched_structs},
    });

    /// Per-user configuration folder (XDG_CONFIG_HOME, ~/Library/Application Support, %LOCALAPPDATA%).
    [[nodiscard]] Path user_config_dir();
    /// Per-user cache folder, for generated headers.
    [[nodiscard]] Path user_cache_dir();
}  // namespace bromaida
