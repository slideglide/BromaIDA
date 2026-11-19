#pragma once

#include <asp/detail/Function.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/platform/address.hpp>
#include <bromaida/util/path.hpp>
#include <bromaida/platform/platform.hpp>
#include <bromaida/broma/signature.hpp>

namespace bromaida {
    struct RawSymbol {
        std::string name;
        std::uint64_t offset = 0;
        bool is_function = false;
        std::optional<bool> is_static = std::nullopt;
    };

    using Demangler = asp::MoveOnlyFunction<std::optional<std::string>(std::string_view) const>;

    [[nodiscard]] Demangler itanium_demangler();

    [[nodiscard]] std::optional<FunctionSignature> parse_demangled_function(std::string_view demangled);

    struct ExtractedMethod {
        FunctionSignature sig;
        Bind bind;
    };

    struct ExtractedClass {
        std::string name;
        std::vector<std::string> bases;
        std::vector<ExtractedMethod> methods;
        std::vector<std::string> vtable;
        bool exported = true;
    };

    struct ExtractedBinary {
        Platform platform = Platform::Windows;
        std::vector<ExtractedClass> classes;
        std::vector<ExtractedMethod> free_functions;
    };

    struct ClassLayout {
        std::string name;
        std::vector<std::string> bases;
        std::vector<std::string> vtable_symbols;
    };

    struct ExtractInput {
        Platform platform = Platform::Windows;
        std::vector<RawSymbol> symbols{};
        std::vector<ClassLayout> layouts{};
        Demangler demangle{};
    };

    [[nodiscard]] ExtractedBinary extract_binary(ExtractInput const& input);
}
