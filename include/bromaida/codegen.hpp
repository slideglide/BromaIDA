#pragma once

#include <string>
#include <vector>

#include <ast.hpp>

#include <bromaida/bundle.hpp>
#include <bromaida/class_graph.hpp>
#include <bromaida/path.hpp>
#include <bromaida/platform.hpp>

namespace bromaida {
    /// Builds the C++ declaration of one Broma class for IDA's parser.
    /// @param cls The class.
    /// @param graph Inheritance and virtuals of all classes.
    /// @param target Platform whose members apply.
    /// @return The declaration text.
    [[nodiscard]] std::string build_class(broma::Class const& cls, ClassGraph const& graph, Platform target);

    struct CodegenInput {
        Platform platform = Platform::Windows;
        std::vector<broma::Class const*> classes;
        /// Classes Broma marks `missing`: they get no definition, only a forward declaration for the classes that name them.
        std::vector<std::string> unavailable_classes;
        /// The headers for what Broma doesn't define (see Bundle), and the `#define`s to set before them.
        Bundle bundle;
        std::vector<std::string> defines;
        Path broma_dir;  ///< Folder holding the .bro files. Enums come from `../include/Geode/Enums.hpp` next to it.
        std::string generator_version;
    };

    struct CodegenOutput {
        std::string text;
        std::size_t enum_count = 0;
        std::size_t header_class_count = 0;
        std::size_t broma_class_count = 0;
    };

    /// Generates the whole header IDAClang parses.
    /// @param input Classes, bundle, defines and platform.
    /// @param graph Graph built from the same classes.
    /// @return The header text and how many enums and classes went into it.
    [[nodiscard]] CodegenOutput generate_types_header(CodegenInput const& input, ClassGraph const& graph);
}  // namespace bromaida
