#include "ida_types.hpp"

#include <algorithm>
#include <ranges>
#include <cctype>
#include <regex>
#include <set>

#include <bromaida/log.hpp>
#include <bromaida/text.hpp>

#include <bytes.hpp>
#include <name.hpp>

namespace bromaida::ida {

static constexpr Log log{"Types"};

// TODO: populate from Broma with an explicit attribute once there is one.
static std::set<std::string, std::less<>> const kKnownNontrivials{"cocos2d::CCPoint", "cocos2d::CCSize"};

static bool is_class_name(std::string_view name) {
    static std::regex const re(R"(^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$)");
    return std::regex_match(name.begin(), name.end(), re);
}

static bool is_generated_arg_name(std::string_view name) {
    static std::regex const re(R"((p|a)[0-9]+)");
    return std::regex_match(name.begin(), name.end(), re);
}

static std::string to_std(qstring const& s) { return {s.c_str(), s.length()}; }

static bool can_return_in_registers(tinfo_t const& type) {
    argloc_t loc;
    return calc_retloc(&loc, type, inf_get_cc_cm() & CM_CC_MASK);
}

bool is_nontrivial_for_calls(tinfo_t const& type) {
    if (!type.is_udt() || type.is_forward_decl()) return false;

    qstring name, final_name;
    type.get_type_name(&name);
    type.get_final_type_name(&final_name);
    for (auto const* n : {&name, &final_name}) {
        if (n->empty()) continue;
        auto const s = to_std(*n);
        if (s.starts_with("std::") || kKnownNontrivials.contains(s)) return true;
    }

    udt_type_data_t udt;
    if (!type.get_udt_details(&udt)) return false;

    for (auto const& member : udt) {
        if (member.is_vftable()) return true;
        tinfo_t member_type = member.type;
        while (member_type.is_array())
            member_type = member_type.get_array_element();
        if (member_type.is_udt() && is_nontrivial_for_calls(member_type)) return true;
    }
    return false;
}

std::optional<tinfo_t> TypeContext::named_type(std::string_view name, bool allow_forward_decl) const {
    tinfo_t type;
    if (!type.get_named_type(get_idati(), std::string(name).c_str())) return std::nullopt;

    if (type.is_forward_decl()) {
        // IDAClang imports forward-only structs as forward declarations: is_correct() is false
        // for those, but they are fine behind a pointer.
        return allow_forward_decl ? std::optional{type} : std::nullopt;
    }
    return type.is_correct() ? std::optional{type} : std::nullopt;
}

std::optional<tinfo_t> TypeContext::declare_forward(std::string_view name) const {
    if (!is_class_name(name)) return std::nullopt;

    tinfo_t forward;
    if (forward.create_forward_decl(get_idati(), BTF_STRUCT, std::string(name).c_str()) != TERR_OK) {
        return std::nullopt;
    }
    return forward;
}

std::optional<tinfo_t> TypeContext::resolve_bare(std::string_view name, bool allow_forward_decl) {
    if (name.empty()) return std::nullopt;

    if (name == "void") {
        tinfo_t type;
        type.create_simple_type(BT_VOID);
        return type;
    }
    if (auto type = named_type(name, allow_forward_decl)) return type;

    // not a named type: maybe a primitive such as `unsigned int`
    tinfo_t probe;
    auto const decl = fmt::format("{} __probe_dummy;", name);
    if (!parse_decl(&probe, nullptr, get_idati(), decl.c_str(), PT_SIL) || !probe.is_correct()) return std::nullopt;
    return probe;
}

std::optional<tinfo_t> TypeContext::resolve(TypeRef const& type) {
    if (auto it = m_cache.find(type.str()); it != m_cache.end()) return it->second;

    auto compute = [&]() -> std::optional<tinfo_t> {
        bool const by_ref = type.is_ptr_or_ref();
        // an STL type is looked up in its libc++-expanded spelling, minus const/pointer/reference
        std::string const lookup = type.is_stl() ? cpp::strip_crp(type.expanded()) : type.stripped();

        auto base = resolve_bare(lookup, by_ref);
        if (!base && by_ref) base = declare_forward(lookup);
        if (!base) return std::nullopt;

        if (base->is_forward_decl() && !by_ref) {
            log.warn("Can't use forward-declared type '{}' by value!", lookup);
            return std::nullopt;
        }

        if (type.is_const()) base->set_const();
        for ([[maybe_unused]] char c : type.node().ptr) {
            tinfo_t wrapped;
            wrapped.create_ptr(*base);
            base = wrapped;
        }
        return base;
    };

    auto result = compute();
    m_cache.emplace(type.str(), result);
    return result;
}

bool TypeContext::needs_return_pointer(TypeRef const& ret, tinfo_t const& resolved) const {
    if (ret.is_ptr_or_ref() || !resolved.is_udt()) return false;
    if (ret.is_stl() || is_nontrivial_for_calls(resolved)) return true;
    return !can_return_in_registers(resolved);
}

bool TypeContext::apply_signature(ea_t ea, FunctionSignature const& sig) {
    auto const ret = sig.ida_return_type();

    std::optional<tinfo_t> final_return;
    std::optional<funcarg_t> return_pointer;

    if (ret.is_unknown_return()) {
        // keep whatever IDA guessed, including a return pointer it already has
        tinfo_t existing;
        func_type_data_t current;
        if (get_tinfo(&existing, ea) && existing.is_func() && existing.get_func_details(&current)) {
            final_return = current.rettype;
            for (auto const& arg : current) {
                if (arg.flags & FAI_RETPTR) {
                    return_pointer = arg;
                    break;
                }
            }
        } else {
            final_return.emplace();
            final_return->create_simple_type(BT_INT);
        }
    } else {
        auto resolved = resolve(ret);
        if (!resolved) {
            log.warn("Couldn't resolve return type '{}' for function at {:#x}!", ret.str(), ea);
            return false;
        }
        final_return = *resolved;
        if (needs_return_pointer(ret, *resolved)) {
            funcarg_t arg;
            arg.name = "retstr";
            arg.type.create_ptr(*resolved);
            arg.flags = FAI_RETPTR | FAI_STRUCT;
            return_pointer = arg;
            final_return = arg.type;
        }
    }

    std::optional<funcarg_t> this_arg;
    if (!sig.is_static && !sig.class_name.empty()) {
        auto this_type = resolve(TypeRef{sig.class_name + "*"});
        if (!this_type) {
            log.warn("Couldn't resolve implicit 'this' argument ('{}*') for function at {:#x}!", sig.class_name, ea);
            return false;
        }
        funcarg_t arg;
        arg.name = "this";
        arg.type = *this_type;
        this_arg = arg;
    }

    std::vector<funcarg_t> params;
    for (auto const& param : sig.params) {
        auto resolved = resolve(param.type);
        if (!resolved) {
            log.warn(
                "Couldn't resolve parameter '{}' ('{}') for function at {:#x}!",
                param.name.empty() ? param.type.str() : param.name, param.type.str(), ea);
            return false;
        }
        funcarg_t arg;
        arg.name = param.name.c_str();
        arg.type = *resolved;
        params.push_back(std::move(arg));
    }

    func_type_data_t data;
    data.rettype = *final_return;
    if (sig.is_variadic) data.set_cc(CM_CC_ELLIPSIS);

    // Itanium returns through the first argument, before `this`; MSVC puts it after
    bool const itanium = m_binary.is_itanium_abi();
    if (return_pointer && itanium) data.push_back(*return_pointer);
    if (this_arg) data.push_back(*this_arg);
    if (return_pointer && !itanium) data.push_back(*return_pointer);
    for (auto& param : params)
        data.push_back(std::move(param));

    tinfo_t function_type;
    if (!function_type.create_func(data)) {
        log.warn("Couldn't construct type information for function at {:#x}!", ea);
        return false;
    }
    return apply_tinfo(ea, function_type, TINFO_DEFINITE);
}

bool TypeContext::params_match(std::vector<std::optional<tinfo_t>> const& types, Binding const& binding, bool strict) {
    if (types.size() != binding.params.size()) return false;

    for (std::size_t i = 0; i < types.size(); ++i) {
        // an argument the demangler got wrong is skipped
        if (!types[i]) continue;
        auto expected = resolve(binding.params[i].type);
        if (!expected) continue;

        bool const same = strict ? types[i]->compare_with(*expected, TCMP_DECL) : types[i]->equals_to(*expected);
        if (!same) return false;
    }
    return true;
}

bool TypeContext::has_mismatch(ea_t ea, Binding const& binding) {
    tinfo_t existing;
    func_type_data_t function;
    if (!get_tinfo(&existing, ea) || !existing.is_func() || !existing.get_func_details(&function)) return true;

    if ((function.get_cc() == CM_CC_ELLIPSIS) != binding.is_variadic) return true;

    std::vector<funcarg_t> args(function.begin(), function.end());
    auto const ret = binding.ida_return_type();

    bool needs_retptr = false;
    tinfo_t expected_return = function.rettype;

    if (ret.is_unknown_return()) {
        // keep IDA's return type and ignore a return pointer it already has
        std::erase_if(args, [](funcarg_t const& a) { return (a.flags & FAI_RETPTR) != 0; });
    } else {
        auto resolved = resolve(ret);
        if (resolved) {
            needs_retptr = needs_return_pointer(ret, *resolved);
            expected_return = *resolved;
            if (needs_retptr) {
                expected_return = tinfo_t{};
                expected_return.create_ptr(*resolved);
            }
            if (!function.rettype.compare_with(expected_return, TCMP_DECL)) return true;
        }
    }

    bool const itanium = m_binary.is_itanium_abi();
    auto take_return_pointer = [&]() {
        if (args.empty() || !args.front().type.equals_to(expected_return) || !(args.front().flags & FAI_RETPTR)) {
            return false;
        }
        args.erase(args.begin());
        return true;
    };

    if (needs_retptr && itanium && !take_return_pointer()) return true;

    if (!binding.class_name.empty() && !binding.is_static) {
        if (args.empty()) return true;
        auto this_type = resolve(TypeRef{binding.class_name + "*"});
        bool const type_matches = this_type && args.front().type.equals_to(*this_type);
        // IDA-generated names (a1, a2) count as a mismatch: they mean "nobody named this yet"
        auto const name = to_std(args.front().name);
        bool const name_matches = name.size() > 1 && name.front() == 'a' &&
                                  std::ranges::all_of(name.substr(1), [](unsigned char c) { return std::isdigit(c); });
        if (!type_matches || !name_matches) return true;
        args.erase(args.begin());
    }

    if (needs_retptr && !itanium && !take_return_pointer()) return true;

    auto types = args | std::views::transform([](auto const& arg) { return std::optional<tinfo_t>{arg.type}; }) |
                 std::ranges::to<std::vector>();
    if (!params_match(types, binding, true)) return true;

    for (std::size_t i = 0; i < args.size() && i < binding.params.size(); ++i) {
        // pN is Broma's generated name, aN is IDA's
        if (!is_generated_arg_name(binding.params[i].name) && to_std(args[i].name) != binding.params[i].name) {
            return true;
        }
    }
    return false;
}

std::optional<TypeContext::DemangledInfo> TypeContext::demangled_info(ea_t ea) {
    qstring demangled;
    if (get_ea_name(&demangled, ea, GN_SHORT | GN_DEMANGLED) <= 0) return std::nullopt;

    // MSVC calling conventions and the like are too problematic; only `name(args)` is understood
    static std::regex const re(R"((\S+)\((.*)\))");
    auto const text = to_std(demangled);
    std::smatch m;
    if (!std::regex_match(text, m, re)) return std::nullopt;

    DemangledInfo info;
    info.name = m[1];
    std::string const args = m[2];
    if (args.empty() || args == "void") return info;

    for (auto const& arg : cpp::split_top_level(args))
        info.args.emplace_back(cpp::trim(arg));

    if (!info.args.empty() && info.args.back() == "...") {
        info.variadic = true;
        info.args.pop_back();
    }
    return info;
}

std::vector<ea_t> TypeContext::resolve_overload(Binding const& binding, std::vector<ea_t> const& candidates) {
    if (candidates.size() == 1) return candidates;

    std::vector<ea_t> exact, plausible;
    for (ea_t const ea : candidates) {
        auto info = demangled_info(ea);
        if (!info || info->variadic != binding.is_variadic || info->args.size() != binding.params.size()) continue;

        // IDA sometimes demangles arguments wrongly; those come back as std::nullopt and match anything
        auto types = info->args | std::views::transform([&](auto const& arg) {
                         return cpp::looks_malformed(arg) ? std::nullopt : resolve_bare(arg);
                     }) |
                     std::ranges::to<std::vector>();
        if (!params_match(types, binding, false)) continue;

        plausible.push_back(ea);
        if (std::ranges::all_of(types, [](auto const& t) { return t.has_value(); })) exact.push_back(ea);
    }
    return exact.empty() ? plausible : exact;
}

bool TypeContext::types_equivalent(std::string_view a, std::string_view b) {
    if (a.empty() || b.empty()) return false;
    auto first = resolve_bare(a);
    auto second = resolve_bare(b);
    return first && second && first->equals_to(*second);
}

}  // namespace bromaida::ida
