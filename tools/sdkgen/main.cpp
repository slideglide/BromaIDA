// bromaida-sdkgen: writes the part of the types header that Broma doesn't define, taken from the Geode SDK.
// Finds types the bindings reference but never define and copies just those (plus what they need) from the SDK with
// libclang, without bodies or Geode's scaffolding.
//
//   bromaida-sdkgen --bindings <folder of .bro files> --sdk <geode>/loader/include --out <folder>
//                   [--platform android64] [--include-dir <dependency headers>]...
#include <clang-c/Index.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <asp/detail/Function.hpp>
#include <asp/iter.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <bromaida/broma_io.hpp>
#include <bromaida/bundle.hpp>
#include <bromaida/class_graph.hpp>
#include <bromaida/codegen.hpp>
#include <bromaida/profile.hpp>
#include <bromaida/text.hpp>

using namespace bromaida;

static std::string to_string(CXString s) {
    std::string out = clang_getCString(s) ? clang_getCString(s) : "";
    clang_disposeString(s);
    return out;
}

/// clang_visitChildren with a lambda: `f(child)` returns the CXChildVisitResult.
template <class F>
static void visit(CXCursor parent, F&& f) {
    clang_visitChildren(
        parent, [](CXCursor child, CXCursor, CXClientData data) { return (*static_cast<F*>(data))(child); }, &f);
}

static std::string_view access_name(CX_CXXAccessSpecifier access) {
    return access == CX_CXXPrivate ? "private" : access == CX_CXXProtected ? "protected" : "public";
}

static std::string trim_end(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

struct Index {
    CXIndex handle = clang_createIndex(0, 0);
    ~Index() { clang_disposeIndex(handle); }
};

struct Unit {
    CXTranslationUnit tu = nullptr;
    ~Unit() {
        if (tu) clang_disposeTranslationUnit(tu);
    }
};

struct Diagnostic {
    unsigned line = 0;
    std::string file;
    std::string text;
};

struct Parsed {
    Unit unit;
    std::vector<Diagnostic> errors;
};

static Parsed parse(Index const& index, Path const& file, std::vector<std::string> const& args) {
    auto const raw =
        asp::iter::from(args).map([](std::string const& a) { return a.c_str(); }).collect<std::vector<char const*>>();

    Parsed out;
    // bodies are never needed (and are where Geode's own scaffolding lives)
    auto const options = CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_KeepGoing;
    out.unit.tu = clang_parseTranslationUnit(
        index.handle, file.c_str(), raw.data(), static_cast<int>(raw.size()), nullptr, 0, options);
    if (!out.unit.tu) return out;

    for (unsigned i = 0, n = clang_getNumDiagnostics(out.unit.tu); i < n; ++i) {
        CXDiagnostic d = clang_getDiagnostic(out.unit.tu, i);
        if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) {
            Diagnostic diagnostic;
            CXFile f;
            unsigned line = 0;
            clang_getSpellingLocation(clang_getDiagnosticLocation(d), &f, &line, nullptr, nullptr);
            diagnostic.line = line;
            diagnostic.file = f ? to_string(clang_getFileName(f)) : "";
            diagnostic.text = to_string(clang_formatDiagnostic(d, 0));
            out.errors.push_back(std::move(diagnostic));
        }
        clang_disposeDiagnostic(d);
    }
    return out;
}

static std::string file_of(CXCursor c) {
    CXFile f;
    clang_getSpellingLocation(clang_getCursorLocation(c), &f, nullptr, nullptr, nullptr);
    return f ? to_string(clang_getFileName(f)) : "";
}

static bool is_type_decl(CXCursorKind k) {
    return k == CXCursor_ClassDecl || k == CXCursor_StructDecl || k == CXCursor_UnionDecl || k == CXCursor_EnumDecl ||
           k == CXCursor_TypedefDecl || k == CXCursor_TypeAliasDecl;
}

static bool is_record(CXCursorKind k) {
    return k == CXCursor_ClassDecl || k == CXCursor_StructDecl || k == CXCursor_UnionDecl;
}

