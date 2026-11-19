#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <bromaida/platform/binary_info.hpp>
#include <bromaida/broma/signature.hpp>

#include <pro.h>
#include <typeinf.hpp>

namespace bromaida::ida {
    class TypeContext {
    public:
        explicit TypeContext(BinaryInfo binary)
            : m_binary(binary) {}

        [[nodiscard]] BinaryInfo const& binary() const noexcept { return m_binary; }
        void clear() { m_cache.clear(); }

        [[nodiscard]] std::optional<tinfo_t> resolve_bare(std::string_view name, bool allow_forward_decl = false);
        [[nodiscard]] std::optional<tinfo_t> resolve(TypeRef const& type);

        [[nodiscard]] bool has_mismatch(ea_t ea, Binding const& binding);
        [[nodiscard]] bool apply_signature(ea_t ea, FunctionSignature const& sig);

        struct DemangledInfo {
            std::string name;
            std::vector<std::string> args;
            bool variadic = false;
        };
        [[nodiscard]] static std::optional<DemangledInfo> demangled_info(ea_t ea);

        [[nodiscard]] std::vector<ea_t> resolve_overload(Binding const& binding, std::vector<ea_t> const& candidates);

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

    [[nodiscard]] bool is_nontrivial_for_calls(tinfo_t const& type);
}  // namespace bromaida::ida
