#include <doctest/doctest.h>

#include <bromaida/broma_io.hpp>

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

TEST_CASE("find_class and split_statements") {
    auto span = find_class(kSample, "Foo");
    REQUIRE(span);
    CHECK(kSample.substr(span->begin).starts_with("[[link(android)]]"));

    auto statements = split_statements(kSample, *span);
    REQUIRE(statements.size() == 3);  // the commented-out ctor is not one
    CHECK(statements[0].text == "static Foo* create() = win 0x10, m1 inline;");
    CHECK(statements[2].text == "int m_value;");
    CHECK_FALSE(find_class(kSample, "Missing"));
    CHECK(find_class(kSample, "Bar"));
}

TEST_CASE("patch_class keeps everything it was not asked to touch") {
    ClassPatch patch;
    patch.replacements.push_back({1, "virtual bool init(int a, float b) = win 0x20, android64 0x30;"});
    patch.additions.push_back("TodoReturn extra();");

    auto patched = patch_class(kSample, "Foo", patch);
    REQUIRE(patched);
    CHECK(patched->contains("// Foo();"));
    CHECK(patched->contains("android64 0x30"));
    CHECK(patched->contains("    TodoReturn extra();"));
    CHECK(patched->contains("// header comment"));

    // the result must still parse
    auto reparsed = parse_broma(*patched);
    REQUIRE(reparsed.isOk());
    CHECK(reparsed.unwrap().classes.size() == 2);
}

TEST_CASE("writer produces text the parser accepts") {
    auto parsed = parse_broma(kSample);
    REQUIRE(parsed.isOk());
    auto root = std::move(parsed).unwrap();
    auto foo = std::ranges::find(root.classes, "Foo", &broma::Class::name);
    REQUIRE(foo != root.classes.end());
    auto written = write_class(*foo);
    CHECK(written.contains("[[link(android)]]"));
    CHECK(written.contains("static Foo* create() = win 0x10, m1 inline;"));

    auto again = parse_broma(written + "\nclass Bar {\n}\n");
    REQUIRE(again.isOk());
    CHECK(again.unwrap().classes.size() == 2);
    CHECK_FALSE(written.contains("depends"));              // implied by the superclass
    CHECK(written.find("link") == written.rfind("link"));  // only on the class, not repeated on members
}

// ---- loading a folder -------------------------------------------------------------------------------------

static Path fresh_dir(std::string_view name) {
    auto dir = Path::tmp().unwrap() / "bromaida-tests" / name;
    (void)asp::fs::removeDirAll(dir, false);
    REQUIRE(asp::fs::createDirAll(dir).isOk());
    return dir;
}

static void write_file(Path const& dir, std::string_view name, std::string_view text) {
    REQUIRE(asp::fs::write(dir / name, text).isOk());
}

TEST_CASE("a folder with an Entry.bro is loaded through that file alone") {
    auto dir = fresh_dir("folder-entry");
    write_file(dir, "Entry.bro", "#include <A.bro>\n#include <B.bro>\n");
    write_file(dir, "A.bro", "class A {\n    void a();\n}\n");
    write_file(dir, "B.bro", "class B {\n    void b();\n}\n");

    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isOk());
    auto const& folder = loaded.unwrap();
    CHECK(folder.used_entry);
    CHECK(folder.files.size() == 1);
    CHECK(folder.root.classes.size() == 2);  // each class once, not once for the entry and once for the file
    CHECK(folder.duplicates.empty());
}

TEST_CASE("without an Entry.bro every .bro file is merged, each class once") {
    auto dir = fresh_dir("folder-merge");
    write_file(dir, "Base.bro", "class Base {\n    void base();\n}\n");
    // includes Base.bro, so Base is parsed twice: once on its own and once through this file
    write_file(dir, "Game.bro", "#include <Base.bro>\nclass Game : Base {\n    void run();\n}\n");
    write_file(dir, "notes.txt", "not broma");

    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isOk());
    auto const& folder = loaded.unwrap();
    CHECK_FALSE(folder.used_entry);
    CHECK(folder.files.size() == 2);
    CHECK(folder.root.classes.size() == 2);
    CHECK(folder.duplicates.empty());
}

TEST_CASE("the same class in two different files is reported and the first one wins") {
    auto dir = fresh_dir("folder-duplicates");
    write_file(dir, "A.bro", "class Twin {\n    void first();\n}\n");
    write_file(dir, "B.bro", "class Twin {\n    void second();\n}\n");

    auto loaded = load_broma_folder(dir);
    REQUIRE(loaded.isOk());
    auto const& folder = loaded.unwrap();
    REQUIRE(folder.root.classes.size() == 1);
    CHECK(folder.duplicates.size() == 1);
    CHECK(folder.duplicates.front().contains("Twin"));
}

TEST_CASE("an empty folder or a path that isn't a folder is an error") {
    auto empty = fresh_dir("folder-empty");
    auto none = load_broma_folder(empty);
    REQUIRE(none.isErr());
    CHECK(none.unwrapErr().contains("no .bro files"));

    write_file(empty, "Only.bro", "class X {}\n");
    CHECK(load_broma_folder(empty / "Only.bro").isErr());
    CHECK(load_broma_folder(empty / "missing").isErr());
}
