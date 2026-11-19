#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

#include <Geode/Result.hpp>

#include <bromaida/util/path.hpp>

namespace bromaida {
    struct Settings {
        bool always_overwrite_merge_information = false;
        bool always_overwrite_idb = false;
        bool disable_input_hash_check = false;
        bool ignore_unmarked_functions = true;
        bool debug_info = false;

        bool export_return_types = false;
        bool export_args_names = false;
        bool export_only_template_classes = false;

        bool import_types = true;
        bool set_default_parser_args = true;
        bool ignore_mismatched_structs = false;

        std::string profile;

        std::string type_parser;

        [[nodiscard]] static geode::Result<Settings, std::string> load(PathView file);
        [[nodiscard]] geode::Result<void, std::string> save(PathView file) const;

        friend bool operator==(Settings const&, Settings const&) = default;
    };

    enum class SettingGroup : unsigned char { General, Importer, Exporter, Profile, Advanced };

    [[nodiscard]] std::string_view title(SettingGroup group) noexcept;

    struct SettingDefinition {
        std::string_view key;
        std::string_view label;
        std::string_view tooltip;
        SettingGroup group;
        bool Settings::* member;
    };

    [[nodiscard]] std::span<SettingDefinition const> setting_definitions() noexcept;

    [[nodiscard]] Path user_config_dir();

    [[nodiscard]] Path user_cache_dir();
}
