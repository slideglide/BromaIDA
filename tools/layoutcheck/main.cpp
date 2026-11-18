// bromaida-layoutcheck: checks that the types header BromaIDA generates lays classes out like the real game.
//
// The STL stand-ins (data/stl) are hand-written, so nothing but the game itself says whether `sizeof(std::string)`
// or `sizeof(std::mutex)` is right. The sizes in tests/data/sizes were read from the allocations in real binaries
// (the argument of `operator new` in `Class::create()` / `Class::sharedState()`); this tool generates the header for
// a platform, lets libclang lay the classes out for that platform and compares.
//
//   bromaida-layoutcheck --version 2.2091 --platform android64 --sizes tests/data/sizes/2.2091-android64.txt
//                        [--bindings <folder>] [--data <folder>]
//
// A second mode, `--instantiate <file> --stl <flavor> --platform <p>`, parses a file that explicitly instantiates the
// STL stand-ins and fails on any error: IDAClang instantiates far more than a plain parse does, so a stand-in can look
// fine and still break an import. It needs neither bindings nor sizes.
//
// Without --bindings the folder is $BROMAIDA_TEST_BINDINGS/<version>; if that is not set the tool exits with 77,
// which ctest reports as "skipped". The sizes file has one `Class size` per line (`#` starts a comment).
#include <clang-c/Index.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <bromaida/broma_io.hpp>
#include <bromaida/bundle.hpp>
#include <bromaida/class_graph.hpp>
#include <bromaida/codegen.hpp>
#include <bromaida/profile.hpp>

using namespace bromaida;

static constexpr int kSkipped = 77;

[[noreturn]] static void fail(std::string_view message) {
    fmt::println(stderr, "bromaida-layoutcheck: {}", message);
    std::exit(1);
}

static std::string to_string(CXString s) {
    std::string out = clang_getCString(s) ? clang_getCString(s) : "";
    clang_disposeString(s);
    return out;
}

/// The triple clang lays classes out for.
static std::string target_of(Platform platform) {
    switch (platform) {
        case Platform::Windows: return "x86_64-pc-windows-msvc";
        case Platform::MacIntel: return "x86_64-apple-macos";
        case Platform::MacArm: return "arm64-apple-macos";
        case Platform::iOS: return "arm64-apple-ios";
        case Platform::Android64: return "aarch64-linux-android";
        case Platform::Android32: return "armv7-linux-androideabi";
    }
    return {};
}

struct Options {
    std::string version;
    std::string platform;
    Path sizes;
    Path bindings;
    Path data = BROMAIDA_DATA_DIR;
    Path instantiate;
    std::string stl;
};

static Options read_options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string_view const arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) fail(fmt::format("{} needs a value", arg));
            return argv[++i];
        };
        if (arg == "--version")
            o.version = value();
        else if (arg == "--platform")
            o.platform = value();
        else if (arg == "--sizes")
            o.sizes = Path{value()};
        else if (arg == "--bindings")
            o.bindings = Path{value()};
        else if (arg == "--data")
            o.data = Path{value()};
        else if (arg == "--instantiate")
            o.instantiate = Path{value()};
        else if (arg == "--stl")
            o.stl = value();
        else
            fail(fmt::format("unknown option {}", arg));
    }
    if (!o.instantiate.string().empty()) return o;
    if (o.platform.empty() || o.sizes.string().empty()) fail("--platform and --sizes are required");
    if (o.bindings.string().empty()) {
        char const* const root = std::getenv("BROMAIDA_TEST_BINDINGS");
        if (root == nullptr || o.version.empty()) {
            fmt::println("skipped: set BROMAIDA_TEST_BINDINGS (and give --version) or pass --bindings");
            std::exit(kSkipped);
        }
        o.bindings = Path{root} / o.version;
    }
    return o;
}

/// Collects `qualified name -> size` of every complete class in the unit.
static CXChildVisitResult collect(CXCursor cursor, CXCursor, CXClientData data) {
    auto& sizes = *static_cast<std::map<std::string, long long>*>(data);
    auto const kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_Namespace) return CXChildVisit_Recurse;
    if ((kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl) && clang_isCursorDefinition(cursor)) {
        // qualified name from the semantic parents
        std::string name = to_string(clang_getCursorSpelling(cursor));
        for (auto parent = clang_getCursorSemanticParent(cursor); clang_getCursorKind(parent) == CXCursor_Namespace;
             parent = clang_getCursorSemanticParent(parent)) {
            name = to_string(clang_getCursorSpelling(parent)) + "::" + name;
        }
        auto const size = clang_Type_getSizeOf(clang_getCursorType(cursor));
        if (size >= 0) sizes.emplace(std::move(name), size);
    }
    return CXChildVisit_Continue;
}

