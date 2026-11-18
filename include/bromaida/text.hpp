#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bromaida::cpp {
    inline constexpr std::array<std::string_view, 2> kTypeSpecifiers{"unsigned", "signed"};
    inline constexpr std::array<std::string_view, 2> kTypeQualifiers{"const", "volatile"};

    [[nodiscard]] std::string_view trim(std::string_view text) noexcept;
    [[nodiscard]] std::string lower_ascii(std::string_view text);
    [[nodiscard]] std::string replace_all(std::string text, std::string_view from, std::string_view to);

    /// Splits text into lines.
    /// @param text Text with `\n` or `\r\n` line endings.
    /// @return One entry per line, without the line ending.
    [[nodiscard]] std::vector<std::string> split_lines(std::string_view text);

    /// Rewrites a type into the spelling BromaIDA uses internally:
    ///  - `gd::` becomes `std::`
    ///  - east const moves west (`T const*` -> `const T*`)
    ///  - pointers and references hug the type (`T *` -> `T*`)
    ///  - one space after every comma, none inside brackets
    /// @param type A type as written in Broma or printed by a demangler.
    /// @return The normalized type.
    [[nodiscard]] std::string normalize_type(std::string_view type);

    /// Spaces a normalized type the way IDA prints it.
    /// @param type A normalized type.
    /// @return The type with pointers written as `T *`.
    [[nodiscard]] std::string format_ptr(std::string_view type);

    /// Prepares a type for comparison with IDA's, which folds references into pointers.
    /// @param type A normalized type.
    /// @return The type with every `&` replaced by `*`.
    [[nodiscard]] std::string to_ida_equivalent(std::string_view type);

    /// @param type A normalized type.
    /// @return The type without `const` and without trailing `*` or `&`.
    [[nodiscard]] std::string strip_crp(std::string_view type);

    /// Splits an argument or template argument list on commas outside `<>` and `()`.
    /// @param list The text between the brackets.
    /// @return The items, or nothing for an empty list or `void`.
    [[nodiscard]] std::vector<std::string> split_top_level(std::string_view list);

    struct QualifiedName {
        std::vector<std::string> namespaces;
        std::string bare;

        [[nodiscard]] std::string joined() const;
        /// Everything but the last component, joined with `::`.
        [[nodiscard]] std::string scope() const;
    };

    /// Splits `a::b::C` into {{"a","b"}, "C"}. Ignores `::` inside template brackets.
    /// @param name A possibly qualified name.
    /// @return The namespaces and the bare name.
    [[nodiscard]] QualifiedName split_qualified(std::string_view name);

    /// Wraps `body` in nested namespaces.
    /// @param namespaces Namespaces to open, in order.
    /// @param body Text to put inside.
    /// @param single_line Keep the body and the closing braces on the same line as the namespaces.
    /// @return The wrapped text.
    [[nodiscard]] std::string wrap_namespaces(
        std::span<std::string const> namespaces, std::string_view body, bool single_line = false);

    /// @param name An argument name.
    /// @return True for IDA's generated names: `p` or `a` followed by digits.
    [[nodiscard]] bool is_generated_arg_name(std::string_view name) noexcept;

    /// @param name Text to check.
    /// @return True for identifiers joined by `::` with no empty parts, such as `A` or `A::B_c`.
    [[nodiscard]] bool is_identifier_path(std::string_view name) noexcept;

    /// Removes the `_<digits>` suffix that rename retries add.
    /// @param name A symbol name.
    /// @return `name` without the suffix, or unchanged if it has none.
    [[nodiscard]] std::string_view strip_numeric_suffix(std::string_view name) noexcept;

    struct CallParts {
        std::string_view name;
        std::string_view args;
    };

    /// Splits `name(args)` at the paren matching the final `)`.
    /// @param text A call or declaration such as `foo(int, char)`.
    /// @return The name and the text between the parens, or nothing if `text` has another shape
    ///         (the name must be non-empty and contain no spaces).
    [[nodiscard]] std::optional<CallParts> split_call(std::string_view text) noexcept;

    /// @param demangled A demangler's output.
    /// @return True if it contains a pattern no valid type has, like `*<`.
    [[nodiscard]] bool looks_malformed(std::string_view demangled) noexcept;

    /// Removes MSVC's `class`/`struct`/`enum` prefixes from a demangled type and normalizes it.
    /// @param type A demangled type.
    /// @return The cleaned type.
    [[nodiscard]] std::string clean_demangled_type(std::string_view type);
}  // namespace bromaida::cpp
