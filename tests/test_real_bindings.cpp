#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/broma/signature.hpp>
#include <bromaida/typegen/bundle.hpp>
#include <bromaida/typegen/class_graph.hpp>
#include <bromaida/typegen/codegen.hpp>
#include <bromaida/typegen/profile.hpp>
#include <bromaida/util/text.hpp>

#include "common.hpp"

using namespace bromaida;

static bool is_unset(char const* name) { return std::getenv(name) == nullptr; }

static std::string forward_declaration_of(std::string_view name) {
    auto q = cpp::split_qualified(name);
    return cpp::wrap_namespaces(q.namespaces, "class " + q.bare + ";", true);
}

static bool balanced(std::string_view text, char open, char close) {
    long depth = 0;
    for (char c : text) {
        if (c == open) ++depth;
        if (c == close && --depth < 0) return false;
    }
    return depth == 0;
}

TEST_CASE("real bindings parse and generate for every platform" * doctest::skip(is_unset("BROMAIDA_TEST_BROMA"))) {
    Path const entry = std::getenv("BROMAIDA_TEST_BROMA");
    char const* data_env = std::getenv("BROMAIDA_TEST_DATA");
    Path const data = data_env ? Path{data_env} : Path{BROMAIDA_SOURCE_DIR} / "data";

    auto profiles = load_profiles(data / "profiles");
    REQUIRE(profiles.errors.empty());
    auto const version = GdVersion::guess(entry);
    REQUIRE(version.has_value());
    auto const* profile = select_profile(profiles.profiles, *version);
    REQUIRE_MESSAGE(profile != nullptr, "no profile for GD ", version->to_string());
    MESSAGE("GD ", version->to_string(), " -> profile ", profile->name);
    auto bundle = load_bundle(data / "sdk" / profile->sdk);
    REQUIRE(bundle.isOk());

    auto root = load_broma(entry);
    std::string const parse_error = root.isErr() ? root.unwrapErr() : std::string{};
    REQUIRE_MESSAGE(root.isOk(), parse_error);
    auto const& broma = root.unwrap();
    CHECK(broma.classes.size() > 100);

    for (auto platform : kAllPlatforms) {
        CAPTURE(to_string(platform));
        std::vector<broma::Class const*> classes;
        std::vector<std::string> unavailable;
        std::size_t bound = 0;
        for (auto const& cls : broma.classes) {
            if (has_platform(cls.attributes.missing, platform)) {
                unavailable.push_back(cls.name);
                continue;
            }
            classes.push_back(&cls);
            bound += static_cast<std::size_t>(std::ranges::count_if(bind_fields(cls), [&](auto const& fn) {
                return Binding::from_field(cls.name, fn, platform).has_address();
            }));
        }
        CHECK(bound >= (is_android(platform) ? std::size_t{0} : std::size_t{1001}));
        CHECK(classes.size() + unavailable.size() == broma.classes.size());

        auto const start = std::chrono::steady_clock::now();
        ClassGraph graph{classes, platform};
        auto choice = resolve_flavor(*profile, platform, data / "stl");
        REQUIRE(choice.flavor.isOk());
        auto const missing_types = missing_std_types(graph.used_std_types(), choice.flavor.unwrap());
        CHECK_MESSAGE(
            missing_types.empty(), "missing from '", choice.flavor.unwrap().manifest.name, "': ", missing_types.size());

        auto output = generate_types_header(
            CodegenInput{
                .platform = platform,
                .classes = classes,
                .unavailable_classes = unavailable,
                .bundle = bundle.unwrap(),
                .defines = profile->defines,
                .broma_dir = parent_dir(entry),
                .generator_version = "test"},
            graph);
        auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        MESSAGE(
            to_string(platform), ": ", classes.size(), " classes, ", bound, " bound, ", output.text.size(),
            " bytes of header in ", ms.count(), " ms");
        if (char const* dump = std::getenv("BROMAIDA_TEST_DUMP")) {
            (void)asp::fs::write(Path{dump} / fmt::format("types_{}.hpp", to_string(platform)), output.text);
        }

        CHECK(output.broma_class_count == classes.size());
        CHECK(output.enum_count > 0);
        CHECK(count_of(output.text, "class CCNode {") + count_of(output.text, "class CCNode : ") >= 1);
        CHECK(output.text.contains("EasingType"));
        CHECK(balanced(output.text, '{', '}'));
        CHECK(balanced(output.text, '(', ')'));
        CHECK(output.text.contains("#define BROMAIDA_GD_2209") == (profile->name == "gd-2.209x"));

        for (auto const& name : unavailable) {
            CAPTURE(name);
            CHECK(output.text.contains(forward_declaration_of(name) + "\n"));
        }

        auto const first_definition = output.text.find("// Broma class definitions");
        REQUIRE(first_definition != std::string::npos);
        for (auto const& name : graph.forward_declarations()) {
            CAPTURE(name);
            auto const at = output.text.find(forward_declaration_of(name));
            REQUIRE(at != std::string::npos);
            CHECK(at < first_definition);
        }
    }
}
