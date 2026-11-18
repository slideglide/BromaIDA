#include <bromaida/class_graph.hpp>

#include <algorithm>
#include <cctype>
#include <asp/iter.hpp>
#include <fmt/format.h>
#include <ranges>

#include <bromaida/log.hpp>
#include <bromaida/stl_types.hpp>
#include <bromaida/text.hpp>

namespace bromaida {
    static constexpr Log log{"ClassGraph"};

    static auto ordered_uses(auto const& info) {
        return info.type_use_order |
               std::views::transform([&info](auto const& key) -> auto const& { return info.type_uses.at(key); }) |
               std::views::join;
    }

    std::string StlStub::emit() const {
        if (members.empty()) return {};
        std::string out = fmt::format("class {} {{\npublic:\n", class_name);
        for (auto const& m : members)
            out += fmt::format("\t{} {};\n", m.type, m.name);
        out += "};\n";
        return out;
    }

    ClassGraph::ClassGraph(std::vector<broma::Class const*> classes, Platform target)
        : m_target(target) {
        for (auto const* cls : classes) {
            if (m_classes.emplace(cls->name, cls).second) m_names.push_back(cls->name);
        }

        for (auto const& name : m_names) {
            if (auto const scope = cpp::split_qualified(name).scope(); !scope.empty()) {
                m_namespace_prefixes.insert(scope);
            }
            build_info(name, *m_classes.at(name));
        }

        for (auto const& name : m_names) {
            std::unordered_set<std::string> visiting;
            collect_inherited(name, visiting);
        }

        compute_order();
        compute_forward_declarations();
        compute_stl();
    }

    broma::Class const* ClassGraph::find(std::string_view name) const {
        auto it = m_classes.find(std::string{name});
        return it == m_classes.end() ? nullptr : it->second;
    }

    std::vector<std::string> ClassGraph::bases_of(broma::Class const& cls) const {
        return asp::iter::from(cls.superclasses)
            .filter([&](broma::Superclass const& base) { return has_platform(base.platform, m_target); })
            .map([](broma::Superclass const& base) { return base.name; })
            .collect<std::vector<std::string>>();
    }

    static void collect_std_names(stl::Node const& node, std::set<std::string>& out) {
        if (node.name.starts_with("std::")) out.insert(node.name);
        for (auto const& arg : node.args)
            collect_std_names(arg, out);
    }

