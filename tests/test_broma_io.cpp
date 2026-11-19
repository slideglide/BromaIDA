#include <doctest/doctest.h>

#include <algorithm>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/platform/platform.hpp>

#include "common.hpp"

using namespace bromaida;

static constexpr std::string_view kSample = R"(// header comment
[[link(android)]]
class Foo : Bar {
    // Foo();
    static Foo* create() = win 0x10, m1 inline;
    virtual bool init(int a, float b) = win 0x20;

    int m_value;
}

class Bar {
}
)";

static broma::Root parsed(std::string_view source) {
    auto result = parse_broma(source);
    REQUIRE(result.isOk());
    return std::move(result).unwrap();
}

static broma::Class const& class_named(broma::Root const& root, std::string_view name) {
    auto it = std::ranges::find(root.classes, name, &broma::Class::name);
    REQUIRE(it != root.classes.end());
    return *it;
}

TEST_CASE("find_class locates the class including its attributes and exact bounds") {
    auto span = find_class(kSample, "Foo");
    REQUIRE(span.has_value());
    CHECK(kSample.substr(span->begin).starts_with("[[link(android)]]\nclass Foo : Bar {"));
    CHECK(kSample.substr(span->header_end - 1, 1) == "{");
    CHECK(kSample.substr(span->body_end, 1) == "}");
    CHECK(span->end == span->body_end + 1);
    CHECK(kSample.substr(span->end).starts_with("\n\nclass Bar {"));

    auto bar = find_class(kSample, "Bar");
    REQUIRE(bar.has_value());
    CHECK(kSample.substr(bar->begin) == "class Bar {\n}\n");

    CHECK_FALSE(find_class(kSample, "Missing").has_value());
    CHECK_FALSE(find_class(kSample, "Fo").has_value());
    CHECK_FALSE(find_class("", "Foo").has_value());
}

TEST_CASE("find_class takes the doc comment and every attribute above the class") {
    constexpr std::string_view text =
        "class Other {\n}\n\n/// docs\n[[since(\"2.0\")]]\n[[link(win)]]\nclass Foo {\n}\n";
    auto span = find_class(text, "Foo");
    REQUIRE(span.has_value());
    CHECK(text.substr(span->begin) == "/// docs\n[[since(\"2.0\")]]\n[[link(win)]]\nclass Foo {\n}\n");
}

TEST_CASE("find_class is not fooled by similar names, comments, strings or nesting") {
    constexpr std::string_view text = "// class Foo {\n"
                                      "/* class Foo { */\n"
                                      "class FooBar {\n    void a();\n}\n"
                                      "class Other {\n    // } not the end\n    [[since(\"}\")]]\n    void b();\n}\n"
                                      "class Foo {\n    void c() { if (x) { y; } }\n}\n";

    auto foo = find_class(text, "Foo");
    REQUIRE(foo.has_value());
    CHECK(text.substr(foo->begin) == "class Foo {\n    void c() { if (x) { y; } }\n}\n");

    auto foobar = find_class(text, "FooBar");
    REQUIRE(foobar.has_value());
    CHECK(text.substr(foobar->begin, foobar->end - foobar->begin) == "class FooBar {\n    void a();\n}");

    auto other = find_class(text, "Other");
    REQUIRE(other.has_value());
    CHECK(
        text.substr(other->begin, other->end - other->begin) ==
        "class Other {\n    // } not the end\n    [[since(\"}\")]]\n    void b();\n}");
    auto statements = split_statements(text, *other);
    REQUIRE(statements.size() == 1);
    CHECK(statements[0].text == "[[since(\"}\")]]\n    void b();");
}

TEST_CASE("find_class returns nothing for an unterminated class") {
    CHECK_FALSE(find_class("class Foo {\n    void a();\n", "Foo").has_value());
    CHECK_FALSE(find_class("class Foo\n", "Foo").has_value());
}

TEST_CASE("split_statements returns each declaration with its exact text and span") {
    auto span = find_class(kSample, "Foo");
    REQUIRE(span.has_value());
    auto statements = split_statements(kSample, *span);
    REQUIRE(statements.size() == 3);
    CHECK(statements[0].text == "static Foo* create() = win 0x10, m1 inline;");
    CHECK(statements[1].text == "virtual bool init(int a, float b) = win 0x20;");
    CHECK(statements[2].text == "int m_value;");
    for (auto const& s : statements)
        CHECK(kSample.substr(s.begin, s.end - s.begin) == s.text);
}

