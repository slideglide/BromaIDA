#include <bromaida/typegen/stl_types.hpp>

#include <array>
#include <optional>

#include <asp/iter.hpp>

#include <bromaida/util/text.hpp>

namespace bromaida::stl {
    struct Expansion {
        std::string_view name;
        std::string_view pattern;
        std::size_t arity;
    };

    static constexpr std::array kExpansions{
        Expansion{"std::map", "std::map<{0}, {1}, std::less<{0}>, std::allocator<std::pair<const {0}, {1}>>>", 2},
        Expansion{
            "std::unordered_map",
            "std::unordered_map<{0}, {1}, std::hash<{0}>, std::equal_to<{0}>, std::allocator<std::pair<const {0}, "
            "{1}>>>",
            2},
        Expansion{"std::vector", "std::vector<{0}, std::allocator<{0}>>", 1},
        Expansion{"std::set", "std::set<{0}, std::less<{0}>, std::allocator<{0}>>", 1},
        Expansion{
            "std::unordered_set", "std::unordered_set<{0}, std::hash<{0}>, std::equal_to<{0}>, std::allocator<{0}>>",
            1},
        Expansion{"std::list", "std::list<{0}, std::allocator<{0}>>", 1},
        Expansion{"std::deque", "std::deque<{0}, std::allocator<{0}>>", 1},
        Expansion{"std::pair", "std::pair<{0}, {1}>", 2},
        Expansion{"std::string", "std::basic_string<char, std::char_traits<char>, std::allocator<char>>", 0},
    };

    static std::optional<Expansion> find_expansion(std::string_view name) {
        return asp::iter::from(kExpansions).copied().find([&](Expansion const& e) { return e.name == name; });
    }

    static std::string instantiate(std::string pattern, std::vector<std::string> const& args) {
        for (auto const& [i, arg] : asp::iter::enumerate(args))
            pattern = cpp::replace_all(std::move(pattern), "{" + std::to_string(i) + "}", arg);
        return pattern;
    }

    static std::string with_decorators(Node const& node, std::string body) {
        std::string out;
        if (!node.constness.empty()) out = node.constness + " ";
        return out + body + node.ptr;
    }

    static std::string expand_node(Node const& node) {
        if (!node.is_stl()) return with_decorators(node, node.name);

        auto const expansion = find_expansion(node.name);
        if (!expansion) return with_decorators(node, node.name);

        std::vector<std::string> args;
        args.reserve(node.args.size());
        for (auto const& arg : node.args)
            args.push_back(expand_node(arg));
        return with_decorators(node, instantiate(std::string{expansion->pattern}, args));
    }

    static void collect_leaves(Node const& node, std::vector<std::pair<std::string, bool>>& out) {
        if (!node.args.empty()) {
            for (auto const& arg : node.args)
                collect_leaves(arg, out);
            return;
        }
        if (node.is_stl() || node.name.empty()) return;

        auto bare = cpp::strip_crp(node.name);
        if (bare.empty()) return;

        bool const by_value = !node.ptr.contains('*') && !node.ptr.contains('&');
        out.emplace_back(std::move(bare), by_value);
    }

    Node parse(std::string_view type) {
        type = cpp::trim(type);
        if (type.empty()) return {};

        std::string ptr;
        while (!type.empty() && (type.back() == '*' || type.back() == '&')) {
            ptr.insert(ptr.begin(), type.back());
            type.remove_suffix(1);
            type = cpp::trim(type);
        }

        std::string constness;
        if (type.ends_with(" const")) {
            constness = "const";
            type.remove_suffix(6);
            type = cpp::trim(type);
        }
        if (type.starts_with("const ")) {
            constness = "const";
            type.remove_prefix(6);
            type = cpp::trim(type);
        }

        auto const stripped = cpp::strip_crp(type);
        auto const open = type.find('<');
        if (!type.contains("std::") || stripped == "std::string" || open == std::string_view::npos) {
            return Node{constness, stripped, {}, ptr};
        }

        auto const close = type.rfind('>');
        Node node{constness, std::string{cpp::trim(type.substr(0, open))}, {}, ptr};
        if (close == std::string_view::npos || close < open) return node;

        for (auto const& arg : cpp::split_top_level(type.substr(open + 1, close - open - 1))) {
            node.args.push_back(parse(arg));
        }
        return node;
    }

    std::string expand(std::string_view type) { return expand_node(parse(type)); }

    Node collapse(Node const& node) {
        if (!node.is_stl()) return Node{node.constness, node.name, {}, node.ptr};
        auto const expansion = find_expansion(node.name);
        if (!expansion) return Node{node.constness, node.name, {}, node.ptr};

        Node out{node.constness, node.name, {}, node.ptr};
        for (std::size_t i = 0; i < expansion->arity && i < node.args.size(); ++i) {
            out.args.push_back(collapse(node.args[i]));
        }
        return out;
    }

    std::string to_string(Node const& node) {
        std::string out;
        if (!node.constness.empty()) out = node.constness + " ";
        out += node.name;
        if (!node.args.empty()) {
            out += '<';
            for (std::size_t i = 0; i < node.args.size(); ++i) {
                if (i != 0) out += ", ";
                out += to_string(node.args[i]);
            }
            out += '>';
        }
        return out + node.ptr;
    }

    static Node sugar_node(Node node) {
        for (auto& arg : node.args)
            arg = sugar_node(std::move(arg));

        if (node.name == "std::basic_string" && !node.args.empty() && node.args.front().name == "char") {
            return Node{node.constness, "std::string", {}, node.ptr};
        }
        if (node.is_stl()) return collapse(node);
        return node;
    }

    static constexpr auto kInlineNamespaces =
        std::to_array<std::string_view>({"std::__ndk1::", "std::__1::", "std::__cxx11::", "std::__Cr::"});

    std::string sugar(std::string_view type) {
        std::string text{type};
        for (auto const inline_ns : kInlineNamespaces)
            text = cpp::replace_all(std::move(text), inline_ns, "std::");
        if (!text.contains("std::")) return text;
        return to_string(sugar_node(parse(cpp::normalize_type(text))));
    }

    std::vector<std::pair<std::string, bool>> value_types(std::string_view type) {
        std::vector<std::pair<std::string, bool>> out;
        collect_leaves(parse(type), out);
        return out;
    }
}  // namespace bromaida::stl
