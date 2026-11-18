#include <bromaida/codegen.hpp>

#include <algorithm>
#include <cctype>
#include <asp/iter.hpp>
#include <Geode/Result.hpp>
#include <fmt/format.h>
#include <unordered_set>

#include <asp/fs.hpp>

#include <bromaida/log.hpp>
#include <bromaida/text.hpp>

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

    namespace detail {
        enum class IncludeMode { None, Filter, Parse, EnumsOnly };

        struct Scope {
            bool is_namespace;
            std::string name;
            int depth;
        };
    }  // namespace detail

    using namespace detail;

    static void filter_relative_includes(std::vector<std::string>& lines) {
        for (auto& line : lines) {
            if (line.starts_with("#include \"")) line.insert(0, "// ");
        }
    }

    static void parse_relative_includes(std::vector<std::string>& lines, PathView base_dir) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!lines[i].starts_with("#include \"")) continue;

            auto const first = lines[i].find('"') + 1;
            auto const last = lines[i].find('"', first);
            auto const include = lines[i].substr(first, last - first);
            lines[i].insert(0, "// ");

            auto const full = (base_dir / include).lexicallyNormal();
            auto read = read_lines(full);
            if (read.isErr()) {
                log.warn("Couldn't resolve relative include '{}' from '{}'! Skipping...", include, base_dir.string());
                continue;
            }
            auto nested = std::move(read).unwrap();

            parse_relative_includes(nested, full.parent().value_or(base_dir));
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
            // `class X;` declares, it doesn't define: the Broma class of that name still has to be written
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

    namespace detail {
        class Generator {
        public:
            Generator(CodegenInput const& input, ClassGraph const& graph)
                : m_input(input), m_graph(graph) {}

            CodegenOutput run() {
                auto const target = m_input.platform;
                auto const& plat = info(target);

                m_out.text += fmt::format(
                    "// Generated by BromaIDA v{}, do not modify\n"
                    "#define BROMAIDA_PLATFORM_{}\n"
                    "// platform shorthands\n"
                    "#define BROMAIDA_IS_PLATFORM_MACHO {}\n"
                    "#define BROMAIDA_IS_PLATFORM_ANDROID {}\n\n",
                    m_input.generator_version, plat.macro_suffix, int{is_macho(target)}, int{is_android(target)});

                for (auto const& define : m_input.defines)
                    m_out.text += fmt::format("#define {}\n", define);
                if (!m_input.defines.empty()) m_out.text += "\n";

                for (auto const& entry : m_input.bundle.manifest.headers) {
                    if (!entry.enums.empty()) {
                        m_out.text += "// Geode/Enums.hpp\n";
                        copy((m_input.broma_dir / entry.enums).lexicallyNormal(), IncludeMode::EnumsOnly, true);
                        continue;
                    }
                    auto const mode = entry.includes == "parse"    ? IncludeMode::Parse
                                      : entry.includes == "filter" ? IncludeMode::Filter
                                                                   : IncludeMode::None;
                    if (entry.optional && !(m_input.bundle.dir / entry.file).exists()) continue;
                    copy(entry.file, mode);
                }

                m_out.text += "// typedefs\n";
                m_out.text += "enum class TodoReturn {}; // Geode placeholder for unknown return types\n\n";

                m_out.text += "// Broma class forward-declarations\n";
                std::unordered_set<std::string> declared;
                for (auto const& name : m_graph.forward_declarations()) {
                    if (declared.insert(name).second) m_out.text += forward_declaration(name);
                }
                for (auto const& name : m_input.unavailable_classes) {
                    if (!m_graph.find(name) && declared.insert(name).second) m_out.text += forward_declaration(name);
                }
                m_out.text += "\n";

                m_out.text += "// STL type definitions part 1: Class types by pointer\n";
                m_out.text += m_graph.stl_stubs().by_pointer.emit();
                m_out.text += "\n";

                m_out.text += "// Broma class definitions (in order of hard dependencies)\n";
                for (auto const& name : m_graph.class_order()) {
                    auto const* cls = m_graph.find(name);
                    if (!cls || m_defined.contains(cls->name)) continue;
                    m_out.text += build_class(*cls, m_graph, target);
                    m_defined.insert(cls->name);
                    ++m_out.broma_class_count;
                }
                m_out.text += "\n";

                m_out.text += "// STL type definitions part 2: Class types by value\n";
                m_out.text += m_graph.stl_stubs().by_value.emit();

                m_out.header_class_count = m_header_classes;
                return std::move(m_out);
            }

        private:
            void copy(PathView file, IncludeMode mode = IncludeMode::None, bool no_comment = false) {
                Path const source = file.isAbsolute() ? file.toPath() : (m_input.bundle.dir / file).lexicallyNormal();

                auto read = read_lines(source);
                if (read.isErr()) {
                    log.warn("Skipping header: {}", read.unwrapErr());
                    return;
                }
                auto lines = std::move(read).unwrap();

                if (!no_comment) m_out.text += fmt::format("// {}\n", file.string());

                switch (mode) {
                    case IncludeMode::Filter: filter_relative_includes(lines); break;
                    case IncludeMode::Parse: parse_relative_includes(lines, m_input.bundle.dir); break;
                    case IncludeMode::EnumsOnly: lines = keep_enums_only(lines, m_out.enum_count); break;
                    case IncludeMode::None: break;
                }

                for (auto const& line : lines)
                    m_out.text += line;
                m_out.text += "\n\n";
                m_header_classes += list_class_definitions(lines, m_defined);
            }

            CodegenInput const& m_input;
            ClassGraph const& m_graph;
            CodegenOutput m_out;
            std::unordered_set<std::string> m_defined;
            std::size_t m_header_classes = 0;
        };
    }  // namespace detail

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
        return Generator{input, graph}.run();
    }
}  // namespace bromaida
