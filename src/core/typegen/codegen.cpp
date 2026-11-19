#include <bromaida/typegen/codegen.hpp>

#include <algorithm>
#include <cctype>
#include <asp/iter.hpp>
#include <Geode/Result.hpp>
#include <fmt/format.h>
#include <set>
#include <unordered_set>

#include <asp/fs.hpp>

#include <bromaida/util/log.hpp>
#include <bromaida/util/text.hpp>

namespace bromaida {
    static int brace_delta(std::string_view line) {
        return static_cast<int>(std::ranges::count(line, '{')) - static_cast<int>(std::ranges::count(line, '}'));
    }

    static constexpr Log log{"Codegen"};

    static geode::Result<std::vector<std::string>, std::string> read_lines(PathView path) {
        auto content = asp::fs::readToString(path);
        if (content.isErr())
            return geode::Err(fmt::format("cannot open '{}': {}", path.string(), content.unwrapErr().message()));

        std::string text = std::move(content).unwrap();
        if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);

        std::vector<std::string> lines;
        for (std::size_t pos = 0; pos < text.size();) {
            auto end = text.find('\n', pos);
            end = end == std::string::npos ? text.size() : end + 1;
            lines.push_back(text.substr(pos, end - pos));
            pos = end;
        }
        return geode::Ok(std::move(lines));
    }

    static std::string_view lstrip(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        return s;
    }

    enum class IncludeMode { None, Filter, Parse, EnumsOnly };

    struct Scope {
        bool is_namespace;
        std::string name;
        int depth;
    };

    static void filter_relative_includes(std::vector<std::string>& lines) {
        for (auto& line : lines) {
            if (line.starts_with("#include \"")) line.insert(0, "// ");
        }
    }

    static void parse_relative_includes(std::vector<std::string>& lines, PathView base_dir, std::set<Path>& pasted) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!lines[i].starts_with("#include \"")) continue;

            auto const first = lines[i].find('"') + 1;
            auto const last = lines[i].find('"', first);
            auto const include = lines[i].substr(first, last - first);
            lines[i].insert(0, "// ");

            auto const full = (base_dir / include).lexicallyNormal();
            // a header already pasted (or being pasted) is skipped, like an include guard would
            if (!pasted.insert(full).second) continue;
            auto read = read_lines(full);
            if (read.isErr()) {
                log.warn("Couldn't resolve relative include '{}' from '{}'! Skipping...", include, base_dir.string());
                continue;
            }
            auto nested = std::move(read).unwrap();

            parse_relative_includes(nested, full.parent().value_or(base_dir), pasted);
            nested.emplace_back("\n\n");
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i) + 1, nested.begin(), nested.end());
        }
    }

    static std::vector<std::string> keep_enums_only(std::vector<std::string> const& lines, std::size_t& enum_count) {
        std::vector<std::string> out;
        int macro_depth = 0;
        int brace_depth = 0;
        bool in_enum = false;
        enum_count = 0;

        for (auto const& line : lines) {
            auto const stripped = lstrip(line);
            if (stripped.starts_with("#if") || stripped.starts_with("#ifdef") || stripped.starts_with("#ifndef")) {
                ++macro_depth;
                continue;
            }
            if (stripped.starts_with("#endif")) {
                --macro_depth;
                continue;
            }
            if (macro_depth > 0 || stripped.starts_with('#')) continue;

            if (!in_enum) {
                if (!stripped.starts_with("enum class ")) continue;
                in_enum = true;
                ++enum_count;
            }

            out.push_back(line);
            brace_depth += brace_delta(line);

            if (brace_depth == 0 && line.contains(';')) {
                in_enum = false;
                out.emplace_back("\n");
            }
        }
        return out;
    }

    static std::string_view read_identifier(std::string_view text) {
        std::size_t n = 0;
        while (n < text.size() && (std::isalnum(static_cast<unsigned char>(text[n])) || text[n] == '_'))
            ++n;
        return text.substr(0, n);
    }

    static std::size_t list_class_definitions(
        std::vector<std::string> const& lines, std::unordered_set<std::string>& defined) {
        std::vector<Scope> scopes;
        std::string pending_namespace;
        int depth = 0;
        std::size_t added = 0;

        for (auto const& line : lines) {
            auto const text = lstrip(line);

            if (!pending_namespace.empty() && text.starts_with('{')) {
                scopes.push_back({true, std::move(pending_namespace), depth + 1});
                pending_namespace.clear();
            }

            if (text.starts_with("namespace ")) {
                auto name = std::string{read_identifier(lstrip(text.substr(10)))};
                if (!name.empty()) {
                    if (text.contains('{')) {
                        scopes.push_back({true, std::move(name), depth + 1});
                    } else {
                        pending_namespace = std::move(name);
                    }
                    depth += brace_delta(line);
                    while (!scopes.empty() && depth < scopes.back().depth)
                        scopes.pop_back();
                    continue;
                }
            }

            bool const is_class = text.starts_with("class ");
            bool const is_struct = text.starts_with("struct ");
            // A `class X;` forward declaration doesn't define it, so the Broma class of that name still has to be written
            bool const forward_only = text.contains(';') && !text.contains('{');
            if ((is_class || is_struct) && !forward_only) {
                auto name = std::string{read_identifier(lstrip(text.substr(is_class ? 6 : 7)))};
                if (!name.empty()) {
                    bool const inside_class =
                        asp::iter::from(scopes).any([](Scope const& s) { return !s.is_namespace; });
                    if (!inside_class) {
                        std::string qualified;
                        for (auto const& s : scopes) {
                            if (s.is_namespace) qualified += s.name + "::";
                        }
                        qualified += name;
                        if (defined.insert(std::move(qualified)).second) ++added;
                    }
                    scopes.push_back({false, std::move(name), depth + 1});
                }
            }

            depth += brace_delta(line);
            while (!scopes.empty() && depth < scopes.back().depth)
                scopes.pop_back();
        }
        return added;
    }

    static std::string forward_declaration(std::string_view name) {
        auto q = cpp::split_qualified(name);
        return cpp::wrap_namespaces(q.namespaces, fmt::format("class {};", q.bare), true) + "\n";
    }

    std::string build_class(broma::Class const& cls, ClassGraph const& graph, Platform target) {
        std::vector<std::string> lines;

        for (auto const& sig : graph.own_virtuals(cls.name)) {
            if (sig.is_missing) continue;
            // MSVC gives secondary bases their own vtable, so their overrides don't go here
            if (target == Platform::Windows && graph.is_secondary_override(cls, sig)) continue;
            lines.push_back("\t" + sig.declaration());
        }
        bool const has_virtuals = !lines.empty();

        std::vector<std::string> fields;
        for (auto const& field : cls.fields) {
            if (auto const* member = field.get_as<broma::MemberField>()) {
                if (!has_platform(member->platform, target)) continue;
                auto type = cpp::replace_all(cpp::normalize_type(member->type.name), "geode::", "");
                fields.push_back(
                    fmt::format(
                        "\t{} {}{};", type, member->name,
                        member->count ? fmt::format("[{}]", member->count) : std::string{}));
            } else if (auto const* pad = field.get_as<broma::PadField>()) {
                if (auto const amount = offset_for(pad->amount, target); amount > 0) {
                    fields.push_back(fmt::format("\tPAD({:#x});", amount));
                }
            }
        }

        if (has_virtuals && !fields.empty()) lines.emplace_back();
        lines.insert(lines.end(), fields.begin(), fields.end());

        std::string bases;
        for (auto const& base : graph.bases_of(cls)) {
            bases += bases.empty() ? "public " : ", public ";
            bases += base;
        }

        auto q = cpp::split_qualified(cls.name);
        std::string declaration = fmt::format("class {}{} {{", q.bare, bases.empty() ? "" : " : " + bases);
        if (!lines.empty()) {
            declaration += "\npublic:\n";
            for (auto const& line : lines)
                declaration += line + "\n";
        }
        declaration += "};";

        return cpp::wrap_namespaces(q.namespaces, declaration) + "\n\n";
    }

    CodegenOutput generate_types_header(CodegenInput const& input, ClassGraph const& graph) {
        CodegenOutput out;
        std::unordered_set<std::string> defined;
        std::size_t header_classes = 0;

        auto copy = [&](PathView file, IncludeMode mode, bool no_comment) {
            Path const source = file.isAbsolute() ? file.toPath() : (input.bundle.dir / file).lexicallyNormal();

            auto read = read_lines(source);
            if (read.isErr()) {
                log.warn("Skipping header: {}", read.unwrapErr());
                return;
            }
            auto lines = std::move(read).unwrap();

            if (!no_comment) out.text += fmt::format("// {}\n", file.string());

            switch (mode) {
                case IncludeMode::Filter: filter_relative_includes(lines); break;
                case IncludeMode::Parse: {
                    PathView const base = source.parent().value_or(PathView{input.bundle.dir});
                    std::set<Path> pasted{source};
                    parse_relative_includes(lines, base, pasted);
                    break;
                }
                case IncludeMode::EnumsOnly: lines = keep_enums_only(lines, out.enum_count); break;
                case IncludeMode::None: break;
            }

            for (auto const& line : lines)
                out.text += line;
            out.text += "\n\n";
            header_classes += list_class_definitions(lines, defined);
        };

        auto const target = input.platform;
        auto const& plat = info(target);

        out.text += fmt::format(
            "// Generated by BromaIDA v{}, do not modify\n"
            "#define BROMAIDA_PLATFORM_{}\n"
            "// platform shorthands\n"
            "#define BROMAIDA_IS_PLATFORM_MACHO {}\n"
            "#define BROMAIDA_IS_PLATFORM_ANDROID {}\n\n",
            input.generator_version, plat.macro_suffix, int{is_macho(target)}, int{is_android(target)});

        for (auto const& define : input.defines)
            out.text += fmt::format("#define {}\n", define);
        if (!input.defines.empty()) out.text += "\n";

        for (auto const& entry : input.bundle.manifest.headers) {
            if (!entry.enums.empty()) {
                out.text += "// Geode/Enums.hpp\n";
                copy((input.broma_dir / entry.enums).lexicallyNormal(), IncludeMode::EnumsOnly, true);
                continue;
            }
            auto const mode = entry.includes == "parse"    ? IncludeMode::Parse
                              : entry.includes == "filter" ? IncludeMode::Filter
                                                           : IncludeMode::None;
            if (entry.optional && !(input.bundle.dir / entry.file).exists()) continue;
            copy(entry.file, mode, false);
        }

        out.text += "// typedefs\n";
        out.text += "enum class TodoReturn {}; // Geode placeholder for unknown return types\n\n";

        out.text += "// Broma class forward-declarations\n";
        std::unordered_set<std::string> declared;
        for (auto const& name : graph.forward_declarations()) {
            if (declared.insert(name).second) out.text += forward_declaration(name);
        }
        for (auto const& name : input.unavailable_classes) {
            if (!graph.find(name) && declared.insert(name).second) out.text += forward_declaration(name);
        }
        out.text += "\n";

        out.text += "// STL type definitions part 1: Class types by pointer\n";
        out.text += graph.stl_stubs().by_pointer.emit();
        out.text += "\n";

        out.text += "// Broma class definitions (in order of hard dependencies)\n";
        for (auto const& name : graph.class_order()) {
            auto const* cls = graph.find(name);
            if (!cls || defined.contains(cls->name)) continue;
            out.text += build_class(*cls, graph, target);
            defined.insert(cls->name);
            ++out.broma_class_count;
        }
        out.text += "\n";

        out.text += "// STL type definitions part 2: Class types by value\n";
        out.text += graph.stl_stubs().by_value.emit();

        out.header_class_count = header_classes;
        return out;
    }
}  // namespace bromaida
