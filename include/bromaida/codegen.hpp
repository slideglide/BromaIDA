#pragma once

#include <string>
#include <vector>

#include <ast.hpp>

#include <bromaida/class_graph.hpp>
#include <bromaida/path.hpp>
#include <bromaida/platform.hpp>

namespace bromaida {

/// Builds the C++ declaration of one Broma class for IDA's parser.
[[nodiscard]] std::string build_class(broma::Class const& cls, ClassGraph const& graph, Platform target);

struct CodegenInput {
    Platform platform = Platform::Windows;
    std::vector<broma::Class const*> classes;
    /// Classes Broma marks `missing` on this platform. They get no definition, but other classes still point at
    /// them, and a type that names an undeclared class is invalid to clang (and crashes IDAClang's vtable code).
    std::vector<std::string> unavailable_classes;
    Path headers_dir;  ///< Folder holding cocos2d.hpp, helpers.hpp, fmod.hpp, c++stl/...
    Path broma_dir;    ///< Folder holding the .bro files. Enums come from `../include/Geode/Enums.hpp` next to it.
    std::string generator_version;
};

struct CodegenOutput {
    std::string text;
    std::size_t enum_count = 0;
    std::size_t header_class_count = 0;
    std::size_t broma_class_count = 0;
};

/// Pure function: produces the header text, never touches the IDB or writes files.
[[nodiscard]] CodegenOutput generate_types_header(CodegenInput const& input, ClassGraph const& graph);

}  // namespace bromaida
