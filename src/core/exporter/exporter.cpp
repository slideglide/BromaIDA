#include <bromaida/util/files.hpp>
#include <bromaida/exporter/exporter.hpp>

#include "planner.hpp"

#include <algorithm>
#include <vector>
#include <span>
#include <ranges>
#include <array>
#include <set>
#include <unordered_map>

#include <asp/fs.hpp>
#include <asp/iter.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/util/log.hpp>
#include <bromaida/util/text.hpp>

namespace bromaida {
    static constexpr Log log{"Exporter"};

    static bool is_generated_name(std::string_view name) {
        if (name.size() < 2 || (name[0] != 'p' && name[0] != 'a')) return false;
        return std::ranges::all_of(name.substr(1), [](char c) { return c >= '0' && c <= '9'; });
    }

    static broma::MemberFunctionProto to_proto(
        FunctionSignature const& sig, std::string_view ret_override = {}, bool keep_names = true) {
        broma::MemberFunctionProto proto;
        proto.name = sig.name;
        proto.ret.name = ret_override.empty() ? sig.ret.str() : std::string{ret_override};
        proto.is_virtual = sig.is_virtual;
        proto.is_static = sig.is_static;
        proto.is_const = sig.is_const;
        proto.is_variadic = sig.is_variadic;
        proto.type = sig.is_ctor   ? broma::FunctionType::Ctor
                     : sig.is_dtor ? broma::FunctionType::Dtor
                                   : broma::FunctionType::Normal;
        for (auto const& [i, param] : asp::iter::enumerate(sig.params)) {
            auto name = !keep_names || param.name.empty() ? fmt::format("p{}", i) : param.name;
            proto.args.push_back({broma::Type{false, param.type.str()}, std::move(name)});
        }
        return proto;
    }

    static std::string statement_key(FunctionSignature const& sig) {
        return sig.overload_key() + (sig.is_const ? "|const" : "");
    }

    static std::vector<ParsedStatement> parse_statements(std::string_view text, ClassSpan const& span) {
        std::vector<ParsedStatement> out;
        auto statements = split_statements(text, span);
        for (auto const [i, statement] : asp::iter::enumerate(statements)) {
            auto parsed = parse_broma("class __Probe {\n" + statement.text + "\n}\n");
            if (parsed.isErr()) continue;
            auto root = std::move(parsed).unwrap();
            if (root.classes.empty()) continue;

            auto functions = bind_fields(root.classes.front());
            auto first = std::ranges::begin(functions);
            if (first == std::ranges::end(functions)) continue;

            auto const& fn = *first;
            auto sig = FunctionSignature::from_proto("__Probe", fn.prototype, Platform::Windows);
            out.push_back({static_cast<std::size_t>(i), statement_key(sig), fn});
        }
        return out;
    }

    Planner::Planner(broma::Root const* root, ExportOptions const& options, ExportPrompter& prompter, ExportPlan& plan)
        : m_root(root), m_options(options), m_prompter(prompter), m_plan(plan) {
        if (root) {
            for (auto const& cls : root->classes)
                m_class_file.emplace(cls.name, Path{cls.source});
        }
    }

    void Planner::run(ExtractedBinary const& binary) {
        for (auto const& cls : binary.classes)
            handle_class(cls, binary.platform);
        handle_free_functions(binary);
        for (auto const& file : m_dirty)
            m_plan.files[file] = m_texts.at(file);
    }

    std::string* Planner::text_of(Path const& file) {
        if (auto it = m_texts.find(file); it != m_texts.end()) return &it->second;
        if (m_unreadable.contains(file)) return nullptr;

        std::string text;
        if (file.exists()) {
            auto read = asp::fs::readToString(file);
            if (read.isErr()) {
                m_unreadable.insert(file);
                m_plan.stats.warnings.push_back(
                    fmt::format("left {} alone, it can't be read: {}", file.string(), read.unwrapErr().message()));
                return nullptr;
            }
            text = std::move(read).unwrap();
        }
        return &m_texts.emplace(file, std::move(text)).first->second;
    }

