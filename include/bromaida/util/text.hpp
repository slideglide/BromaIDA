#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bromaida::cpp {

    [[nodiscard]] constexpr bool is_word_char(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    [[nodiscard]] constexpr bool is_qualified_char(char c) noexcept { return is_word_char(c) || c == ':'; }

    [[nodiscard]] std::string_view trim(std::string_view text) noexcept;
    [[nodiscard]] std::string lower_ascii(std::string_view text);
    [[nodiscard]] std::string replace_all(std::string text, std::string_view from, std::string_view to);

    [[nodiscard]] std::vector<std::string> split_lines(std::string_view text);

    [[nodiscard]] std::string normalize_type(std::string_view type);

    [[nodiscard]] std::string format_ptr(std::string_view type);

    [[nodiscard]] std::string strip_crp(std::string_view type);

    [[nodiscard]] std::vector<std::string> split_top_level(std::string_view list);

    struct QualifiedName {
        std::vector<std::string> namespaces;
        std::string bare;

        [[nodiscard]] std::string scope() const;
    };

    [[nodiscard]] QualifiedName split_qualified(std::string_view name);

    [[nodiscard]] std::string wrap_namespaces(
        std::span<std::string const> namespaces, std::string_view body, bool single_line = false);

    [[nodiscard]] bool is_generated_arg_name(std::string_view name) noexcept;

    [[nodiscard]] bool is_identifier_path(std::string_view name) noexcept;

    [[nodiscard]] std::string_view strip_numeric_suffix(std::string_view name) noexcept;

    struct CallParts {
        std::string_view name;
        std::string_view args;
    };

    [[nodiscard]] std::optional<CallParts> split_call(std::string_view text) noexcept;

    [[nodiscard]] bool looks_malformed(std::string_view demangled) noexcept;

    [[nodiscard]] std::string clean_demangled_type(std::string_view type);
}
