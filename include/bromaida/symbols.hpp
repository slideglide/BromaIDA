#pragma once

#include <asp/detail/Function.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/address.hpp>
#include <bromaida/path.hpp>
#include <bromaida/platform.hpp>
#include <bromaida/signature.hpp>

namespace bromaida {
    struct RawSymbol {
        std::string name;          ///< Mangled.
        std::uint64_t offset = 0;  ///< From the image base.
        bool is_function = false;
        std::optional<bool> is_static = std::nullopt;  ///< Only known when a decompiler/type library told us.
    };

    /// Takes a mangled name and returns its demangled form, or nothing if it can't be demangled.
    using Demangler = asp::MoveOnlyFunction<std::optional<std::string>(std::string_view) const>;

    /// Itanium demangler.
    [[nodiscard]] Demangler itanium_demangler();

    /// Reads the symbols of an ELF file.
    /// @param file Path to the ELF.
    /// @return Its symbols, or an error message.
    [[nodiscard]] geode::Result<std::vector<RawSymbol>, std::string> read_elf_symbols(PathView file);

    /// Parses a demangled function such as `cocos2d::CCNode::setPosition(cocos2d::CCPoint const&) const`.
    /// @param demangled A demangled name.
    /// @return The signature, or nothing for thunks, lambdas, templates and anything else Broma can't express.
    ///         `class_name` is left empty because the scope could be a class or a namespace.
    [[nodiscard]] std::optional<FunctionSignature> parse_demangled_function(std::string_view demangled);

    struct ExtractedMethod {
        FunctionSignature sig;
        Bind bind;
    };

    struct ExtractedClass {
        std::string name;
        std::vector<std::string> bases;  ///< From typeinfo, when the source could read it.
        std::vector<ExtractedMethod> methods;
        std::vector<std::string> vtable;  ///< `overload_key`s in slot order; empty if unknown.
        bool exported = true;             ///< Symbols are exported by the binary (=> `[[link]]`).
    };

    struct ExtractedBinary {
        Platform platform = Platform::Windows;
        std::vector<ExtractedClass> classes;
        std::vector<ExtractedMethod> free_functions;
    };

    /// Class info beyond plain symbols, when IDA can read it.
    struct ClassLayout {
        std::string name;
        std::vector<std::string> bases;
        std::vector<std::string> vtable_symbols;  ///< Mangled names, in slot order.
    };

    struct ExtractInput {
        Platform platform = Platform::Windows;
        std::vector<RawSymbol> symbols{};
        std::vector<ClassLayout> layouts{};
        Demangler demangle{};
    };

    /// Groups symbols into classes, merges constructor and destructor variants and marks virtuals.
    /// @param input Platform, symbols, and class layouts if IDA could read them.
    /// @return The classes and free functions found.
    [[nodiscard]] ExtractedBinary extract_binary(ExtractInput const& input);
}  // namespace bromaida