TEST_CASE("split_statements keeps inline bodies whole and counts them as one statement") {
    constexpr std::string_view text =
        "class Foo {\n    void a() { {} }\n    int b;\n    int c() const { return 1; }\n}\n";
    auto span = find_class(text, "Foo");
    REQUIRE(span.has_value());
    auto statements = split_statements(text, *span);
    REQUIRE(statements.size() == 3);
    CHECK(statements[0].text == "void a() { {} }");
    CHECK(statements[1].text == "int b;");
    CHECK(statements[2].text == "int c() const { return 1; }");
}

TEST_CASE("split_statements skips comments between declarations") {
    constexpr std::string_view text = "class Foo {\n    // one;\n    int a; /* two; */\n    /// docs;\n    int b;\n}\n";
    auto span = find_class(text, "Foo");
    REQUIRE(span.has_value());
    auto statements = split_statements(text, *span);
    REQUIRE(statements.size() == 2);
    CHECK(statements[0].text == "int a;");
    CHECK(statements[1].text == "int b;");
}

TEST_CASE("patch_class changes only the asked statement and appends with the file's indent") {
    ClassPatch patch;
    patch.replacements.push_back({1, "virtual bool init(int a, float b) = win 0x20, android64 0x30;"});
    patch.additions.push_back("TodoReturn extra();");

    auto patched = patch_class(kSample, "Foo", patch);
    REQUIRE(patched.has_value());
    CHECK(
        *patched == "// header comment\n[[link(android)]]\nclass Foo : Bar {\n    // Foo();\n"
                    "    static Foo* create() = win 0x10, m1 inline;\n"
                    "    virtual bool init(int a, float b) = win 0x20, android64 0x30;\n\n"
                    "    int m_value;\n    TodoReturn extra();\n}\n\nclass Bar {\n}\n");

    auto reparsed = parsed(*patched);
    CHECK(reparsed.classes.size() == 2);
    CHECK(class_named(reparsed, "Foo").fields.size() == 4);
}

TEST_CASE("patch_class keeps doc comments, replaces the header, and re-indents multi-line text") {
    constexpr std::string_view text = "class Foo {\n    /// docs\n    void run();\n}\n";
    ClassPatch patch;
    patch.replacements.push_back({0, "[[since(\"2.0\")]]\nvoid run() = win 0x1;"});
    patch.new_header = "[[link(android)]]\nclass Foo {";
    auto patched = patch_class(text, "Foo", patch);
    REQUIRE(patched.has_value());
    CHECK(
        *patched ==
        "[[link(android)]]\nclass Foo {\n    /// docs\n    [[since(\"2.0\")]]\n    void run() = win 0x1;\n}\n");
}

TEST_CASE("patch_class adds to an empty class and reports a missing class") {
    ClassPatch patch;
    patch.additions = {"void a();", "void b();"};
    auto patched = patch_class("class Foo {\n}\n", "Foo", patch);
    REQUIRE(patched.has_value());
    CHECK(*patched == "class Foo {\n    void a();\n    void b();\n}\n");

    CHECK_FALSE(patch_class("class Foo {\n}\n", "Bar", patch).has_value());
}

TEST_CASE("patch_class ignores a replacement that points past the last statement") {
    ClassPatch patch;
    patch.replacements.push_back({7, "void never();"});
    auto patched = patch_class("class Foo {\n    int a;\n}\n", "Foo", patch);
    REQUIRE(patched.has_value());
    CHECK(*patched == "class Foo {\n    int a;\n}\n");
}

TEST_CASE("write_class output parses back to the same members") {
    auto root = parsed(kSample);
    auto const& foo = class_named(root, "Foo");
    auto written = write_class(foo);
    CHECK(
        written == "[[link(android)]]\nclass Foo : Bar {\n    static Foo* create() = win 0x10, m1 inline;\n"
                   "    virtual bool init(int a, float b) = win 0x20;\n    int m_value;\n}\n");

    auto again = parsed(written + "\nclass Bar {\n}\n");
    REQUIRE(again.classes.size() == 2);
    auto const& foo2 = class_named(again, "Foo");
    CHECK(foo2.superclasses.size() == 1);
    CHECK(foo2.superclasses[0].name == "Bar");
    CHECK(foo2.fields.size() == 3);
    CHECK(write_class(foo2) == written);
}

