#include "settings_ui.hpp"

#include <algorithm>
#include <ranges>
#include <array>
#include <string>

#include <fmt/format.h>

#include <pro.h>
#include <kernwin.hpp>

namespace bromaida::ida {
    static constexpr std::array kGroups{
        SettingGroup::General, SettingGroup::Importer, SettingGroup::Exporter, SettingGroup::Advanced};

    static std::string sanitize(std::string_view text) {
        std::string out;
        out.reserve(text.size());
        for (char const c : text) {
            switch (c) {
                case '\n': out += ' '; break;
                case '#':
                case '<':
                case '>':
                case '%':
                case ':': break;
                default: out += c; break;
            }
        }
        return out;
    }

    bool edit_settings_native(Settings& settings) {
        namespace v = std::views;
        auto definitions_of = [](SettingGroup group) {
            return kSettingDefinitions | v::filter([group](auto const& def) { return def.group == group; });
        };

        std::array<ushort, kGroups.size()> masks{};
        std::string form = "BUTTON YES* OK\nBUTTON CANCEL Cancel\nBromaIDA Settings\n\n";

        for (auto const [g, group] : kGroups | v::enumerate) {
            auto definitions = definitions_of(group);
            auto const count = std::ranges::distance(definitions);

            for (auto const [index, def] : definitions | v::enumerate) {
                if (settings.*def.member) masks[g] |= static_cast<ushort>(1u << index);

                bool const first = index == 0;
                bool const last = index + 1 == count;
                form += fmt::format(
                    "<#{}#{}{}:C>{}\n", sanitize(def.tooltip),
                    first ? fmt::format("{}#", sanitize(title(group))) : std::string{}, sanitize(def.label),
                    last ? ">" : "");
            }
            form += "\n";
        }

        if (ask_form(form.c_str(), &masks[0], &masks[1], &masks[2], &masks[3]) <= 0) return false;

        for (auto const [g, group] : kGroups | v::enumerate) {
            for (auto const [index, def] : definitions_of(group) | v::enumerate) {
                settings.*def.member = (masks[g] >> index) & 1u;
            }
        }
        return true;
    }
}  // namespace bromaida::ida
