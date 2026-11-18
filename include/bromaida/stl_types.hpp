#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bromaida::stl {
    /// A C++ type with its (possibly nested) STL template arguments.
    struct Node {
        std::string constness;  ///< "const" or empty.
        std::string name;       ///< The type itself, template arguments excluded.
        std::vector<Node> args;
        std::string ptr;  ///< Trailing `*`/`&` decorators.

        [[nodiscard]] bool is_stl() const noexcept { return name.contains("std::"); }
        friend bool operator==(Node const&, Node const&) = default;
    };

    /// @param type A type already normalized with cpp::normalize_type.
    /// @return The parsed type.
    [[nodiscard]] Node parse(std::string_view type);

    /// Writes out the default template arguments: `std::vector<int>` becomes
    /// `std::vector<int, std::allocator<int>>`, the form IDA's libc++ headers need.
    /// @param type A normalized type.
    /// @return The expanded type.
    [[nodiscard]] std::string expand(std::string_view type);

    /// @return The node without its "sugar" template arguments (allocators, comparators, ...).
    [[nodiscard]] Node collapse(Node const& node);

    /// @return The node written back as source.
    [[nodiscard]] std::string to_string(Node const& node);

    /// Undoes what demanglers do to STL types: drops libc++'s inline namespaces (`std::__ndk1::` -> `std::`)
    /// and folds default template arguments away, `basic_string<char, ...>` becoming `std::string`.
    /// @param type A demangled type.
    /// @return The type as written in source.
    [[nodiscard]] std::string sugar(std::string_view type);

    /// Finds the types an STL type holds. `std::vector<Foo*>` gives {{"Foo", false}}.
    /// @param type A normalized type.
    /// @return Each leaf type, with true if it is held by value.
    [[nodiscard]] std::vector<std::pair<std::string, bool>> value_types(std::string_view type);
}  // namespace bromaida::stl