/// `cocos2d::CCPoint`: the namespaces and classes around a declaration, then its name. std::nullopt for anonymous
/// things and for declarations inside classes or functions, which are printed with their owner.
static std::optional<std::string> namespace_scoped_name(CXCursor c) {
    std::string name = to_string(clang_getCursorSpelling(c));
    if (name.empty()) return std::nullopt;

    std::vector<std::string> scopes;
    for (CXCursor p = clang_getCursorSemanticParent(c); !clang_Cursor_isNull(p); p = clang_getCursorSemanticParent(p)) {
        auto const kind = clang_getCursorKind(p);
        if (kind == CXCursor_TranslationUnit) break;
        if (kind != CXCursor_Namespace) return std::nullopt;
        auto scope = to_string(clang_getCursorSpelling(p));
        if (scope.empty()) return std::nullopt;  // an anonymous namespace
        scopes.push_back(std::move(scope));
    }
    std::ranges::reverse(scopes);
    scopes.push_back(std::move(name));
    return asp::iter::from(scopes).copied().join("::");
}

struct Decl {
    CXCursor cursor;
    std::string name;
    std::string file;
    bool defined = false;
};

using Decls = std::map<std::string, Decl>;

struct IndexContext {
    Decls* decls;
    asp::FunctionRef<bool(std::string const&)> keep;
};

static CXChildVisitResult index_visitor(CXCursor c, CXCursor, CXClientData data) {
    auto& ctx = *static_cast<IndexContext*>(data);
    auto const kind = clang_getCursorKind(c);
    if (kind == CXCursor_Namespace) return CXChildVisit_Recurse;
    if (!is_type_decl(kind)) return CXChildVisit_Continue;

    auto name = namespace_scoped_name(c);
    if (!name) return CXChildVisit_Continue;
    auto file = file_of(c);
    if (!ctx.keep(file)) return CXChildVisit_Continue;

    bool const defined = clang_isCursorDefinition(c) != 0;
    auto& existing = (*ctx.decls)[*name];
    if (existing.name.empty() || (defined && !existing.defined)) existing = Decl{c, *name, file, defined};
    return CXChildVisit_Continue;
}

/// Every namespace-scoped type declaration in the translation unit. A definition replaces a forward declaration of
/// the same name. `keep` decides by file (the SDK's own, not std's).
static Decls index_declarations(CXTranslationUnit tu, asp::FunctionRef<bool(std::string const&)> keep) {
    Decls decls;
    IndexContext context{&decls, keep};
    clang_visitChildren(clang_getTranslationUnitCursor(tu), index_visitor, &context);
    return decls;
}

/// The declaration a type names, looking through pointers, references, arrays and sugar; and whether it is held
/// by value (which needs its definition) or not (a declaration is enough).
static std::optional<std::pair<std::string, bool>> type_target(CXType t) {
    bool by_value = true;
    for (int guard = 0; guard < 16; ++guard) {
        switch (t.kind) {
            case CXType_Pointer:
            case CXType_LValueReference:
            case CXType_RValueReference:
                by_value = false;
                t = clang_getPointeeType(t);
                continue;
            case CXType_ConstantArray:
            case CXType_IncompleteArray:
            case CXType_VariableArray: t = clang_getArrayElementType(t); continue;
            case CXType_Elaborated: t = clang_Type_getNamedType(t); continue;
            default: break;
        }
        break;
    }
    CXCursor decl = clang_getTypeDeclaration(t);
    if (clang_Cursor_isNull(decl) || !is_type_decl(clang_getCursorKind(decl))) return std::nullopt;
    auto name = namespace_scoped_name(decl);
    if (!name) return std::nullopt;
    return std::pair{*name, by_value};
}

struct Dependency {
    std::string name;
    bool hard;  ///< Needs the definition; otherwise a forward declaration is enough.
};