    static bool is_primitive_name(std::string_view name) {
        static constexpr std::array words{
            "void",    "bool",    "char",    "short",    "int",       "long",     "float",     "double",    "unsigned",
            "signed",  "auto",    "wchar_t", "size_t",   "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t",    "int16_t",
            "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",  "uint64_t", "va_list",   "nullptr_t", "time_t"};
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) return true;
        return asp::iter::split(name, ' ').all(
            [&](std::string_view word) { return word.empty() || asp::iter::contains(words, word); });
    }

    static void collect_referenced_names(stl::Node const& node, std::set<std::string>& out) {
        if (!node.name.starts_with("std::") && !is_primitive_name(node.name)) out.insert(node.name);
        for (auto const& arg : node.args)
            collect_referenced_names(arg, out);
    }

    std::set<std::string> ClassGraph::referenced_types() const {
        std::set<std::string> referenced;
        for (auto const& name : m_names) {
            for (auto const& type : m_info.at(name).type_use_order)
                collect_referenced_names(stl::parse(type), referenced);
            for (auto const& base : bases_of(*m_classes.at(name)))
                referenced.insert(base);
        }
        return referenced;
    }

    std::set<std::string> ClassGraph::used_std_types() const {
        std::set<std::string> used;
        for (auto const& name : m_names) {
            for (auto const& type : m_info.at(name).type_use_order)
                collect_std_names(stl::parse(type), used);
        }
        return used;
    }

    void ClassGraph::build_info(std::string const& name, broma::Class const& cls) {
        Info& info = m_info[name];
        std::unordered_set<std::string> seen_overloads;

        auto note_type = [&](std::string_view raw, bool from_function) {
            auto normalized = TypeRef{raw}.str();
            if (info.type_uses.contains(normalized)) return;

            std::vector<TypeUse> uses;
            if (normalized.contains("std::")) {
                for (auto&& [bare, by_value] : stl::value_types(normalized)) {
                    uses.push_back({std::move(bare), by_value, from_function});
                }
            } else if (auto bare = cpp::strip_crp(normalized); !bare.empty()) {
                bool const by_value = !normalized.contains('*') && !normalized.contains('&');
                uses.push_back({std::move(bare), by_value, from_function});
            } else {
                return;
            }
            info.type_use_order.push_back(normalized);
            info.type_uses.emplace(std::move(normalized), std::move(uses));
        };

        for (auto const& field : cls.fields) {
            if (auto const* fn = field.get_as<broma::FunctionBindField>()) {
                note_type(fn->prototype.ret.name, true);
                for (auto const& arg : fn->prototype.args)
                    note_type(arg.first.name, true);

                auto sig = FunctionSignature::from_proto(name, fn->prototype, m_target);
                if (!seen_overloads.insert(sig.overload_key()).second) {
                    log.warn(
                        "Function '{}' was redeclared with an identical signature in {}!", sig.qualified_name(),
                        cls.source);
                }
                if (fn->prototype.is_virtual) {
                    info.own_keys.insert(sig.overload_key());
                    info.own_virtuals.push_back(std::move(sig));
                }
            } else if (auto const* member = field.get_as<broma::MemberField>()) {
                note_type(member->type.name, false);
            }
        }
    }

    void ClassGraph::collect_inherited(std::string const& name, std::unordered_set<std::string>& visiting) {
        if (m_inherited_done.contains(name) || !visiting.insert(name).second) return;

        auto const* cls = find(name);
        if (!cls) return;
        Info& info = m_info.at(name);

        for (auto const& base_name : bases_of(*cls)) {
            auto base_it = m_info.find(base_name);
            if (base_it == m_info.end()) continue;

            collect_inherited(base_name, visiting);
            Info const& base = base_it->second;

            for (auto const& sig : base.own_virtuals) {
                if (info.inherited_keys.insert(sig.overload_key()).second) {
                    info.inherited_by_name[sig.name].push_back(sig);
                }
            }
            for (auto const& key : base.inherited_keys)
                info.inherited_keys.insert(key);
            for (auto const& [sig_name, sigs] : base.inherited_by_name) {
                auto& list = info.inherited_by_name[sig_name];
                for (auto const& sig : sigs) {
                    if (!asp::iter::from(list).any([&](auto const& s) { return s.same_overload(sig); })) {
                        list.push_back(sig);
                    }
                }
            }
        }
        m_inherited_done.insert(name);
    }

    std::vector<FunctionSignature> const& ClassGraph::own_virtuals(std::string_view cls) const {
        static std::vector<FunctionSignature> const none;
        auto it = m_info.find(std::string{cls});
        return it == m_info.end() ? none : it->second.own_virtuals;
    }

    bool ClassGraph::is_override(std::string_view cls, FunctionSignature const& sig) const {
        auto it = m_info.find(std::string{cls});
        return it != m_info.end() && it->second.inherited_keys.contains(sig.overload_key());
    }

    bool ClassGraph::is_secondary_override(broma::Class const& cls, FunctionSignature const& sig) const {
        auto bases = bases_of(cls);
        if (bases.size() < 2) return false;

        auto const key = sig.overload_key();
        for (std::size_t i = 1; i < bases.size(); ++i) {
            auto it = m_info.find(bases[i]);
            if (it == m_info.end()) continue;
            if (it->second.own_keys.contains(key) || it->second.inherited_keys.contains(key)) return true;
        }
        return false;
    }

    std::vector<ClassGraph::BrokenOverride> ClassGraph::broken_overrides(std::string_view cls) const {
        std::vector<BrokenOverride> out;
        auto it = m_info.find(std::string{cls});
        if (it == m_info.end()) return out;

        for (auto const& own : it->second.own_virtuals) {
            auto same_name = it->second.inherited_by_name.find(own.name);
            if (same_name == it->second.inherited_by_name.end()) continue;
            if (it->second.inherited_keys.contains(own.overload_key())) continue;
            out.push_back({own, same_name->second});
        }
        return out;
    }

    std::vector<std::string> ClassGraph::hard_dependencies(std::string const& name) const {
        auto const* cls = find(name);
        if (!cls) return {};

        auto deps = bases_of(*cls);
        std::unordered_set<std::string> seen(deps.begin(), deps.end());
        auto const& info = m_info.at(name);
        for (auto const& use : ordered_uses(info)) {
            if (use.by_value && !use.from_function && m_classes.contains(use.bare) && seen.insert(use.bare).second) {
                deps.push_back(use.bare);
            }
        }
        return deps;
    }

    void ClassGraph::compute_order() {
        std::unordered_set<std::string> visited, visiting;

        auto visit = [&](this auto&& self, std::string const& name, std::string const& from) -> void {
            if (visited.contains(name)) return;
            if (visiting.contains(name)) {
                log.warn("Cyclic dependency detected between '{}' and '{}'!", from, name);
                return;
            }
            if (m_namespace_prefixes.contains(name) || !m_classes.contains(name)) {
                visited.insert(name);
                return;
            }

            visiting.insert(name);
            for (auto const& dep : hard_dependencies(name))
                self(dep, name);
            visiting.erase(name);
            visited.insert(name);
            m_order.push_back(name);
        };

        for (auto const& name : m_names)
            visit(name, {});
    }

    void ClassGraph::compute_forward_declarations() {
        std::unordered_map<std::string, std::size_t> position;
        for (auto const [i, name] : asp::iter::enumerate(m_order))
            position.emplace(name, static_cast<std::size_t>(i));

        std::unordered_set<std::string> seen;
        auto is_std_type = [](auto const& entry) { return entry.first.contains("std::"); };
        for (auto const& name : m_names) {
            for (auto const& use :
                 m_info.at(name).type_uses | std::views::filter(is_std_type) | std::views::values | std::views::join) {
                if ((use.by_value && !use.from_function) || m_namespace_prefixes.contains(use.bare)) continue;
                if (m_classes.contains(use.bare)) m_stl_forward.insert(use.bare);
            }
        }
        for (auto const& name : m_stl_forward) {
            if (seen.insert(name).second) m_forward_decls.push_back(name);
        }
        std::ranges::sort(m_forward_decls);

        for (auto const& name : m_names) {
            auto const class_pos = position.contains(name) ? static_cast<long long>(position.at(name)) : -1LL;
            auto const& info = m_info.at(name);
            for (auto const& use : ordered_uses(info)) {
                if ((use.by_value && !use.from_function) || !m_classes.contains(use.bare) ||
                    m_namespace_prefixes.contains(use.bare)) {
                    continue;
                }
                auto it = position.find(use.bare);
                if (it != position.end() && static_cast<long long>(it->second) > class_pos &&
                    seen.insert(use.bare).second) {
                    m_forward_decls.push_back(use.bare);
                }
            }
        }
    }

    void ClassGraph::compute_stl() {
        std::unordered_set<std::string> seen_members;
        std::size_t index = 0;

        for (auto const& name : m_names) {
            auto const& info = m_info.at(name);
            for (auto const& type : info.type_use_order) {
                if (!type.contains("std::")) continue;

                auto stripped = cpp::strip_crp(type);
                if (!seen_members.insert(stripped).second) continue;

                auto const& uses = info.type_uses.at(type);
                bool const needs_value =
                    asp::iter::from(uses).any([](TypeUse const& u) { return u.by_value && !u.from_function; });

                StlStub& target = needs_value ? m_stubs.by_value : m_stubs.by_pointer;
                target.members.push_back({std::move(stripped), fmt::format("m_{}", index++)});
            }
        }
    }
}  // namespace bromaida