    void Planner::commit(Path const& file) { m_dirty.insert(file); }

    void Planner::handle_class(ExtractedClass const& cls, Platform platform) {
        auto& stats = m_plan.stats;
        if (m_options.ignore.matches(cls.name)) {
            ++stats.classes_skipped;
            return;
        }
        if (cls.name.find_first_of("(`' ") != std::string::npos) {
            stats.warnings.push_back(fmt::format("skipped '{}': Broma can't name it", cls.name));
            ++stats.classes_skipped;
            return;
        }

        auto where = m_class_file.find(cls.name);
        if (where == m_class_file.end()) {
            if (m_options.only_template_classes && m_root) {
                ++stats.classes_skipped;
                return;
            }
            create_class(cls, platform);
            return;
        }
        update_class(cls, where->second, platform);
    }

    void Planner::create_class(ExtractedClass const& cls, Platform platform) {
        auto const& file = m_options.default_file;
        if (file.empty()) {
            ++m_plan.stats.classes_skipped;
            return;
        }

        bool const linked = links_by_symbol(cls, platform);
        broma::Class out;
        out.name = cls.name;
        if (linked) out.attributes.links = broma::Platform::Android;
        for (auto const& base : cls.bases)
            out.superclasses.push_back({base, broma::Platform::All});

        std::vector<ExtractedMethod const*> virtuals, others;
        for (auto const& m : cls.methods)
            (m.sig.is_virtual ? virtuals : others).push_back(&m);
        auto slot = [&](ExtractedMethod const* m) {
            auto it = std::ranges::find(cls.vtable, m->sig.overload_key());
            return it == cls.vtable.end() ? cls.vtable.size() : static_cast<std::size_t>(it - cls.vtable.begin());
        };
        std::ranges::stable_sort(virtuals, [&](auto* a, auto* b) { return slot(a) < slot(b); });
        std::ranges::stable_sort(others, [](auto* a, auto* b) { return a->sig.name < b->sig.name; });

        std::array const ordered{std::span{virtuals}, std::span{others}};
        for (auto const* m : ordered | std::views::join) {
            out.fields.push_back(broma::Field{0, out.name, new_field(*m, linked, platform), 0});
        }

        auto* target = text_of(file);
        if (!target) {
            ++m_plan.stats.classes_skipped;
            return;
        }
        auto& text = *target;
        if (!text.empty() && !text.ends_with("\n\n")) text += text.ends_with('\n') ? "\n" : "\n\n";
        text += write_class(out);
        commit(file);

        m_class_file.emplace(cls.name, file);
        ++m_plan.stats.classes_created;
        log.debug("Created class '{}' in {}", cls.name, file.string());
    }

    broma::FunctionBindField Planner::new_field(ExtractedMethod const& m, bool linked, Platform platform) {
        auto& stats = m_plan.stats;
        auto const return_type = return_type_for(m.sig);

        broma::FunctionBindField field;
        field.prototype = to_proto(m.sig, return_type, m_options.include_param_names);
        if (!linked) {
            offset_for(field.binds, platform) = m.bind.to_broma();
            ++stats.bindings_created;
        }
        if (!m.sig.is_ctor_or_dtor() && return_type != "TodoReturn") ++stats.return_types;
        if (m_options.include_param_names) {
            stats.param_names +=
                asp::iter::from(m.sig.params)
                    .filter([](Param const& p) { return !p.name.empty() && !is_generated_name(p.name); })
                    .count();
        }
        ++stats.methods_created;
        return field;
    }

    bool Planner::links_by_symbol(ExtractedClass const& cls, Platform platform) const {
        return m_options.mark_link && cls.exported && is_android(platform);
    }

    std::string Planner::return_type_for(FunctionSignature const& sig) const {
        if (sig.is_ctor_or_dtor()) return {};
        if (m_options.include_return_types && !sig.ret.is_unknown_return()) return sig.ret.str();
        return "TodoReturn";
    }