TEST_CASE("write_binds lists platforms in a fixed order with inline and skips unbound ones") {
    broma::PlatformNumber binds;
    offset_for(binds, Platform::Android64) = 0x30;
    offset_for(binds, Platform::Windows) = 0x10;
    offset_for(binds, Platform::MacArm) = -2;
    CHECK(write_binds(binds) == "win 0x10, m1 inline, android64 0x30");
    CHECK(write_binds(broma::PlatformNumber{}).empty());
}

TEST_CASE("write_pad lists only platforms with a positive amount") {
    broma::PadField pad;
    offset_for(pad.amount, Platform::Windows) = 8;
    offset_for(pad.amount, Platform::Android64) = 0x10;
    CHECK(write_pad(pad, "    ") == "    PAD = win 0x8, android64 0x10;");
}

TEST_CASE("write_attributes writes docs, link, missing, depends, since and renamed_from, minus inherited ones") {
    broma::Attributes attrs;
    attrs.docs = "first\nsecond";
    attrs.links = broma::Platform::Android;
    attrs.missing = broma::Platform::Windows;
    attrs.depends = {"Foo", "Bar"};
    attrs.since = "2.2";
    attrs.renamed_from = {"old", "older"};
    CHECK(
        write_attributes(attrs, "  ") ==
        "  /// first\n  /// second\n  [[link(android)]]\n  [[missing(win)]]\n  [[depends(Foo, Bar)]]\n"
        "  [[since(\"2.2\")]]\n  [[renamed_from(old, older)]]\n");

    broma::Attributes inherited;
    inherited.links = broma::Platform::Android;
    inherited.missing = broma::Platform::Windows;
    inherited.depends = {"Foo"};
    CHECK(
        write_attributes(attrs, "", &inherited) ==
        "/// first\n/// second\n[[depends(Bar)]]\n[[since(\"2.2\")]]\n[[renamed_from(old, older)]]\n");

    CHECK(write_attributes(broma::Attributes{}, "    ").empty());
}

TEST_CASE("parse_broma reports malformed input with a message and no tree") {
    for (std::string_view text :
         {"class {", "class Foo {\n    void a(;\n}\n", "class Foo {\n    void a()\n}\n", "class Foo {\n    int ;\n}\n",
          "class Foo { void a() = win 0xZZ; }", "class Foo {\n    PAD = ;\n}\n"}) {
        CAPTURE(text);
        auto result = parse_broma(text);
        REQUIRE(result.isErr());
        CHECK_FALSE(result.unwrapErr().empty());
    }
}

TEST_CASE("load_broma reports a missing file and a malformed file") {
    auto dir = fresh_dir("io-load");
    auto missing = load_broma(dir / "absent.bro");
    REQUIRE(missing.isErr());
    CHECK_FALSE(missing.unwrapErr().empty());

    write_file(dir / "bad.bro", "class Foo {\n    void a(;\n}\n");
    auto bad = load_broma(dir / "bad.bro");
    REQUIRE(bad.isErr());
    CHECK_FALSE(bad.unwrapErr().empty());

    write_file(dir / "good.bro", "class Foo {\n    void a();\n}\n");
    auto good = load_broma(dir / "good.bro");
    REQUIRE(good.isOk());
    CHECK(good.unwrap().classes.size() == 1);
}

TEST_CASE("load_broma follows an include into a subfolder") {
    auto dir = fresh_dir("io-includes");
    write_file(dir / "sub" / "Inner.bro", "class Inner {\n    void i();\n}\n");
    write_file(dir / "Entry.bro", "#include <sub/Inner.bro>\nclass Outer {\n    void o();\n}\n");
    auto loaded = load_broma(dir / "Entry.bro");
    REQUIRE(loaded.isOk());
    std::vector<std::string> names;
    for (auto const& cls : loaded.unwrap().classes)
        names.push_back(cls.name);
    std::ranges::sort(names);
    CHECK(names == Strings{"Inner", "Outer"});
}