static std::vector<Dependency> dependencies(CXCursor decl) {
    std::vector<Dependency> out;
    auto const add = [&](CXType t, bool soft) {
        if (auto target = type_target(t)) out.push_back({target->first, target->second && !soft});
    };

    auto const kind = clang_getCursorKind(decl);
    if (kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
        add(clang_getTypedefDeclUnderlyingType(decl), false);
    } else if (is_record(kind)) {
        visit(decl, [&](CXCursor c) {
            switch (clang_getCursorKind(c)) {
                case CXCursor_CXXBaseSpecifier:
                case CXCursor_FieldDecl: add(clang_getCursorType(c), false); break;
                case CXCursor_VarDecl: add(clang_getCursorType(c), true); break;  // static member
                case CXCursor_CXXMethod:
                case CXCursor_Constructor:
                case CXCursor_Destructor:
                    add(clang_getCursorResultType(c), true);
                    for (int i = 0, n = clang_Cursor_getNumArguments(c); i < n; ++i)
                        add(clang_getCursorType(clang_Cursor_getArgument(c, static_cast<unsigned>(i))), true);
                    break;
                default: break;
            }
            return CXChildVisit_Continue;
        });
    }
    return out;
}

static constexpr auto kScaffoldingMarkers = std::to_array<std::string_view>({"geode::", "tulip::", "Geode", "GEODE"});

/// Scaffolding that the SDK adds around every class: it only means something to Geode's build.
static bool is_geode_scaffolding(std::string const& text) {
    return asp::iter::from(kScaffoldingMarkers).any([&](std::string_view marker) { return text.contains(marker); });
}

static std::string pretty(CXCursor c, CXPrintingPolicy policy) {
    auto text = to_string(clang_getCursorPrettyPrinted(c, policy));
    text = cpp::replace_all(std::move(text), "__null", "nullptr");  // what clang makes of NULL
    return trim_end(std::move(text));
}

static std::string print_declaration(Decl const& decl) {
    CXCursor c = decl.cursor;
    CXPrintingPolicy policy = clang_getCursorPrintingPolicy(c);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_TerseOutput, 0);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_IncludeTagDefinition, 1);

    std::string out;
    auto const kind = clang_getCursorKind(c);
    if (!is_record(kind)) {
        out = pretty(c, policy);
        if (!out.ends_with(';')) out += ';';
        clang_PrintingPolicy_dispose(policy);
        return out;
    }

    // head: keyword, name, bases
    auto const keyword = kind == CXCursor_ClassDecl ? "class" : kind == CXCursor_StructDecl ? "struct" : "union";
    out = fmt::format("{} {}", keyword, to_string(clang_getCursorSpelling(c)));

    std::vector<std::string> bases;
    visit(c, [&](CXCursor b) {
        if (clang_getCursorKind(b) == CXCursor_CXXBaseSpecifier) {
            bases.push_back(
                fmt::format(
                    "{} {}{}", access_name(clang_getCXXAccessSpecifier(b)), clang_isVirtualBase(b) ? "virtual " : "",
                    to_string(clang_getTypeSpelling(clang_getCursorType(b)))));
        }
        return CXChildVisit_Continue;
    });
    if (!bases.empty()) out += fmt::format(" : {}", fmt::join(bases, ", "));
    out += " {\n";

    // members, with the access labels in between
    visit(c, [&](CXCursor m) {
        switch (clang_getCursorKind(m)) {
            case CXCursor_CXXAccessSpecifier:
                out += fmt::format("{}:\n", access_name(clang_getCXXAccessSpecifier(m)));
                return CXChildVisit_Continue;
            case CXCursor_CXXBaseSpecifier:
            case CXCursor_FriendDecl:
            case CXCursor_StaticAssert:
            case CXCursor_UnexposedDecl: return CXChildVisit_Continue;
            default: break;
        }
        auto text = pretty(m, policy);
        if (text.empty() || is_geode_scaffolding(text)) return CXChildVisit_Continue;
        if (!text.ends_with(';')) text += ';';
        out += "    " + cpp::replace_all(text, "\n", "\n    ") + "\n";
        return CXChildVisit_Continue;
    });
    out += "};";
    clang_PrintingPolicy_dispose(policy);
    return out;
}

