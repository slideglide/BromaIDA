#pragma once

#include <string>
#include <vector>

#include <ast.hpp>

#include <bromaida/typegen/bundle.hpp>
#include <bromaida/typegen/class_graph.hpp>
#include <bromaida/util/path.hpp>
#include <bromaida/platform/platform.hpp>

namespace bromaida {

    [[nodiscard]] std::string build_class(broma::Class const& cls, ClassGraph const& graph, Platform target);

    struct CodegenInput {
        Platform platform = Platform::Windows;
        std::vector<broma::Class const*> classes;

        std::vector<std::string> unavailable_classes;

        Bundle bundle;
        std::vector<std::string> defines;
        Path broma_dir;
        std::string generator_version;
    };

    struct CodegenOutput {
        std::string text;
        std::size_t enum_count = 0;
        std::size_t header_class_count = 0;
        std::size_t broma_class_count = 0;
    };

    [[nodiscard]] CodegenOutput generate_types_header(CodegenInput const& input, ClassGraph const& graph);
}
