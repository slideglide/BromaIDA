#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bromaida::stl {
    /// Structured view of a C++ type with (possibly nested) STL template arguments.
    struct Node {
        std::string constness;  ///< "const" or empty.
        std::string name;       ///< The type itself, template arguments excluded.
        std::vector<Node> args;
        std::string ptr;  ///< Trailing `*`/`&` decorators.

        [[nodiscard]] bool is_stl() const noexcept { return name.contains("std::"); }
        friend bool operator==(Node const&, Node const&) = default;
    };

    /// Parses a *normalized* type string (see cpp::normalize_type).
    [[nodiscard]] Node parse(std::string_view type);

    /// `std::vector<int>` -> `std::vector<int, std::allocator<int>>`, the form IDA's libc++ headers need.
    [[nodiscard]] std::string expand(std::string_view type);

    /// Drops everything but the "sugar" template arguments (allocators, comparators, ...).
    [[nodiscard]] Node collapse(Node const& node);

    /// Renders a node back into source form.
    [[nodiscard]] std::string to_string(Node const& node);

    /// Undoes what demanglers do to STL types: drops libc++'s inline namespaces (`std::__ndk1::` -> `std::`)
    /// and folds default template arguments away, `basic_string<char, ...>` becoming `std::string`.
    [[nodiscard]] std::string sugar(std::string_view type);

    /// Leaf types of an STL type with whether each is held by value.
    /// `std::vector<Foo*>` -> {{"Foo", false}}.
    [[nodiscard]] std::vector<std::pair<std::string, bool>> value_types(std::string_view type);
}  // namespace bromaida::stl