    void Planner::update_class(ExtractedClass const& cls, Path const& file, Platform platform) {
        auto& stats = m_plan.stats;
        auto* source = text_of(file);
        if (!source) return;
        auto& text = *source;

        auto span = find_class(text, cls.name);
        if (!span) {
            stats.warnings.push_back(
                fmt::format(
                    "class '{}' is declared in {} but could not be located in its text", cls.name, file.string()));
            return;
        }

        auto statements = parse_statements(text, *span);
        std::unordered_map<std::string, std::size_t> by_key;
        for (auto const& [i, statement] : asp::iter::enumerate(statements))
            by_key.emplace(statement.key, i);

        ClassPatch patch;
        bool changed = false;
        bool const linked = links_by_symbol(cls, platform);

        for (auto const& m : cls.methods) {
            auto hit = by_key.find(statement_key(m.sig));
            if (hit == by_key.end()) {
                auto field = new_field(m, linked, platform);
                patch.additions.push_back(write_function(field.prototype, field.binds, {}, ""));
                changed = true;
                continue;
            }

            auto& existing = statements[hit->second];
            auto field = existing.field;
            bool edited = false;

            auto& slot = offset_for(field.binds, platform);
            auto const found = m.bind.to_broma();
            if (!linked && (slot == -1 || (slot == -2 && found >= 0))) {
                slot = found;
                ++stats.bindings_added;
                edited = true;
            } else if (!linked && slot != found && slot != -2) {
                if (m_prompter.overwrite_address && m_prompter.overwrite_address(
                                                        fmt::format("{}::{}", cls.name, m.sig.name),
                                                        Bind::from_broma(slot).to_string(), m.bind.to_string())) {
                    slot = found;
                    ++stats.bindings_updated;
                    edited = true;
                } else {
                    ++stats.mismatches_kept;
                }
            }

            if (m_options.include_return_types && !m.sig.is_ctor_or_dtor() && !m.sig.ret.is_unknown_return() &&
                TypeRef{field.prototype.ret.name}.is_unknown_return()) {
                field.prototype.ret.name = m.sig.ret.str();
                ++stats.return_types;
                edited = true;
            }

            if (m_options.include_param_names && field.prototype.args.size() == m.sig.params.size()) {
                for (std::size_t i = 0; i < m.sig.params.size(); ++i) {
                    auto const& wanted = m.sig.params[i].name;
                    auto& current = field.prototype.args[i].second;
                    if (!wanted.empty() && !is_generated_name(wanted) && is_generated_name(current)) {
                        current = wanted;
                        ++stats.param_names;
                        edited = true;
                    }
                }
            }

            if (edited) {
                patch.replacements.push_back(
                    {existing.index, write_function(field.prototype, field.binds, field.inner, "")});
                changed = true;
            }
        }

        warn_if_virtual_order_differs(cls, statements);

        if (linked) {
            if (auto const* in_root = find_in_root(cls.name);
                in_root && !has_platform(in_root->attributes.links, platform)) {
                auto copy = *in_root;
                copy.attributes.links = copy.attributes.links | broma::Platform::Android;
                patch.new_header = write_class_header(copy);
                changed = true;
            }
        }

        if (!changed) return;
        if (auto patched = patch_class(text, cls.name, patch)) {
            text = std::move(*patched);
            commit(file);
        }
    }

    broma::Class const* Planner::find_in_root(std::string_view name) const {
        if (!m_root) return nullptr;
        auto it = std::ranges::find(m_root->classes, name, &broma::Class::name);
        return it == m_root->classes.end() ? nullptr : &*it;
    }

    void Planner::warn_if_virtual_order_differs(
        ExtractedClass const& cls, std::vector<ParsedStatement> const& statements) {
        if (cls.vtable.size() < 2) return;
        auto in_file = asp::iter::from(statements)
                           .filter([](ParsedStatement const& s) { return s.field.prototype.is_virtual; })
                           .map([](ParsedStatement const& s) { return s.key.substr(0, s.key.find("|const")); })
                           .collect<std::vector<std::string>>();
        auto shared_with = [](std::vector<std::string> const& keys, std::vector<std::string> const& other) {
            return asp::iter::from(keys)
                .filter([&](std::string const& key) { return asp::iter::contains(other, key); })
                .copied()
                .collect<std::vector<std::string>>();
        };
        auto in_binary = shared_with(cls.vtable, in_file);
        auto filtered = shared_with(in_file, in_binary);
        if (filtered != in_binary) {
            m_plan.stats.warnings.push_back(
                fmt::format(
                    "virtual functions of '{}' are ordered differently from the vtable in the binary", cls.name));
        }
    }