/// Parses `file` for the platform with the flavor's headers and counts the errors.
static int instantiate(Options const& options, Platform platform) {
    if (options.stl.empty()) fail("--instantiate needs --stl <flavor>");
    auto const triple = target_of(platform);
    auto const stl_dir = (options.data / "stl" / options.stl).string();
    std::vector<std::string> args{"-x",        "c++",         "-std=c++17", "-target", triple,           "-nostdlib",
                                  "-nostdinc", "-nostdinc++", "-isystem",   stl_dir,   "-ferror-limit=0"};
    std::vector<char const*> raw;
    for (auto const& a : args)
        raw.push_back(a.c_str());

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = nullptr;
    auto const status = clang_parseTranslationUnit2(
        index, options.instantiate.c_str(), raw.data(), static_cast<int>(raw.size()), nullptr, 0,
        CXTranslationUnit_KeepGoing, &tu);
    if (status != CXError_Success || !tu) fail("libclang could not parse the file");

    unsigned errors = 0;
    for (unsigned i = 0, n = clang_getNumDiagnostics(tu); i < n; ++i) {
        CXDiagnostic d = clang_getDiagnostic(tu, i);
        if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) {
            if (errors++ < 8) fmt::println("  {}", to_string(clang_formatDiagnostic(d, 0)));
        }
        clang_disposeDiagnostic(d);
    }
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    fmt::println("STL flavor {} ({}): {} error(s) instantiating the stand-ins", options.stl, triple, errors);
    return errors == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    auto const options = read_options(argc, argv);
    auto const platform = platform_from_string(options.platform);
    if (!platform) fail(fmt::format("unknown platform '{}'", options.platform));
    if (!options.instantiate.string().empty()) return instantiate(options, *platform);

    auto folder = load_broma_folder(options.bindings);
    if (folder.isErr()) fail(folder.unwrapErr());
    auto loaded = std::move(folder).unwrap();

    auto profiles = load_profiles(options.data / "profiles");
    auto version = GdVersion::guess(options.bindings);
    Profile const* profile = version ? select_profile(profiles.profiles, *version) : nullptr;
    if (!profile) fail("no profile for these bindings");
    auto bundle = load_bundle(options.data / "sdk" / profile->sdk);
    if (bundle.isErr()) fail(bundle.unwrapErr());
    auto choice = resolve_flavor(*profile, *platform, options.data / "stl");
    if (choice.flavor.isErr()) fail(choice.flavor.unwrapErr());
    auto const flavor = std::move(choice.flavor).unwrap();

    std::vector<broma::Class const*> classes;
    std::vector<std::string> unavailable;
    for (auto const& cls : loaded.root.classes) {
        if (has_platform(cls.attributes.missing, *platform))
            unavailable.push_back(cls.name);
        else
            classes.push_back(&cls);
    }
    ClassGraph graph{classes, *platform};
    auto header = generate_types_header(
        CodegenInput{
            .platform = *platform,
            .classes = classes,
            .unavailable_classes = unavailable,
            .bundle = bundle.unwrap(),
            .defines = profile->defines,
            .broma_dir = options.bindings,
            .generator_version = "layoutcheck"},
        graph);

    auto const header_file = Path{std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp"} /
                             fmt::format("bromaida-layoutcheck-{}-{}.hpp", options.version, options.platform);
    if (asp::fs::write(header_file, header.text).isErr()) fail("cannot write the generated header");

    auto const triple = target_of(*platform);
    auto const stl_dir = (options.data / "stl" / flavor.manifest.name).string();
    std::vector<std::string> args{"-x",        "c++",         "-std=c++17", "-target", triple,
                                  "-nostdinc", "-nostdinc++", "-isystem",   stl_dir,   "-ferror-limit=0"};
    std::vector<char const*> raw;
    for (auto const& a : args)
        raw.push_back(a.c_str());

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = nullptr;
    auto const error = clang_parseTranslationUnit2(
        index, header_file.c_str(), raw.data(), static_cast<int>(raw.size()), nullptr, 0,
        CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_KeepGoing, &tu);
    if (error != CXError_Success || !tu) fail("libclang could not parse the generated header");

    std::map<std::string, long long> sizes;
    clang_visitChildren(clang_getTranslationUnitCursor(tu), collect, &sizes);

    // the table
    auto text = asp::fs::readToString(options.sizes);
    if (text.isErr()) fail(fmt::format("cannot read {}", options.sizes.string()));
    std::size_t checked = 0, wrong = 0, absent = 0;
    std::string content = text.unwrap();
    for (std::size_t pos = 0; pos < content.size();) {
        auto end = content.find('\n', pos);
        auto line = content.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? content.size() : end + 1;
        if (line.empty() || line.front() == '#') continue;
        auto const space = line.find(' ');
        if (space == std::string::npos) continue;
        auto const name = line.substr(0, space);
        auto const expected = std::strtoll(line.c_str() + space + 1, nullptr, 10);
        ++checked;
        auto it = sizes.find(name);
        if (it == sizes.end()) {
            ++absent;
            fmt::println("  {}: not in the generated header (the game allocates {} bytes)", name, expected);
        } else if (it->second != expected) {
            ++wrong;
            fmt::println("  {}: the header lays it out as {} bytes, the game allocates {}", name, it->second, expected);
        }
    }
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);

    fmt::println(
        "{} {} ({}, STL flavor {}): {} of {} sizes match the real binary", options.version, options.platform, triple,
        flavor.manifest.name, checked - wrong - absent, checked);
    return wrong + absent == 0 ? 0 : 1;
}