TEST_CASE("a folder with an Entry.bro is loaded through that file alone") {
    auto dir = fresh_dir("folder-entry");
    write_file(dir / "Entry.bro", "#include <A.bro>\n#include <B.bro>\n");
    write_file(dir / "A.bro", "class A {\n    void a();\n}\n");
    write_file(dir / "B.bro", "class B {\n    void b();\n}\n");

    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isOk());
    auto const& folder = loaded.unwrap();
    CHECK(folder.used_entry);
    REQUIRE(folder.files.size() == 1);
    CHECK(folder.files[0].filename().value_or(PathView{}).string() == "Entry.bro");
    REQUIRE(folder.root.classes.size() == 2);
    CHECK(folder.root.classes[0].name == "A");
    CHECK(folder.root.classes[1].name == "B");
    CHECK(folder.duplicates.empty());
}

TEST_CASE("an entry file is found whatever its case") {
    auto dir = fresh_dir("folder-entry-case");
    write_file(dir / "ENTRY.BRO", "class A {\n    void a();\n}\n");
    write_file(dir / "Other.bro", "class Never {\n}\n");
    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isOk());
    CHECK(loaded.unwrap().used_entry);
    REQUIRE(loaded.unwrap().root.classes.size() == 1);
    CHECK(loaded.unwrap().root.classes[0].name == "A");
}

TEST_CASE("without an Entry.bro every .bro file is merged in name order, each class once") {
    auto dir = fresh_dir("folder-merge");
    write_file(dir / "Base.bro", "class Base {\n    void base();\n}\n");
    write_file(dir / "Game.bro", "#include <Base.bro>\nclass Game : Base {\n    void run();\n}\n");
    write_file(dir / "notes.txt", "not broma");
    write_file(dir / "sub" / "Deep.bro", "class Deep {\n}\n");

    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isOk());
    auto const& folder = loaded.unwrap();
    CHECK_FALSE(folder.used_entry);
    CHECK(folder.files.size() == 2);
    REQUIRE(folder.root.classes.size() == 2);
    CHECK(folder.root.classes[0].name == "Base");
    CHECK(folder.root.classes[1].name == "Game");
    CHECK(folder.duplicates.empty());
}

TEST_CASE("the same class in two different files is reported and the first one wins") {
    auto dir = fresh_dir("folder-duplicates");
    write_file(dir / "A.bro", "class Twin {\n    void first();\n}\n");
    write_file(dir / "B.bro", "class Twin {\n    void second();\n}\n");

    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isOk());
    auto const& folder = loaded.unwrap();
    REQUIRE(folder.root.classes.size() == 1);
    REQUIRE(folder.root.classes[0].fields.size() == 1);
    auto const* fn = folder.root.classes[0].fields[0].get_as<broma::FunctionBindField>();
    REQUIRE(fn != nullptr);
    CHECK(fn->prototype.name == "first");
    REQUIRE(folder.duplicates.size() == 1);
    CHECK(folder.duplicates.front().contains("Twin"));
    CHECK(folder.duplicates.front().contains("A.bro"));
    CHECK(folder.duplicates.front().contains("B.bro"));
}

TEST_CASE("a folder with a malformed file fails as a whole") {
    auto dir = fresh_dir("folder-bad");
    write_file(dir / "A.bro", "class A {\n    void a();\n}\n");
    write_file(dir / "B.bro", "class B {\n    void b(;\n}\n");
    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isErr());
    CHECK_FALSE(loaded.unwrapErr().empty());
}

TEST_CASE("an empty folder or a path that isn't a folder is an error") {
    auto empty = fresh_dir("folder-empty");
    auto none = load_broma_folder(empty);
    REQUIRE(none.isErr());
    CHECK(none.unwrapErr().contains("no .bro files"));

    write_file(empty / "Only.bro", "class X {}\n");
    auto file_as_folder = load_broma_folder(empty / "Only.bro");
    REQUIRE(file_as_folder.isErr());
    CHECK(file_as_folder.unwrapErr().contains("not a folder"));
    REQUIRE(load_broma_folder(empty / "missing").isErr());
}