    void Planner::handle_free_functions(ExtractedBinary const& binary) {
        if (binary.free_functions.empty() || m_options.default_file.empty()) return;

        std::set<std::string> known;
        if (m_root) {
            for (auto const& f : m_root->functions) {
                known.insert(FunctionSignature::from_proto(f.prototype, binary.platform).overload_key());
            }
        }

        std::vector<std::string> lines;
        for (auto const& fn : binary.free_functions) {
            if (m_options.ignore.matches(fn.sig.name) || !known.insert(fn.sig.overload_key()).second) continue;
            if (m_options.only_template_classes && m_root) continue;
            broma::Function out;
            out.prototype = to_proto(fn.sig, return_type_for(fn.sig), m_options.include_param_names);
            offset_for(out.binds, binary.platform) = fn.bind.to_broma();
            lines.push_back(write_free_function(out));
            ++m_plan.stats.methods_created;
            ++m_plan.stats.bindings_created;
        }
        if (lines.empty()) return;

        auto* target = text_of(m_options.default_file);
        if (!target) return;
        auto& text = *target;
        if (!text.empty() && !text.ends_with('\n')) text += '\n';
        for (auto const& line : lines)
            text += line + '\n';
        commit(m_options.default_file);
    }

    bool IgnoreRules::matches(std::string_view class_name) const {
        if (asp::iter::contains(classes, class_name)) return true;
        return asp::iter::from(namespaces).any([&](std::string const& ns) {
            return class_name.starts_with(ns) && class_name.substr(ns.size()).starts_with("::");
        });
    }

    geode::Result<ExportPlan, std::string> plan_export(
        ExtractedBinary const& binary, broma::Root const* template_root, ExportOptions const& options,
        ExportPrompter& prompter) {
        return plan_export(std::span{&binary, 1}, template_root, options, prompter);
    }

    geode::Result<ExportPlan, std::string> plan_export(
        std::span<ExtractedBinary const> binaries, broma::Root const* template_root, ExportOptions const& options,
        ExportPrompter& prompter) {
        if (!template_root && options.default_file.empty()) {
            return geode::Err(std::string{"nothing to export into: no template and no default output file"});
        }
        ExportPlan plan;
        Planner planner{template_root, options, prompter, plan};
        for (auto const& binary : binaries)
            planner.run(binary);
        return geode::Ok(std::move(plan));
    }

    geode::Result<void, std::string> apply_plan(ExportPlan const& plan) {
        std::vector<std::string> done;
        for (auto const& [path, text] : plan.files) {
            if (auto written = write_atomic(path, text); written.isErr()) {
                if (done.empty()) return written;
                return geode::Err(
                    fmt::format(
                        "{}\n\nThese files were already changed:\n{}", written.unwrapErr(), fmt::join(done, "\n")));
            }
            done.push_back(path.string());
        }
        return geode::Ok();
    }

    std::string export_function_line(FunctionSignature const& sig, Bind bind, Platform platform) {
        auto named = sig;
        auto const own_name = cpp::split_qualified(named.class_name).bare;
        if (named.name.starts_with('~')) named.is_dtor = true;
        if (!own_name.empty() && named.name == own_name) named.is_ctor = true;

        broma::MemberFunctionProto proto = to_proto(named);
        if (named.ret.is_unknown_return() && !named.is_ctor_or_dtor()) proto.ret.name = "TodoReturn";
        broma::PlatformNumber binds;
        offset_for(binds, platform) = bind.to_broma();
        return write_function(proto, binds, {}, "");
    }
}  // namespace bromaida
