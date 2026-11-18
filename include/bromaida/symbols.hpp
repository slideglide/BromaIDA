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
    /// A symbol as found in a binary's symbol table.
    struct RawSymbol {
        std::string name;          ///< Mangled.
        std::uint64_t offset = 0;  ///< From the image base.
        bool is_function = false;
        std::optional<bool> is_static = std::nullopt;  ///< Only known when a decompiler/type library told us.
    };

    /// Turns a mangled name into its demangled form, or std::nullopt.
    using Demangler = asp::MoveOnlyFunction<std::optional<std::string>(std::string_view) const>;

    /// Itanium demangler (`abi::__cxa_demangle`); returns an always-failing one where unavailable.
    [[nodiscard]] Demangler itanium_demangler();

    /// Reads .dynsym/.symtab of an ELF file (Android builds keep their exports).
    [[nodiscard]] geode::Result<std::vector<RawSymbol>, std::string> read_elf_symbols(PathView file);

    /// Parses a demangled function such as `cocos2d::CCNode::setPosition(cocos2d::CCPoint const&) const`.
    /// Returns std::nullopt for thunks, lambdas, templates and other things Broma can't express.
    /// `class_name` is left empty: whether the scope is a class or a namespace is not known here.
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

    /// Class information beyond plain symbols, if the front end (IDA) can read it.
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

    /// Groups symbols into classes, merges constructor/destructor variants and marks virtuals.
    [[nodiscard]] ExtractedBinary extract_binary(ExtractInput const& input);
}  // namespace bromaida
