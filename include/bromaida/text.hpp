#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// General C++ type/name helpers shared by the whole project.
namespace bromaida::cpp {

inline constexpr std::array<std::string_view, 2> kTypeSpecifiers{"unsigned", "signed"};
inline constexpr std::array<std::string_view, 2> kTypeQualifiers{"const", "volatile"};

[[nodiscard]] std::string_view trim(std::string_view text) noexcept;
[[nodiscard]] std::string replace_all(std::string text, std::string_view from, std::string_view to);

/// Splits on `\n`, dropping a trailing `\r` from every line.
[[nodiscard]] std::vector<std::string> split_lines(std::string_view text);

/// Canonical spelling used everywhere inside BromaIDA:
///  - `gd::` becomes `std::`
///  - east const moves west (`T const*` -> `const T*`)
///  - pointers and references hug the type (`T *` -> `T*`)
///  - one space after every comma, none inside brackets
[[nodiscard]] std::string normalize_type(std::string_view type);

/// IDA prints pointers as `T *`.
[[nodiscard]] std::string format_ptr(std::string_view type);

/// IDA folds references into pointers, so `&` becomes `*` before comparing against IDA data.
[[nodiscard]] std::string to_ida_equivalent(std::string_view type);

/// Strips `const` and any number of trailing `*`/`&`.
[[nodiscard]] std::string strip_crp(std::string_view type);

/// Splits on commas that are not nested inside `<>` or `()`. `void`/empty yield nothing.
[[nodiscard]] std::vector<std::string> split_top_level(std::string_view list);

struct QualifiedName {
    std::vector<std::string> namespaces;
    std::string bare;

    [[nodiscard]] std::string joined() const;
    /// Everything but the last component, joined with `::`.
    [[nodiscard]] std::string scope() const;
};

/// `a::b::C` -> {{"a","b"}, "C"}. Only splits on `::` outside of template brackets.
[[nodiscard]] QualifiedName split_qualified(std::string_view name);

/// Wraps `body` in nested namespaces, either on one line or on separate lines.
[[nodiscard]] std::string wrap_namespaces(
    std::span<std::string const> namespaces, std::string_view body, bool single_line = false);

/// Some demangles contain impossible patterns like `*<`.
[[nodiscard]] bool looks_malformed(std::string_view demangled) noexcept;

/// Strips MSVC's `class`/`struct`/`enum` noise and normalizes.
[[nodiscard]] std::string clean_demangled_type(std::string_view type);

}  // namespace bromaida::cpp
