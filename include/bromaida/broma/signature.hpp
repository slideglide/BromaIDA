#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <ast.hpp>

#include <bromaida/platform/address.hpp>
#include <bromaida/platform/platform.hpp>
#include <bromaida/typegen/stl_types.hpp>

namespace bromaida {

    class TypeRef {
    public:
        TypeRef() = default;
        explicit TypeRef(std::string_view type);

        [[nodiscard]] std::string const& str() const noexcept { return m_type; }
        [[nodiscard]] bool empty() const noexcept { return m_type.empty(); }

        [[nodiscard]] std::string const& stripped() const noexcept { return m_node.name; }
        [[nodiscard]] stl::Node const& node() const noexcept { return m_node; }

        [[nodiscard]] std::string expanded() const;

        [[nodiscard]] bool is_stl() const noexcept { return m_node.is_stl(); }
        [[nodiscard]] bool is_const() const noexcept { return m_node.constness == "const"; }
        [[nodiscard]] bool is_ptr() const noexcept { return m_node.ptr.ends_with('*'); }
        [[nodiscard]] bool is_ref() const noexcept { return m_node.ptr.ends_with('&'); }
        [[nodiscard]] bool is_ptr_or_ref() const noexcept { return !m_node.ptr.empty(); }

        [[nodiscard]] bool is_unknown_return() const noexcept;

        [[nodiscard]] static TypeRef void_type() { return TypeRef{"void"}; }
        [[nodiscard]] static TypeRef for_ctor(std::string_view class_name);

        friend bool operator==(TypeRef const& a, TypeRef const& b) noexcept { return a.m_type == b.m_type; }

    private:
        std::string m_type;
        stl::Node m_node;
    };

    struct Param {
        TypeRef type;
        std::string name;
        std::string reg;

        [[nodiscard]] std::string to_string() const;
        friend bool operator==(Param const& a, Param const& b) noexcept { return a.type == b.type; }
    };

    struct FunctionSignature {
        std::string name;
        std::string class_name;
        TypeRef ret;
        std::vector<Param> params;

        bool is_virtual = false;
        bool is_static = false;
        bool is_const = false;
        bool is_inline = false;
        bool is_missing = false;
        bool is_ctor = false;
        bool is_dtor = false;
        bool is_variadic = false;

        [[nodiscard]] bool is_ctor_or_dtor() const noexcept { return is_ctor || is_dtor; }
        [[nodiscard]] std::string qualified_name() const;

        [[nodiscard]] std::string ida_qualified_name(bool tilde_is_visible = true) const;

        [[nodiscard]] TypeRef ida_return_type() const;

        [[nodiscard]] std::string args_string(bool include_this = false, bool include_names = true) const;

        [[nodiscard]] std::string overload_key() const;
        [[nodiscard]] bool same_overload(FunctionSignature const& other) const;

        [[nodiscard]] std::string declaration() const;

        [[nodiscard]] static FunctionSignature from_proto(
            std::string_view class_name, broma::MemberFunctionProto const& proto, Platform target);
        [[nodiscard]] static FunctionSignature from_proto(broma::FunctionProto const& proto, Platform target);
    };

    struct Binding : FunctionSignature {
        Bind bind;

        [[nodiscard]] bool has_address() const noexcept { return bind.has_address(); }
        [[nodiscard]] std::string short_info() const;

        [[nodiscard]] std::string ida_declaration(bool tilde_is_visible = true) const;

        [[nodiscard]] static Binding from_field(
            std::string_view class_name, broma::FunctionBindField const& field, Platform target);
        [[nodiscard]] static Binding from_function(broma::Function const& function, Platform target);
    };
}
