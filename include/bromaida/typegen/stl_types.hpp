#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bromaida::stl {
    struct Node {
        std::string constness;
        std::string name;
        std::vector<Node> args;
        std::string ptr;

        [[nodiscard]] bool is_stl() const noexcept { return name.contains("std::"); }
        friend bool operator==(Node const&, Node const&) = default;
    };

    [[nodiscard]] Node parse(std::string_view type);

    [[nodiscard]] std::string expand(std::string_view type);

    [[nodiscard]] Node collapse(Node const& node);

    [[nodiscard]] std::string to_string(Node const& node);

    [[nodiscard]] std::string sugar(std::string_view type);

    [[nodiscard]] std::vector<std::pair<std::string, bool>> value_types(std::string_view type);
}
