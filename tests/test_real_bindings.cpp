#include <doctest/doctest.h>

#include <chrono>
#include <algorithm>
#include <cstdlib>

#include <bromaida/broma_io.hpp>
#include <bromaida/class_graph.hpp>
#include <bromaida/codegen.hpp>
#include <bromaida/signature.hpp>

using namespace bromaida;

// Runs against a real Geode bindings checkout: BROMAIDA_TEST_BROMA=/path/to/Entry.bro
// BROMAIDA_TEST_HEADERS defaults to data/types of this repository.
TEST_CASE("real bindings parse and generate for every platform") {
    char const* entry_env = std::getenv("BROMAIDA_TEST_BROMA");
    if (entry_env == nullptr) {
        MESSAGE("BROMAIDA_TEST_BROMA is not set, skipping");
        return;
    }
    Path const entry = entry_env;
    char const* headers_env = std::getenv("BROMAIDA_TEST_HEADERS");
    Path const headers = headers_env ? Path{headers_env} : Path{BROMAIDA_SOURCE_DIR} / "data" / "types";

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
            bound += std::ranges::count_if(bind_fields(cls), [&](auto const& fn) {
                return Binding::from_field(cls.name, fn, platform).has_address();
            });
        }
        // Android is bound by symbol (`link`), not by address
        if (!is_android(platform)) CHECK(bound > 1000);

        auto const start = std::chrono::steady_clock::now();
        ClassGraph graph{classes, platform};
        auto output = generate_types_header(
            CodegenInput{
                .platform = platform,
                .classes = classes,
                .unavailable_classes = unavailable,
                .headers_dir = headers,
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
        CHECK(output.text.contains("class CCNode"));
        // the enums come from bindings/include/Geode/Enums.hpp, one level above the folder
        CHECK(output.enum_count > 0);
        CHECK(output.text.contains("EasingType"));
        // classes Broma marks `missing` here are still named by others: they need a forward declaration, or the
        // classes that use them are invalid to clang (IDAClang crashes computing the vtable of such a class)
        for (auto const& name : unavailable) {
            if (name.contains("::")) continue;
            CAPTURE(name);
            CHECK(output.text.contains("class " + name + ";"));
        }
        // scrollWheel is bool from 2.209 on and void before: the header has to agree with the bindings
        if (std::getenv("BROMAIDA_TEST_EXPECT_BOOL_SCROLLWHEEL"))
            CHECK(output.text.contains("#define BROMAIDA_SCROLLWHEEL_RETURNS_BOOL"));
    }
}
