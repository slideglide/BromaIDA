#include <doctest/doctest.h>

#include <algorithm>
#include <set>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/typegen/class_graph.hpp>
#include <bromaida/typegen/codegen.hpp>

#include "common.hpp"

using namespace bromaida;

static std::vector<broma::Class const*> all_classes(broma::Root const& root) {
    std::vector<broma::Class const*> classes;
    for (auto const& c : root.classes)
        classes.push_back(&c);
    return classes;
}

static broma::Class const& class_named(broma::Root const& root, std::string_view name) {
    auto it = std::ranges::find(root.classes, name, &broma::Class::name);
    REQUIRE(it != root.classes.end());
    return *it;
}

static broma::Root parsed(std::string_view source) {
    auto result = parse_broma(source);
    REQUIRE(result.isOk());
    return std::move(result).unwrap();
}

static CodegenOutput generate(
    broma::Root const& root, Platform platform, std::vector<BundleEntry> headers = {}, Path dir = {},
    std::vector<std::string> unavailable = {}, Path broma_dir = {}) {
    ClassGraph graph{all_classes(root), platform};
    CodegenInput input;
    input.platform = platform;
    input.classes = all_classes(root);
    input.unavailable_classes = std::move(unavailable);
    input.bundle.dir = std::move(dir);
    input.bundle.manifest.headers = std::move(headers);
    input.broma_dir = std::move(broma_dir);
    input.generator_version = "vtest";
    return generate_types_header(input, graph);
}

static bool balanced(std::string_view text, char open, char close) {
    long depth = 0;
    for (char c : text) {
        if (c == open) ++depth;
        if (c == close && --depth < 0) return false;
    }
    return depth == 0;
}

static constexpr std::string_view kBroma = R"(
class Child : Base {
    virtual void tick(float dt) = win 0x1;
    Holder m_holder;
    std::vector<Base*> m_list;
    PAD = win 0x8;
}

class Base {
    virtual void tick(float dt) = win 0x2;
    virtual void tick(int dt) = win 0x3;
}

class Holder {
    int m_x;
}
)";

TEST_CASE("class graph puts bases and by-value members before the class that needs them") {
    auto root = parsed(kBroma);
    ClassGraph graph{all_classes(root), Platform::Windows};
    CHECK(graph.class_order() == Strings{"Base", "Holder", "Child"});
}

TEST_CASE("class graph survives a by-value cycle and still lists every class once") {
    auto root = parsed("class A {\n    B m_b;\n}\nclass B {\n    A m_a;\n}\nclass C : A {\n}\n");
    ClassGraph graph{all_classes(root), Platform::Windows};
    auto const& order = graph.class_order();
    REQUIRE(order.size() == 3);
    CHECK(std::set<std::string>(order.begin(), order.end()) == std::set<std::string>{"A", "B", "C"});
    CHECK((std::ranges::find(order, "A") - order.begin()) < (std::ranges::find(order, "C") - order.begin()));
}

TEST_CASE("class graph asks for a forward declaration of pointer, reference and function uses") {
    auto root =
        parsed("class A {\n    B* m_b;\n    void f(C& c);\n    D m_d;\n}\nclass B {\n}\nclass C {\n}\nclass D {\n}\n");
    ClassGraph graph{all_classes(root), Platform::Windows};
    CHECK(graph.class_order() == Strings{"D", "A", "B", "C"});
    CHECK(graph.forward_declarations() == Strings{"B", "C"});
}

TEST_CASE("class graph tells overrides from broken overrides") {
    auto root = parsed(R"(
class Base {
    virtual void tick(float dt) = win 0x1;
    virtual void tick(int dt) = win 0x2;
    virtual void other() = win 0x3;
}
class Child : Base {
    virtual void tick(float dt) = win 0x4;
    virtual void tick(double dt) = win 0x5;
    virtual void fresh() = win 0x6;
}
)");
    ClassGraph graph{all_classes(root), Platform::Windows};

    auto const& own = graph.own_virtuals("Child");
    REQUIRE(own.size() == 3);
    CHECK(own[0].overload_key() == "tick(float)");
    CHECK(graph.is_override("Child", own[0]));
    CHECK_FALSE(graph.is_override("Child", own[1]));
    CHECK_FALSE(graph.is_override("Child", own[2]));

    auto const broken = graph.broken_overrides("Child");
    REQUIRE(broken.size() == 1);
    CHECK(broken[0].own.overload_key() == "tick(double)");
    REQUIRE(broken[0].conflicting.size() == 2);
    CHECK(broken[0].conflicting[0].overload_key() == "tick(float)");
    CHECK(broken[0].conflicting[1].overload_key() == "tick(int)");

    CHECK(graph.own_virtuals("Missing").empty());
}

