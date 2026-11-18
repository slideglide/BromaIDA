#include <bromaida/text.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>

#include <asp/iter.hpp>

namespace bromaida::cpp {
    static bool is_ident_char(char c) noexcept {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == ':';
    }

    std::string lower_ascii(std::string_view text) {
        std::string out{text};
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    static bool is_space(char c) noexcept { return std::isspace(static_cast<unsigned char>(c)) != 0; }

    static std::string replace_gd_namespace(std::string text) {
        constexpr std::string_view from = "gd::";
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            if (text.compare(i, from.size(), from) == 0 && (i == 0 || !is_ident_char(text[i - 1]))) {
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
        if (type.size() < 8) return type;
        char const decorator = type.back();
        if (decorator != '*' && decorator != '&') return type;

        constexpr std::string_view east = " const";
        std::size_t const end = type.size() - 1;
        if (end < east.size() || type.compare(end - east.size(), east.size(), east) != 0) return type;

        std::string body = type.substr(0, end - east.size());
        if (body.contains("const*") || body.contains("const&")) return type;

        return "const " + body + decorator;
    }

    std::string_view trim(std::string_view text) noexcept {
        while (!text.empty() && is_space(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && is_space(text.back()))
            text.remove_suffix(1);
        return text;
    }

    std::string replace_all(std::string text, std::string_view from, std::string_view to) {
        if (from.empty()) return text;
        for (std::size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos; pos += to.size()) {
            text.replace(pos, from.size(), to);
        }
        return text;
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

    std::string to_ida_equivalent(std::string_view type) {
        return replace_all(replace_all(std::string{type}, "geode::", ""), "&", "*");
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

    std::string QualifiedName::scope() const {
        std::string out;
        for (auto const& ns : namespaces) {
            if (!out.empty()) out += "::";
            out += ns;
        }
        return out;
    }

    std::string QualifiedName::joined() const {
        auto out = scope();
        if (!out.empty()) out += "::";
        return out + bare;
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
        std::string out;
        for (auto const& ns : namespaces) {
            out += "namespace " + ns + " {";
            out += (&ns == &namespaces.back()) ? separator : ' ';
        }
        out += body;
        out += separator;
        for (std::size_t i = 0; i < namespaces.size(); ++i) {
            out += i + 1 == namespaces.size() ? "}" : "} ";
        }
        return out;
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
            if (i == 0 || !is_ident_char(type[i - 1])) {
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