struct Options {
    Path bindings;
    Path sdk;  ///< <geode>/loader/include
    Path out;
    Path data = Path{BROMAIDA_DATA_DIR};
    Path stubs = Path{BROMAIDA_SDKGEN_STUBS};
    std::string platform = "android64";
    bool install = false;  ///< Also write the result into the bundle, where the plugin reads it.
    std::vector<Path> include_dirs;
    std::vector<std::string> umbrella{"Geode/cocos/include/cocos2d.h", "Geode/cocos/extensions/cocos-ext.h"};
};

[[noreturn]] static void fail(std::string const& message) {
    std::fprintf(stderr, "bromaida-sdkgen: %s\n", message.c_str());
    std::exit(1);
}

static Options read_options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) fail(fmt::format("{} needs a value", arg));
            return argv[++i];
        };
        if (arg == "--bindings")
            o.bindings = Path{value()};
        else if (arg == "--sdk")
            o.sdk = Path{value()};
        else if (arg == "--out")
            o.out = Path{value()};
        else if (arg == "--data")
            o.data = Path{value()};
        else if (arg == "--stubs")
            o.stubs = Path{value()};
        else if (arg == "--platform")
            o.platform = value();
        else if (arg == "--install")
            o.install = true;
        else if (arg == "--include-dir")
            o.include_dirs.push_back(Path{value()});
        else if (arg == "--umbrella")
            o.umbrella.push_back(value());
        else
            fail(fmt::format("unknown argument {}", arg));
    }
    if (o.bindings.empty() || o.sdk.empty() || o.out.empty()) {
        fail(
            "usage: bromaida-sdkgen --bindings <folder> --sdk <geode>/loader/include --out <folder> "
            "[--platform android64|android32] [--install] [--include-dir <dir>]...");
    }
    return o;
}

/// The compiler arguments that make the SDK parse as a given platform. Only the declarations matter, so the host's
/// own standard library is used (the SDK needs a real one) and the platform is chosen with Geode's own macros.
static std::vector<std::string> sdk_arguments(Options const& o, std::string const& resource_dir) {
    std::vector<std::string> args{"-x", "c++", "-std=c++23", "-ferror-limit=0", "-resource-dir=" + resource_dir};
    if (o.platform == "android64") {
        args.insert(args.end(), {"-D__ANDROID__", "-DANDROID", "-D__aarch64__"});
    } else if (o.platform == "android32") {
        args.insert(args.end(), {"-D__ANDROID__", "-DANDROID", "-D__arm__"});
    } else {
        fail(
            fmt::format(
                "platform '{}' is not supported yet: the SDK's Windows, macOS and iOS parts need their own stub system "
                "headers (see tools/sdkgen/stubs). android64 and android32 work.",
                o.platform));
    }
    args.insert(args.end(), {"-include", "cstdint", "-include", "cstring"});
    args.push_back("-isystem" + o.stubs.string());
    args.push_back("-isystem" + (o.stubs / "android").string());
    for (auto const& dir : o.include_dirs)
        args.push_back("-isystem" + dir.string());
    args.push_back("-I" + o.sdk.string());
    args.push_back("-I" + (parent_dir(o.bindings) / "include").string());  // Geode/Enums.hpp
    args.push_back("-I" + (o.sdk / "Geode/cocos/include").string());
    return args;
}

static std::string command_output(std::string const& command) {
    std::string out;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[512];
        while (std::fgets(buffer, sizeof buffer, pipe))
            out += buffer;
        pclose(pipe);
    }
    return trim_end(std::move(out));
}

/// The signatures of a class's virtual functions, in vtable order, as far as can be compared across the two
/// sources: names, argument counts and primitive return types.
static std::vector<std::string> virtual_signatures(CXCursor record) {
    std::vector<std::string> out;
    visit(record, [&](CXCursor m) {
        auto kind = clang_getCursorKind(m);
        if ((kind != CXCursor_CXXMethod && kind != CXCursor_Destructor) || !clang_CXXMethod_isVirtual(m)) {
            return CXChildVisit_Continue;
        }
        auto ret_text = to_string(clang_getTypeSpelling(clang_getCanonicalType(clang_getCursorResultType(m))));
        // only primitives compare reliably: the two sources spell library types differently
        bool const primitive = !ret_text.contains("::") && !ret_text.contains('<');
        out.push_back(
            fmt::format(
                "{} {}({} args)", primitive ? ret_text : "?", to_string(clang_getCursorSpelling(m)),
                clang_Cursor_getNumArguments(m)));
        return CXChildVisit_Continue;
    });
    return out;
}