TEST_CASE("class graph reports exactly the std types and the referenced types") {
    auto root = parsed(R"(
class Holder {
    std::vector<std::pair<std::string, Foo*>> m_a;
    std::unordered_map<int, Bar> m_b;
    int m_plain;
    unsigned int m_u;
    Baz* m_baz;
}
)");
    ClassGraph graph{all_classes(root), Platform::Windows};
    CHECK(
        graph.used_std_types() ==
        std::set<std::string>{"std::pair", "std::string", "std::unordered_map", "std::vector"});
    CHECK(graph.referenced_types() == std::set<std::string>{"Bar", "Baz", "Foo"});
}

TEST_CASE("std members land in the by-pointer or by-value stub with sequential names") {
    auto root = parsed(
        "class Holder {\n    std::vector<Foo*> m_a;\n    std::map<int, Bar> m_b;\n}\nclass Foo {\n}\nclass Bar {\n}\n");
    ClassGraph graph{all_classes(root), Platform::Windows};
    auto const& stubs = graph.stl_stubs();

    REQUIRE(stubs.by_pointer.members.size() == 1);
    CHECK(stubs.by_pointer.members[0].type == "std::vector<Foo*>");
    CHECK(stubs.by_pointer.members[0].name == "m_0");
    REQUIRE(stubs.by_value.members.size() == 1);
    CHECK(stubs.by_value.members[0].type == "std::map<int, Bar>");
    CHECK(stubs.by_value.members[0].name == "m_1");

    CHECK(
        stubs.by_pointer.emit() ==
        "class " + stubs.by_pointer.class_name + " {\npublic:\n\tstd::vector<Foo*> m_0;\n};\n");
    CHECK(StlStub{"X", {}}.emit() == "");
}

TEST_CASE("build_class writes the exact declaration: virtuals, blank line, members, padding") {
    auto root = parsed(kBroma);
    ClassGraph graph{all_classes(root), Platform::Windows};

    CHECK(
        build_class(class_named(root, "Child"), graph, Platform::Windows) ==
        "class Child : public Base {\npublic:\n\tvirtual void tick(float dt);\n\n\tHolder m_holder;\n"
        "\tstd::vector<Base*> m_list;\n\tPAD(0x8);\n};\n\n");
    CHECK(
        build_class(class_named(root, "Base"), graph, Platform::Windows) ==
        "class Base {\npublic:\n\tvirtual void tick(float dt);\n\tvirtual void tick(int dt);\n};\n\n");
    CHECK(
        build_class(class_named(root, "Holder"), graph, Platform::Windows) ==
        "class Holder {\npublic:\n\tint m_x;\n};\n\n");
}

TEST_CASE("build_class picks the padding of the target platform") {
    auto root = parsed("class Pad {\n    int m_a;\n    PAD = win 0x8, m1 0x10;\n    PAD = android64 0x4;\n}\n");
    ClassGraph win{all_classes(root), Platform::Windows};
    ClassGraph arm{all_classes(root), Platform::MacArm};
    ClassGraph droid{all_classes(root), Platform::Android64};
    ClassGraph ios{all_classes(root), Platform::iOS};

    CHECK(
        build_class(class_named(root, "Pad"), win, Platform::Windows) ==
        "class Pad {\npublic:\n\tint m_a;\n\tPAD(0x8);\n};\n\n");
    CHECK(
        build_class(class_named(root, "Pad"), arm, Platform::MacArm) ==
        "class Pad {\npublic:\n\tint m_a;\n\tPAD(0x10);\n};\n\n");
    CHECK(
        build_class(class_named(root, "Pad"), droid, Platform::Android64) ==
        "class Pad {\npublic:\n\tint m_a;\n\tPAD(0x4);\n};\n\n");
    CHECK(build_class(class_named(root, "Pad"), ios, Platform::iOS) == "class Pad {\npublic:\n\tint m_a;\n};\n\n");
}

TEST_CASE("build_class wraps a namespaced class and keeps its qualified bases") {
    auto root = parsed("class ns::Foo : ns::Base {\n    virtual void a();\n}\nclass ns::Base {\n}\n");
    ClassGraph graph{all_classes(root), Platform::Windows};
    CHECK(
        build_class(class_named(root, "ns::Foo"), graph, Platform::Windows) ==
        "namespace ns {\nclass Foo : public ns::Base {\npublic:\n\tvirtual void a();\n};\n}\n\n");
}

