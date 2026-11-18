#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <bromaida/binary_info.hpp>
#include <bromaida/signature.hpp>

#include <pro.h>
#include <typeinf.hpp>

namespace bromaida::ida {
    /// Resolves type names from Broma against the IDB's type library (and IDA's built-ins).
    /// Results are cached; drop the cache after anything that changes local types.
    class TypeContext {
    public:
        explicit TypeContext(BinaryInfo binary)
            : m_binary(binary) {}

        [[nodiscard]] BinaryInfo const& binary() const noexcept { return m_binary; }
        void clear() { m_cache.clear(); }

        /// @param name A bare name: no const, pointer or reference.
        /// @param allow_forward_decl Accept a type that is only forward declared.
        /// @return The type, or nothing if IDA doesn't know it.
        [[nodiscard]] std::optional<tinfo_t> resolve_bare(std::string_view name, bool allow_forward_decl = false);
        /// @param type A type with its const, pointer and reference decorations.
        /// @return The IDA type, or nothing if it can't be resolved. Unknown classes behind pointers become
        ///         forward declarations.
        [[nodiscard]] std::optional<tinfo_t> resolve(TypeRef const& type);

        /// @param ea Function address.
        /// @param binding What Broma says the function looks like.
        /// @return True if the function in the database differs from `binding`.
        [[nodiscard]] bool has_mismatch(ea_t ea, Binding const& binding);
        /// Builds the whole signature (return pointer, `this`, parameters) and applies it at `ea`.
        /// @return True if IDA accepted it.
        [[nodiscard]] bool apply_signature(ea_t ea, FunctionSignature const& sig);

        /// Name, argument strings and variadic flag of an existing symbol, demangled.
        struct DemangledInfo {
            std::string name;
            std::vector<std::string> args;
            bool variadic = false;
        };
        [[nodiscard]] static std::optional<DemangledInfo> demangled_info(ea_t ea);

        /// Picks the overload `binding` describes.
        /// @param candidates Addresses of symbols that share a qualified name.
        /// @return The candidates that match.
        [[nodiscard]] std::vector<ea_t> resolve_overload(Binding const& binding, std::vector<ea_t> const& candidates);

        /// @return True if both names end up as the same IDA type.
        [[nodiscard]] bool types_equivalent(std::string_view a, std::string_view b);

    private:
        [[nodiscard]] std::optional<tinfo_t> named_type(std::string_view name, bool allow_forward_decl) const;
        [[nodiscard]] std::optional<tinfo_t> declare_forward(std::string_view name) const;
        [[nodiscard]] bool needs_return_pointer(TypeRef const& ret, tinfo_t const& resolved) const;
        [[nodiscard]] bool params_match(
            std::vector<std::optional<tinfo_t>> const& types, Binding const& binding, bool strict);

        BinaryInfo m_binary;
        std::unordered_map<std::string, std::optional<tinfo_t>> m_cache;
    };

    /// Types IDA can't pass in registers even though they look trivial.
    [[nodiscard]] bool is_nontrivial_for_calls(tinfo_t const& type);
}  // namespace bromaida::ida
