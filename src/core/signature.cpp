#include <bromaida/signature.hpp>

#include <algorithm>
#include <fmt/format.h>

#include <bromaida/text.hpp>

namespace bromaida {

// ---- TypeRef ---------------------------------------------------------------

TypeRef::TypeRef(std::string_view type)
    : m_type(cpp::normalize_type(type)), m_node(stl::parse(m_type)) {}

std::string TypeRef::expanded() const { return is_stl() ? cpp::format_ptr(stl::expand(m_type)) : m_type; }

bool TypeRef::is_unknown_return() const noexcept {
    return m_node.name.empty() || m_node.name == "auto" || m_node.name == "TodoReturn";
}

TypeRef TypeRef::for_ctor(std::string_view class_name) { return TypeRef{fmt::format("{}*", class_name)}; }

std::string Param::to_string() const {
    if (name.empty()) return type.str();
    auto out = fmt::format("{} {}", type.str(), name);
    if (!reg.empty()) out += fmt::format("@<{}>", reg);
    return out;
}

// ---- FunctionSignature -----------------------------------------------------

std::string FunctionSignature::qualified_name() const {
    return class_name.empty() ? name : fmt::format("{}::{}", class_name, name);
}

std::string FunctionSignature::ida_qualified_name(bool tilde_is_visible) const {
    auto out = qualified_name();
    return tilde_is_visible ? out : cpp::replace_all(std::move(out), "~", "d");
}

TypeRef FunctionSignature::ida_return_type() const {
    if (is_ctor) return TypeRef::for_ctor(class_name);
    if (is_dtor) return TypeRef::void_type();
    return ret;
}

std::string FunctionSignature::args_string(bool include_this, bool include_names) const {
    std::vector<std::string> parts;
    auto const this_type = fmt::format("{}*", class_name);
    bool const has_this = !params.empty() && params.front().type.str() == this_type;

    if (include_this && !is_static && !has_this) {
        parts.push_back(include_names ? fmt::format("{} this", this_type) : this_type);
    }

    for (std::size_t i = has_this && !include_this ? 1 : 0; i < params.size(); ++i) {
        parts.push_back(include_names ? params[i].to_string() : params[i].type.str());
    }
    if (is_variadic) parts.emplace_back("...");

    std::string out;
    for (auto const& part : parts) {
        if (!out.empty()) out += ", ";
        out += part;
    }
    return out;
}

std::string FunctionSignature::overload_key() const {
    std::string key = name + "(";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0) key += ',';
        key += params[i].type.str();
    }
    return key + ")";
}

bool FunctionSignature::same_overload(FunctionSignature const& other) const {
    return name == other.name && params == other.params;
}

std::string FunctionSignature::declaration() const {
    return fmt::format(
        "{}{}{}{}({}){}", is_static ? "static " : "", is_virtual ? "virtual " : "",
        (!ret.empty() && !is_ctor_or_dtor()) ? ret.str() + " " : std::string{}, name, args_string(),
        is_const ? " const;" : ";");
}

FunctionSignature FunctionSignature::from_proto(broma::FunctionProto const& proto, Platform target) {
    FunctionSignature sig;
    sig.name = proto.name;
    sig.ret = TypeRef{proto.ret.name};
    sig.is_variadic = proto.is_variadic;
    sig.is_missing = has_platform(proto.attributes.missing, target);
    for (auto const& [type, name] : proto.args) {
        if (type.name == "...") continue;  // the variadic ellipsis is tracked by is_variadic
        sig.params.push_back(Param{TypeRef{type.name}, name, {}});
    }
    return sig;
}

FunctionSignature FunctionSignature::from_proto(
    std::string_view class_name, broma::MemberFunctionProto const& proto, Platform target) {
    auto sig = from_proto(static_cast<broma::FunctionProto const&>(proto), target);
    sig.class_name = std::string{class_name};
    sig.is_virtual = proto.is_virtual;
    sig.is_static = proto.is_static;
    sig.is_const = proto.is_const;
    sig.is_ctor = proto.type == broma::FunctionType::Ctor;
    sig.is_dtor = proto.type == broma::FunctionType::Dtor;
    return sig;
}

// ---- Binding ---------------------------------------------------------------

std::string Binding::short_info() const { return fmt::format("{} @ {}", qualified_name(), bind.to_string()); }

std::string Binding::ida_declaration(bool tilde_is_visible) const {
    // IDA drops `const` on methods.
    return fmt::format(
        "{}{}{} {}({});", is_static ? "static " : "", is_virtual ? "virtual " : "", ret.str(),
        ida_qualified_name(tilde_is_visible), args_string());
}

Binding Binding::from_field(std::string_view class_name, broma::FunctionBindField const& field, Platform target) {
    Binding binding;
    static_cast<FunctionSignature&>(binding) = FunctionSignature::from_proto(class_name, field.prototype, target);
    binding.bind = Bind::from_broma(offset_for(field.binds, target));
    binding.is_inline = binding.bind.is_inlined();
    return binding;
}

Binding Binding::from_function(broma::Function const& function, Platform target) {
    Binding binding;
    static_cast<FunctionSignature&>(binding) = FunctionSignature::from_proto(function.prototype, target);
    binding.bind = Bind::from_broma(offset_for(function.binds, target));
    binding.is_inline = binding.bind.is_inlined();
    return binding;
}

}  // namespace bromaida