TEST_CASE("build_class leaves out secondary-base overrides on MSVC only, and missing virtuals everywhere") {
    auto root = parsed(R"(
class A {
    virtual void f() = win 0x1;
}
class B {
    virtual void g() = win 0x2;
}
class C : A, B {
    virtual void f() = win 0x3;
    virtual void g() = win 0x4;
    [[missing(win)]] virtual void gone();
}
)");
    ClassGraph win{all_classes(root), Platform::Windows};
    ClassGraph mac{all_classes(root), Platform::MacArm};
    auto const& c = class_named(root, "C");

    CHECK(
        build_class(c, win, Platform::Windows) ==
        "class C : public A, public B {\npublic:\n\tvirtual void f();\n};\n\n");
    CHECK(
        build_class(c, mac, Platform::MacArm) == "class C : public A, public B {\npublic:\n\tvirtual void "
                                                 "f();\n\tvirtual void g();\n\tvirtual void gone();\n};\n\n");
}

TEST_CASE("generated header starts with the platform preamble and defines") {
    auto root = parsed("class Foo {\n    int m_x;\n}\n");
    auto out = generate(root, Platform::Windows);
    CHECK(out.text.starts_with(
        "// Generated by BromaIDA vtest, do not modify\n#define BROMAIDA_PLATFORM_WINDOWS\n// platform shorthands\n"
        "#define BROMAIDA_IS_PLATFORM_MACHO 0\n#define BROMAIDA_IS_PLATFORM_ANDROID 0\n\n"));

    auto mac = generate(root, Platform::MacArm);
    CHECK(mac.text.contains("#define BROMAIDA_PLATFORM_M1_MACOS\n"));
    CHECK(mac.text.contains("#define BROMAIDA_IS_PLATFORM_MACHO 1\n#define BROMAIDA_IS_PLATFORM_ANDROID 0\n"));
    auto droid = generate(root, Platform::Android64);
    CHECK(droid.text.contains("#define BROMAIDA_IS_PLATFORM_MACHO 0\n#define BROMAIDA_IS_PLATFORM_ANDROID 1\n"));

    CHECK(out.broma_class_count == 1);
    CHECK(out.header_class_count == 0);
    CHECK(out.text.contains("class Foo {\npublic:\n\tint m_x;\n};\n"));
    CHECK(out.text.contains("enum class TodoReturn {};"));
}

TEST_CASE("generated header is brace-balanced and declares before it defines") {
    auto root = parsed(R"(
class ns::Derived : ns::Base {
    virtual void tick(float dt) = win 0x1;
    std::vector<Other*> m_others;
    Value m_value;
}
class ns::Base {
    virtual void tick(float dt) = win 0x2;
}
class Value {
    int m_x;
}
class Other {
    ns::Derived* m_back;
}
)");
    auto out = generate(root, Platform::Windows, {}, {}, {"ns::Gone", "Plain"});
    REQUIRE(out.broma_class_count == 4);

    CHECK(balanced(out.text, '{', '}'));
    CHECK(balanced(out.text, '(', ')'));

    CHECK(out.text.contains("namespace ns { class Gone; }\n"));
    CHECK(out.text.contains("class Plain;\n"));
    CHECK(out.text.contains("class Other;\n"));

    auto at = [&](std::string_view needle) {
        auto pos = out.text.find(needle);
        REQUIRE_MESSAGE(pos != std::string::npos, needle);
        return pos;
    };
    CHECK(at("namespace ns { class Gone; }") < at("class Value {"));
    CHECK(at("class Plain;") < at("class Value {"));
    CHECK(at("class Other;") < at("class Derived : public ns::Base {"));
    CHECK(at("class Base {") < at("class Derived : public ns::Base {"));
    CHECK(at("class Value {") < at("class Derived : public ns::Base {"));
    CHECK(at("class __BromaSTLTypesPtr {") < at("class Base {"));
    CHECK(count_of(out.text, "class Other {") == 1);
    CHECK(count_of(out.text, "class Other;") == 1);
}

TEST_CASE("an unavailable class that Broma also defines is declared once, as a class") {
    auto root = parsed("class Foo {\n    int m_x;\n}\n");
    auto out = generate(root, Platform::Windows, {}, {}, {"Foo", "Gone", "Gone"});
    CHECK(count_of(out.text, "class Gone;") == 1);
    CHECK(count_of(out.text, "class Foo;") == 0);
    CHECK(count_of(out.text, "class Foo {") == 1);
}

