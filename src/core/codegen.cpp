#include <bromaida/codegen.hpp>

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <stdexcept>
#include <unordered_set>

#include <asp/fs.hpp>

#include <bromaida/broma_io.hpp>
#include <bromaida/log.hpp>
#include <bromaida/text.hpp>

namespace bromaida {

static constexpr Log log{"Codegen"};

// ---- file helpers ------------------------------------------------------------

static std::vector<std::string> read_lines(PathView path) {
    auto content = asp::fs::readToString(path);
    if (content.isErr())
        throw std::runtime_error(fmt::format("cannot open '{}': {}", path.string(), content.unwrapErr().message()));

    std::string text = std::move(content).unwrap();
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);  // UTF-8 BOM

    // keep the line terminators: headers are copied back out verbatim
    std::vector<std::string> lines;
    for (std::size_t pos = 0; pos < text.size();) {
        auto end = text.find('\n', pos);
        end = end == std::string::npos ? text.size() : end + 1;
        lines.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return lines;
}

static std::string_view lstrip(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
}

namespace {

// ---- header processing ---------------------------------------------------------

enum class IncludeMode { None, Filter, Parse, EnumsOnly };
}  // namespace

/// Comments out `#include "..."` lines.
static void filter_relative_includes(std::vector<std::string>& lines) {
    for (auto& line : lines) {
        if (line.starts_with("#include \"")) line.insert(0, "// ");
    }
}

/// Inlines `#include "..."` lines, recursively, relative to the including file.
static void parse_relative_includes(std::vector<std::string>& lines, PathView base_dir) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!lines[i].starts_with("#include \"")) continue;

        auto const first = lines[i].find('"') + 1;
        auto const last = lines[i].find('"', first);
        auto const include = lines[i].substr(first, last - first);
        lines[i].insert(0, "// ");

        auto const full = (base_dir / include).lexicallyNormal();
        std::vector<std::string> nested;
        try {
            nested = read_lines(full);
        } catch (std::exception const&) {
            log.warn("Couldn't resolve relative include '{}' from '{}'! Skipping...", include, base_dir.string());
            continue;
        }

        parse_relative_includes(nested, full.parent().value_or(base_dir));
        nested.emplace_back("\n\n");
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i) + 1, nested.begin(), nested.end());
    }
}

/// Keeps only top-level `enum class` declarations, ignoring all #if blocks.
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
        brace_depth += static_cast<int>(std::ranges::count(line, '{'));
        brace_depth -= static_cast<int>(std::ranges::count(line, '}'));

        if (brace_depth == 0 && line.contains(';')) {
            in_enum = false;
            out.emplace_back("\n");
        }
    }
    return out;
}

/// Reads the first identifier of `text`.
static std::string_view read_identifier(std::string_view text) {
    std::size_t n = 0;
    while (n < text.size() && (std::isalnum(static_cast<unsigned char>(text[n])) || text[n] == '_'))
        ++n;
    return text.substr(0, n);
}

/// Records every top-level class/struct of `lines` (namespace-qualified) in `defined`.
/// Returns how many were not known before.
static std::size_t list_class_definitions(
    std::vector<std::string> const& lines, std::unordered_set<std::string>& defined) {
    struct Scope {
        bool is_namespace;
        std::string name;
        int depth;
    };
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
                // keep counting braces below like every other line
                depth +=
                    static_cast<int>(std::ranges::count(line, '{')) - static_cast<int>(std::ranges::count(line, '}'));
                while (!scopes.empty() && depth < scopes.back().depth)
                    scopes.pop_back();
                continue;
            }
        }

        bool const is_class = text.starts_with("class ");
        bool const is_struct = text.starts_with("struct ");
        if (is_class || is_struct) {
            auto name = std::string{read_identifier(lstrip(text.substr(is_class ? 6 : 7)))};
            if (!name.empty()) {
                bool const inside_class = std::ranges::any_of(scopes, [](Scope const& s) { return !s.is_namespace; });
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

        depth += static_cast<int>(std::ranges::count(line, '{')) - static_cast<int>(std::ranges::count(line, '}'));
        while (!scopes.empty() && depth < scopes.back().depth)
            scopes.pop_back();
    }
    return added;
}

static std::string forward_declaration(std::string_view name) {
    auto q = cpp::split_qualified(name);
    return cpp::wrap_namespaces(q.namespaces, fmt::format("class {};", q.bare), true) + "\n";
}

namespace {

// ---- generation ------------------------------------------------------------------

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

        // Some bundled declarations differ between game versions; the bindings tell which one applies.
        if (scroll_wheel_returns_bool()) {
            m_out.text +=
                "// 2.209+: CCMouseDelegate::scrollWheel returns bool\n#define BROMAIDA_SCROLLWHEEL_RETURNS_BOOL\n\n";
        }

        // Geode's "SeedValue" helper classes and other common types.
        copy("helpers.hpp");
        // Enums come straight from the Geode headers: in the bindings repository the .bro folders (bindings/2.2091)
        // sit next to bindings/include/Geode/Enums.hpp, so it is one level up from the folder.
        m_out.text += "// Geode/Enums.hpp\n";
        copy(
            (parent_dir(m_input.broma_dir) / "include/Geode/Enums.hpp").lexicallyNormal(), IncludeMode::EnumsOnly,
            true);
        copy("cocos2d.hpp", IncludeMode::Parse);
        copy("fmod.hpp");

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
    /// True when the bindings declare a `bool scrollWheel(...)`, i.e. the game's CCMouseDelegate returns bool.
    [[nodiscard]] bool scroll_wheel_returns_bool() const {
        return std::ranges::any_of(m_graph.class_order(), [&](std::string const& name) {
            auto const* cls = m_graph.find(name);
            return cls && std::ranges::any_of(bind_fields(*cls), [](broma::FunctionBindField const& fn) {
                       return fn.prototype.name == "scrollWheel" && fn.prototype.ret.name == "bool";
                   });
        });
    }

    void copy(PathView file, IncludeMode mode = IncludeMode::None, bool no_comment = false) {
        Path const source = file.isAbsolute() ? file.toPath() : (m_input.headers_dir / file).lexicallyNormal();

        std::vector<std::string> lines;
        try {
            lines = read_lines(source);
        } catch (std::exception const& e) {
            log.warn("Skipping header: {}", e.what());
            return;
        }

        if (!no_comment) m_out.text += fmt::format("// {}\n", file.string());

        switch (mode) {
            case IncludeMode::Filter: filter_relative_includes(lines); break;
            case IncludeMode::Parse: parse_relative_includes(lines, m_input.headers_dir); break;
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
}  // namespace

// ---- public API ---------------------------------------------------------------------

std::string build_class(broma::Class const& cls, ClassGraph const& graph, Platform target) {
    std::vector<std::string> lines;

    // virtual functions first, in declaration order: that is what builds the vtable
    for (auto const& sig : graph.own_virtuals(cls.name)) {
        if (sig.is_missing) continue;
        // This is a hack that makes MSVC vtables work: secondary-base overrides are
        // skipped because IDA otherwise lays out the vtable wrong. Not needed for Itanium.
        // See https://docs.hex-rays.com/core/types/concepts/cpp-type-details
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
