#include <bromaida/util/text.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <utility>

#include <asp/iter.hpp>
#include <fmt/format.h>

namespace bromaida::cpp {
    static constexpr std::string_view kSpace = " \t\n\r\f\v";

    static constexpr bool is_space(char c) noexcept { return kSpace.contains(c); }

    std::string lower_ascii(std::string_view text) {
        return text | std::views::transform([](unsigned char c) { return static_cast<char>(std::tolower(c)); }) |
               std::ranges::to<std::string>();
    }

    static std::string replace_gd_namespace(std::string text) {
        constexpr std::string_view from = "gd::";
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            if (std::string_view{text}.substr(i).starts_with(from) && (i == 0 || !is_qualified_char(text[i - 1]))) {
                out += "std::";
                i += from.size();
            } else {
                out += text[i++];
            }
        }
        return out;
    }

    static std::string collapse_whitespace(std::string_view text) {
        std::string out;
        out.reserve(text.size());
        bool pending_space = false;
        for (char c : text) {
            if (is_space(c)) {
                pending_space = true;
                continue;
            }
            if (pending_space && !out.empty()) {
                bool const glue_left = out.back() == '<' || out.back() == ' ';
                bool const glue_right = c == '>' || c == ',' || c == '*' || c == '&';
                if (!glue_left && !glue_right) out += ' ';
            }
            pending_space = false;
            out += c;
            if (c == ',') out += ' ';
        }
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    static std::string west_const(std::string type) {
        std::string_view view = type;
        if (view.size() < 8 || !(view.ends_with('*') || view.ends_with('&'))) return type;

        auto body = view.substr(0, view.size() - 1);
        if (!body.ends_with(" const")) return type;
        body.remove_suffix(6);
        if (body.contains("const*") || body.contains("const&")) return type;

        return fmt::format("const {}{}", body, view.back());
    }

    std::string_view trim(std::string_view text) noexcept {
        auto const first = text.find_first_not_of(kSpace);
        if (first == std::string_view::npos) return {};
        return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
    }

    std::string replace_all(std::string text, std::string_view from, std::string_view to) {
        if (from.empty()) return text;
        std::string out;
        out.reserve(text.size());
        bool first = true;
        for (auto const part : std::views::split(text, from)) {
            if (!std::exchange(first, false)) out += to;
            out.append(part.begin(), part.end());
        }
        return out;
    }

    std::vector<std::string> split_lines(std::string_view text) {
        return asp::iter::lines(text)
            .map([](std::string_view line) {
                if (line.ends_with('\r')) line.remove_suffix(1);
                return std::string{line};
            })
            .collect();
    }

    std::string normalize_type(std::string_view type) {
        auto text = collapse_whitespace(replace_gd_namespace(std::string{trim(type)}));
        return west_const(std::move(text));
    }

    std::string format_ptr(std::string_view type) {
        std::string out;
        out.reserve(type.size() + 2);
        for (std::size_t i = 0; i < type.size(); ++i) {
            if (type[i] == '*' && i > 0 && type[i - 1] != ' ' && type[i - 1] != '*') out += ' ';
            out += type[i];
        }
        return out;
    }

    std::string strip_crp(std::string_view type) {
        type = trim(type);
        while (!type.empty() && (type.back() == '*' || type.back() == '&')) {
            type.remove_suffix(1);
            type = trim(type);
        }
        if (type.starts_with("const ")) type.remove_prefix(6);
        if (type.ends_with(" const")) type.remove_suffix(6);
        return std::string{trim(type)};
    }

    std::vector<std::string> split_top_level(std::string_view list) {
        list = trim(list);
        if (list.empty() || list == "void") return {};

        std::vector<std::string> out;
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= list.size(); ++i) {
            char const c = i < list.size() ? list[i] : ',';
            if (c == '<' || c == '(') {
                ++depth;
            } else if (c == '>' || c == ')') {
                --depth;
            } else if (c == ',' && depth == 0) {
                out.emplace_back(trim(list.substr(start, i - start)));
                start = i + 1;
            }
        }
        return out;
    }

    std::string QualifiedName::scope() const { return asp::iter::from(namespaces).copied().join("::"); }

    bool is_generated_arg_name(std::string_view name) noexcept {
        return name.size() > 1 && (name[0] == 'p' || name[0] == 'a') &&
               name.find_first_not_of("0123456789", 1) == std::string_view::npos;
    }

    bool is_identifier_path(std::string_view name) noexcept {
        auto is_part = [&](auto part) {
            return !part.empty() && !std::isdigit(static_cast<unsigned char>(part.front())) &&
                   std::ranges::all_of(part, is_word_char);
        };
        return !name.empty() && std::ranges::all_of(std::views::split(name, std::string_view{"::"}), is_part);
    }

    std::string_view strip_numeric_suffix(std::string_view name) noexcept {
        auto const last_non_digit = name.find_last_not_of("0123456789");
        if (last_non_digit == std::string_view::npos || last_non_digit == 0 || last_non_digit + 1 == name.size() ||
            name[last_non_digit] != '_') {
            return name;
        }
        return name.substr(0, last_non_digit);
    }

    std::optional<CallParts> split_call(std::string_view text) noexcept {
        if (text.empty() || text.back() != ')') return std::nullopt;

        int depth = 0;
        std::size_t open = std::string_view::npos;
        for (std::size_t i = text.size(); i-- > 0;) {
            if (text[i] == ')') {
                ++depth;
            } else if (text[i] == '(' && --depth == 0) {
                open = i;
                break;
            }
        }
        if (open == std::string_view::npos || open == 0) return std::nullopt;

        auto const name = text.substr(0, open);
        if (std::ranges::any_of(name, is_space)) return std::nullopt;
        return CallParts{name, text.substr(open + 1, text.size() - open - 2)};
    }

    QualifiedName split_qualified(std::string_view name) {
        QualifiedName result;
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i < name.size(); ++i) {
            char const c = name[i];
            if (c == '<' || c == '(') {
                ++depth;
            } else if (c == '>' || c == ')') {
                --depth;
            } else if (c == ':' && depth == 0 && i + 1 < name.size() && name[i + 1] == ':') {
                result.namespaces.emplace_back(name.substr(start, i - start));
                start = i + 2;
                ++i;
            }
        }
        result.bare = std::string{name.substr(start)};
        return result;
    }

    std::string wrap_namespaces(std::span<std::string const> namespaces, std::string_view body, bool single_line) {
        if (namespaces.empty()) return std::string{body};

        char const separator = single_line ? ' ' : '\n';
        auto const opens = namespaces |
                           std::views::transform([](auto const& ns) { return fmt::format("namespace {} {{", ns); }) |
                           std::ranges::to<std::vector>();
        auto const closes = std::vector<std::string>(namespaces.size(), "}");
        return fmt::format(
            "{}{}{}{}{}", asp::iter::from(opens).copied().join(" "), separator, body, separator,
            asp::iter::from(closes).copied().join(" "));
    }

    bool looks_malformed(std::string_view demangled) noexcept {
        for (std::size_t i = 0; i < demangled.size(); ++i) {
            if (demangled[i] != '*' && demangled[i] != '&') continue;
            auto rest = demangled.substr(i + 1);
            while (!rest.empty() && is_space(rest.front()))
                rest.remove_prefix(1);
            if (rest.starts_with('<')) return true;
        }
        return false;
    }

    static constexpr auto kTagKeywords = std::to_array<std::string_view>({"class ", "struct ", "enum "});

    std::string clean_demangled_type(std::string_view type) {
        std::string out;
        out.reserve(type.size());
        for (std::size_t i = 0; i < type.size();) {
            bool skipped = false;
            if (i == 0 || !is_qualified_char(type[i - 1])) {
                for (auto const keyword : kTagKeywords) {
                    if (type.compare(i, keyword.size(), keyword) == 0) {
                        i += keyword.size();
                        while (i < type.size() && is_space(type[i]))
                            ++i;
                        skipped = true;
                        break;
                    }
                }
            }
            if (!skipped) out += type[i++];
        }
        return normalize_type(out);
    }
}  // namespace bromaida::cpp