TEST_CASE("bundle headers are copied in order, with filter, parse and optional modes") {
    auto dir = fresh_dir("codegen-bundle");
    write_file(dir / "first.hpp", "#include \"inc.hpp\"\nstruct First {};\n");
    write_file(dir / "inc.hpp", "struct Included {};\n");
    write_file(dir / "second.hpp", "struct Second {};\n");
    write_file(dir / "sub" / "third.hpp", "#include \"neighbour.hpp\"\nstruct Third {};\n");
    write_file(dir / "sub" / "neighbour.hpp", "struct Neighbour {};\n");

    auto root = parsed("class Foo {\n    int m_x;\n}\n");
    auto out = generate(
        root, Platform::Windows,
        {BundleEntry{.file = "first.hpp", .includes = "filter"}, BundleEntry{.file = "missing.hpp", .optional = true},
         BundleEntry{.file = "second.hpp"}, BundleEntry{.file = "sub/third.hpp", .includes = "parse"}},
        dir);

    CHECK(out.text.contains("// first.hpp\n// #include \"inc.hpp\"\nstruct First {};\n"));
    CHECK(count_of(out.text, "struct Included {};") == 0);
    CHECK(count_of(out.text, "struct Second {};") == 1);
    CHECK(count_of(out.text, "struct Neighbour {};") == 1);
    CHECK(count_of(out.text, "struct Third {};") == 1);
    CHECK(out.text.find("struct First {};") < out.text.find("struct Second {};"));
    CHECK(out.text.find("struct Second {};") < out.text.find("struct Neighbour {};"));
    CHECK(out.text.find("struct Neighbour {};") < out.text.find("struct Third {};"));
    CHECK(out.header_class_count == 4);
}

TEST_CASE("a header that cannot be read is skipped without touching the rest") {
    auto dir = fresh_dir("codegen-missing-header");
    write_file(dir / "ok.hpp", "struct Ok {};\n");
    auto root = parsed("class Foo {\n    int m_x;\n}\n");
    auto out =
        generate(root, Platform::Windows, {BundleEntry{.file = "absent.hpp"}, BundleEntry{.file = "ok.hpp"}}, dir);
    CHECK(count_of(out.text, "struct Ok {};") == 1);
    CHECK(out.text.contains("class Foo {"));
}

TEST_CASE("headers that include each other are pasted once") {
    auto dir = fresh_dir("codegen-cycle");
    write_file(dir / "a.hpp", "#include \"b.hpp\"\nstruct A {};\n");
    write_file(dir / "b.hpp", "#include \"a.hpp\"\nstruct B {};\n");

    auto root = parsed("class Foo { int m_x; }");
    auto out = generate(root, Platform::Windows, {BundleEntry{.file = "a.hpp", .includes = "parse"}}, dir);

    CHECK(count_of(out.text, "struct A {};") == 1);
    CHECK(count_of(out.text, "struct B {};") == 1);
    CHECK(out.text.contains("// #include \"b.hpp\"\n"));
}

TEST_CASE("the enums entry keeps top-level enums and drops everything else") {
    auto dir = fresh_dir("codegen-enums");
    // Enums.hpp sits in bindings/include, next to the version folders
    write_file(
        dir / "bindings" / "include" / "Geode" / "Enums.hpp",
        "#pragma once\n#include <cstdint>\nstruct NotAnEnum {};\nenum class A { X, Y };\n#ifdef HIDDEN\nenum class "
        "Hidden {\n    Z\n};\n#endif\n"
        "enum class B : int {\n    P = 1,\n    Q = 2,\n};\nclass AlsoNot {};\n");

    auto root = parsed("class Foo {\n    int m_x;\n}\n");
    auto out = generate(
        root, Platform::Windows, {BundleEntry{.enums = "../include/Geode/Enums.hpp"}}, dir, {},
        dir / "bindings" / "2.2");

    CHECK(out.enum_count == 2);
    CHECK(out.text.contains("// Geode/Enums.hpp\n"));
    CHECK(out.text.contains("enum class A { X, Y };\n"));
    CHECK(out.text.contains("enum class B : int {\n    P = 1,\n    Q = 2,\n};\n"));
    CHECK(count_of(out.text, "Hidden") == 0);
    CHECK(count_of(out.text, "NotAnEnum") == 0);
    CHECK(count_of(out.text, "AlsoNot") == 0);
}

TEST_CASE("a missing enums file produces a header with no enums") {
    auto dir = fresh_dir("codegen-no-enums");
    auto root = parsed("class Foo {\n    int m_x;\n}\n");
    auto out = generate(
        root, Platform::Windows, {BundleEntry{.enums = "../include/Geode/Enums.hpp"}}, dir, {}, dir / "bindings");
    CHECK(out.enum_count == 0);
    CHECK(out.text.contains("class Foo {"));
}