int main(int argc, char** argv) {
    auto const options = read_options(argc, argv);
    auto const resource_dir = command_output("clang -print-resource-dir");
    if (resource_dir.empty()) fail("clang -print-resource-dir printed nothing: is clang installed?");

    auto const platform = platform_from_string(options.platform).value_or(Platform::Android64);
    auto const out_dir = options.out;
    if (auto made = asp::fs::createDirAll(out_dir); made.isErr()) fail("cannot create the output folder");

    // what the plugin would generate without a supplement
    auto folder = load_broma_folder(options.bindings);
    if (folder.isErr()) fail(folder.unwrapErr());
    auto loaded = std::move(folder).unwrap();

    auto profiles = load_profiles(options.data / "profiles");
    auto version = GdVersion::guess(options.bindings);
    Profile const* profile = version ? select_profile(profiles.profiles, *version) : nullptr;
    if (!profile) profile = select_profile(profiles.profiles, GdVersion{99, 9999});
    if (!profile) fail("no profiles found in " + (options.data / "profiles").string());
    std::printf("profile %s, platform %s\n", profile->name.c_str(), options.platform.c_str());

    auto bundle = load_bundle(options.data / "sdk" / profile->sdk);
    if (bundle.isErr()) fail(bundle.unwrapErr());
    // what the supplement is computed against must not already contain a previous supplement
    constexpr std::string_view kSupplementName = "sdk_supplement.hpp";
    std::erase_if(bundle.unwrap().manifest.headers, [&](BundleEntry const& e) { return e.file == kSupplementName; });
    auto choice = resolve_flavor(*profile, platform, options.data / "stl");
    if (choice.flavor.isErr()) fail(choice.flavor.unwrapErr());
    auto const flavor = std::move(choice.flavor).unwrap();

    std::vector<broma::Class const*> classes;
    std::vector<std::string> unavailable;
    for (auto const& cls : loaded.root.classes) {
        if (has_platform(cls.attributes.missing, platform)) {
            unavailable.push_back(cls.name);
        } else {
            classes.push_back(&cls);
        }
    }
    ClassGraph graph{classes, platform};

    auto const generate = [&](Bundle const& with) {
        return generate_types_header(
            CodegenInput{
                .platform = platform,
                .classes = classes,
                .unavailable_classes = unavailable,
                .bundle = with,
                .defines = profile->defines,
                .broma_dir = options.bindings,
                .generator_version = "sdkgen"},
            graph);
    };
    auto baseline = generate(bundle.unwrap());
    auto const baseline_file = out_dir / "baseline.hpp";
    (void)asp::fs::write(baseline_file, baseline.text);

    // what the baseline already declares
    Index index;
    std::vector<std::string> baseline_args{
        "-x",
        "c++",
        "-std=c++17",
        "-ferror-limit=0",
        "-resource-dir=" + resource_dir,
        "-nostdlibinc",
        "-nostdinc++",
        "-isystem" + flavor.dir.string()};
    auto baseline_parsed = parse(index, baseline_file, baseline_args);
    if (!baseline_parsed.unit.tu) fail("libclang could not parse the baseline header");
    auto const known = index_declarations(baseline_parsed.unit.tu, [](std::string const&) { return true; });

    // referenced, but defined nowhere
    std::set<std::string> wanted;
    for (auto const& name : graph.referenced_types()) {
        if (!known.contains(name)) wanted.insert(name);
    }
    std::printf("%zu types are referenced by the bindings and not declared by the baseline\n", wanted.size());

    // the SDK
    std::string umbrella_text;
    for (auto const& header : options.umbrella)
        umbrella_text += fmt::format("#include <{}>\n", header);
    auto const umbrella_file = out_dir / "umbrella.cpp";
    (void)asp::fs::write(umbrella_file, umbrella_text);

    auto sdk_parsed = parse(index, umbrella_file, sdk_arguments(options, resource_dir));
    if (!sdk_parsed.unit.tu) fail("libclang could not parse the SDK");
    std::printf("SDK parsed with %zu errors\n", sdk_parsed.errors.size());
    for (std::size_t i = 0; i < sdk_parsed.errors.size() && i < 5; ++i)
        std::printf("  %s\n", sdk_parsed.errors[i].text.c_str());

    auto const sdk_root = options.sdk.string();
    auto const sdk_root_bindings = (parent_dir(options.bindings) / "include").string();
    auto const sdk = index_declarations(sdk_parsed.unit.tu, [&](std::string const& file) {
        return file.starts_with(sdk_root) || file.starts_with(sdk_root_bindings);
    });

    // the closure of what is wanted
    std::map<std::string, std::set<std::string>> hard;
    std::set<std::string> soft, picked, unresolved;
    std::deque<std::string> queue;
    for (auto const& name : wanted) {
        if (sdk.contains(name)) {
            queue.push_back(name);
        } else {
            unresolved.insert(name);
        }
    }
    while (!queue.empty()) {
        auto const name = queue.front();
        queue.pop_front();
        if (!picked.insert(name).second) continue;

        for (auto const& dep : dependencies(sdk.at(name).cursor)) {
            // Geode's own types only appear in the scaffolding that is filtered out of the printed classes
            if (dep.name.starts_with("geode::") || dep.name.starts_with("tulip::")) continue;
            if (dep.name == name || !sdk.contains(dep.name)) continue;
            // declared elsewhere (the bindings define it, but later): a forward declaration is still needed here
            if (known.contains(dep.name)) {
                if (!dep.hard) soft.insert(dep.name);
                continue;
            }
            if (dep.hard) {
                hard[name].insert(dep.name);
                queue.push_back(dep.name);
            } else {
                soft.insert(dep.name);
            }
        }
    }
    std::vector<std::string> forward;
    for (auto const& name : soft) {
        if (!picked.contains(name)) forward.push_back(name);
    }

    // definitions after the definitions they need
    std::vector<std::string> order;
    std::set<std::string> visiting, done;
    std::function<void(std::string const&)> place = [&](std::string const& name) {
        if (done.contains(name) || !visiting.insert(name).second) return;
        for (auto const& dep : hard[name])
            place(dep);
        done.insert(name);
        order.push_back(name);
    };
    for (auto const& name : picked)
        place(name);

    // print
    std::string text = fmt::format(
        "// Generated by bromaida-sdkgen from {} ({}), do not modify.\n"
        "// Types the bindings refer to that nothing else declares, with what they need.\n\n",
        options.sdk.string(), options.platform);
    auto emit = [&](std::string const& name, std::string const& body) {
        auto q = cpp::split_qualified(name);
        text += cpp::wrap_namespaces(q.namespaces, body, false) + "\n";
    };
    for (auto const& name : forward) {
        auto const& decl = sdk.at(name);
        auto const keyword = is_record(clang_getCursorKind(decl.cursor)) ? "class" : nullptr;
        if (keyword) emit(name, fmt::format("class {};", cpp::split_qualified(name).bare));
    }
    if (!forward.empty()) text += "\n";
    for (auto const& name : order)
        emit(name, print_declaration(sdk.at(name)));

    auto const supplement_file = out_dir / "sdk_supplement.hpp";
    if (auto written = asp::fs::write(supplement_file, text); written.isErr()) fail("cannot write the supplement");

    if (options.install) {
        auto target = options.data / "sdk" / profile->sdk / kSupplementName;
        if (auto written = asp::fs::write(target, text); written.isErr()) fail("cannot write " + target.string());
        std::printf("installed into %s\n", target.string().c_str());
    }

    std::printf("\n%zu declarations written to %s\n", order.size(), supplement_file.string().c_str());
    for (auto const& name : order)
        std::printf("  + %s\n", name.c_str());
    for (auto const& name : forward)
        std::printf("  . %s (forward declaration)\n", name.c_str());
    for (auto const& name : unresolved)
        std::printf("  ! %s is referenced but is in neither the bundle nor the SDK\n", name.c_str());

    // stale definitions: classes the bundle defines differently from the SDK
    std::map<std::string, CXCursor> defined_in_baseline;
    for (auto const& [name, decl] : known) {
        if (decl.defined && is_record(clang_getCursorKind(decl.cursor))) defined_in_baseline.emplace(name, decl.cursor);
    }
    std::size_t stale = 0;
    for (auto const& [name, cursor] : defined_in_baseline) {
        auto it = sdk.find(name);
        if (it == sdk.end() || !it->second.defined) continue;
        if (graph.find(name)) continue;  // Broma defines it itself
        auto a = virtual_signatures(cursor);
        auto b = virtual_signatures(it->second.cursor);
        if (a.empty() && b.empty()) continue;
        if (a != b) {
            ++stale;
            std::printf("\nstale: %s differs from the SDK\n", name.c_str());
            for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
                auto const left = i < a.size() ? a[i] : "(none)";
                auto const right = i < b.size() ? b[i] : "(none)";
                if (left != right)
                    std::printf("  virtual #%zu: bundle `%s`, SDK `%s`\n", i, left.c_str(), right.c_str());
            }
        }
    }
    if (stale == 0) std::printf("\nno stale class definitions found\n");

    // check the result the way the plugin will use it
    auto const check_dir = out_dir / "bundle";
    (void)asp::fs::removeDirAll(check_dir, false);
    if (auto copied = asp::fs::copy(bundle.unwrap().dir, check_dir); copied.isErr()) fail("cannot copy the bundle");
    (void)asp::fs::write(check_dir / "sdk_supplement.hpp", text);

    auto with_supplement = bundle.unwrap();
    with_supplement.dir = check_dir;
    // right after the last bundled header, before the Broma classes
    // after cocos2d.hpp, before fmod: the types it needs come first
    BundleEntry supplement;
    supplement.file = std::string{kSupplementName};
    supplement.optional = true;
    auto& entries = with_supplement.manifest.headers;
    auto after = std::ranges::find_if(entries, [](BundleEntry const& e) { return e.file == "cocos2d.hpp"; });
    entries.insert(after == entries.end() ? entries.end() : after + 1, supplement);

    auto validated = generate(with_supplement);
    auto const validate_file = out_dir / "validate.hpp";
    (void)asp::fs::write(validate_file, validated.text);

    auto validated_parsed = parse(index, validate_file, baseline_args);
    if (!validated_parsed.unit.tu) fail("libclang could not parse the validated header");

    // errors in the Broma section are the ones that crash IDAClang; the bundled headers have a few of their own
    unsigned section_start = 0;
    for (auto const [i, line] : asp::iter::enumerate(cpp::split_lines(validated.text))) {
        if (line.starts_with("// Broma class forward-declarations")) {
            section_start = static_cast<unsigned>(i) + 1;
            break;
        }
    }
    std::size_t section_errors = 0;
    for (auto const& e : validated_parsed.errors) {
        if (e.file.ends_with("validate.hpp") && e.line >= section_start) {
            ++section_errors;
            std::printf("  error at line %u: %s\n", e.line, e.text.c_str());
        }
    }

    std::vector<std::string> invalid;
    visit(clang_getTranslationUnitCursor(validated_parsed.unit.tu), [&](CXCursor c) {
        if (is_record(clang_getCursorKind(c)) && clang_isCursorDefinition(c) && clang_isInvalidDeclaration(c)) {
            invalid.push_back(to_string(clang_getCursorSpelling(c)));
        }
        return CXChildVisit_Recurse;
    });

    std::printf(
        "\nvalidation: %zu errors in the generated section (%zu in the whole header), %zu invalid classes\n",
        section_errors, validated_parsed.errors.size(), invalid.size());
    for (auto const& name : invalid)
        std::printf("  invalid: %s\n", name.c_str());
    return section_errors == 0 && invalid.empty() ? 0 : 2;
}
