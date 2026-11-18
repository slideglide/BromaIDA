#include <doctest/doctest.h>

#include <bromaida/broma_io.hpp>
#include <bromaida/exporter.hpp>

using namespace bromaida;

static ExtractedMethod method(
    std::string cls, std::string name, std::vector<std::string> params, std::uint64_t offset, bool is_virtual = false) {
    FunctionSignature sig;
    sig.class_name = std::move(cls);
    sig.name = std::move(name);
    sig.ret = TypeRef{"TodoReturn"};
    sig.is_virtual = is_virtual;
    for (auto& p : params)
        sig.params.push_back(Param{TypeRef{p}, {}, {}});
    return {std::move(sig), Bind{BindState::Bound, offset}};
}

static Path temp_dir() {
    auto dir = Path::tmp().unwrap() / "bromaida-tests";
    REQUIRE(asp::fs::createDirAll(dir).isOk());
    return dir;
}

TEST_CASE("exporter fills addresses into a template without losing comments") {
    auto file = temp_dir() / "template.bro";
    REQUIRE(
        asp::fs::write(
            file,
            std::string_view{
                "// keep me\nclass Foo {\n    // Foo();\n    bool init(int a) = win 0x10;\n    void other();\n}\n"})
            .isOk());
    auto root = load_broma(file);
    REQUIRE(root.isOk());
    auto tmpl = std::move(root).unwrap();

    ExtractedBinary binary;
    binary.platform = Platform::MacArm;
    ExtractedClass cls;
    cls.name = "Foo";
    cls.exported = false;
    cls.methods = {
        method("Foo", "init", {"int"}, 0x40), method("Foo", "other", {}, 0x50),
        method("Foo", "brandNew", {"float"}, 0x60)};
    binary.classes = {cls};

    ExportOptions options;
    options.platform = Platform::MacArm;
    options.default_file = file;
    ExportPrompter prompter;

    auto plan = plan_export(binary, &tmpl, options, prompter);
    REQUIRE(plan.isOk());
    auto const& result = plan.unwrap();
    CHECK(result.stats.bindings_added == 2);
    CHECK(result.stats.methods_created == 1);

    auto const& text = result.files.at(file);
    CHECK(text.contains("// keep me"));
    CHECK(text.contains("// Foo();"));
    CHECK(text.contains("bool init(int a) = win 0x10, m1 0x40;"));
    CHECK(text.contains("TodoReturn brandNew(float p0) = m1 0x60;"));
    CHECK(parse_broma(text).isOk());
}

TEST_CASE("address mismatches are kept unless the prompter agrees") {
    auto file = temp_dir() / "mismatch.bro";
    REQUIRE(asp::fs::write(file, std::string_view{"class Foo {\n    void run() = win 0x10;\n}\n"}).isOk());
    auto tmpl = load_broma(file).unwrap();

    ExtractedBinary binary;
    binary.platform = Platform::Windows;
    ExtractedClass cls;
    cls.name = "Foo";
    cls.methods = {method("Foo", "run", {}, 0x99)};
    binary.classes = {cls};

    ExportOptions options;
    options.platform = Platform::Windows;
    ExportPrompter keep;
    auto kept = plan_export(binary, &tmpl, options, keep).unwrap();
    CHECK(kept.stats.mismatches_kept == 1);
    CHECK(kept.files.empty());

    struct Overwrite final : ExportPrompter {
        bool overwrite_address(std::string_view, std::string_view, std::string_view) override { return true; }
    } overwrite;
    auto changed = plan_export(binary, &tmpl, options, overwrite).unwrap();
    CHECK(changed.stats.bindings_updated == 1);
    CHECK(changed.files.at(file).contains("win 0x99"));
}

TEST_CASE("android classes are linked by symbol and new classes follow vtable order") {
    ExtractedBinary binary;
    binary.platform = Platform::Android64;
    ExtractedClass cls;
    cls.name = "ns::Thing";
    cls.bases = {"Base"};
    cls.methods = {
        method("ns::Thing", "b", {}, 0x1, true), method("ns::Thing", "a", {}, 0x2, true),
        method("ns::Thing", "z", {}, 0x3)};
    cls.vtable = {"b()", "a()"};
    binary.classes = {cls};

    ExportOptions options;
    options.platform = Platform::Android64;
    options.default_file = temp_dir() / "fresh.bro";
    (void)asp::fs::removeFile(options.default_file);
    ExportPrompter prompter;

    auto plan = plan_export(binary, nullptr, options, prompter);
    REQUIRE(plan.isOk());
    auto const& text = plan.unwrap().files.at(options.default_file);
    CHECK(text.contains("[[link(android)]]"));
    CHECK(text.contains("class ns::Thing : Base {"));
    CHECK(text.find("b()") < text.find("a()"));
    CHECK_FALSE(text.contains("0x1"));
    CHECK(parse_broma(text).isOk());
}

TEST_CASE("a from-scratch export counts what it wrote and honours the argument name option") {
    auto named = method("Foo", "set", {"int", "float"}, 0x40);
    named.sig.params[0].name = "count";
    named.sig.params[1].name = "scale";
    named.sig.ret = TypeRef{"bool"};

    ExtractedClass cls;
    cls.name = "Foo";
    cls.exported = false;
    cls.methods = {named, method("Foo", "other", {}, 0x50)};

    ExtractedClass hidden;
    hidden.name = "(anonymous namespace)::Hidden";
    hidden.methods = {method("(anonymous namespace)::Hidden", "run", {}, 0x60)};

    ExtractedBinary binary;
    binary.platform = Platform::Windows;
    binary.classes = {cls, hidden};

    auto run = [&](bool names, bool returns) {
        ExportOptions options;
        options.platform = Platform::Windows;
        options.include_param_names = names;
        options.include_return_types = returns;
        options.default_file = temp_dir() / "counted.bro";
        ExportPrompter prompter;
        auto plan = plan_export(binary, nullptr, options, prompter);
        REQUIRE(plan.isOk());
        return std::move(plan).unwrap();
    };

    auto off = run(false, false);
    CHECK(off.stats.classes_created == 1);
    CHECK(off.stats.classes_skipped == 1);
    CHECK(off.stats.methods_created == 2);
    CHECK(off.stats.bindings_created == 2);
    CHECK(off.stats.return_types == 0);
    CHECK(off.stats.param_names == 0);
    auto const& off_text = off.files.begin()->second;
    CHECK(off_text.contains("TodoReturn set(int p0, float p1) = win 0x40;"));
    CHECK_FALSE(off_text.contains("Hidden"));
    CHECK(parse_broma(off_text).isOk());

    auto on = run(true, true);
    CHECK(on.stats.return_types == 1);
    CHECK(on.stats.param_names == 2);
    CHECK(on.files.begin()->second.contains("bool set(int count, float scale) = win 0x40;"));
}

TEST_CASE("ignore rules") {
    IgnoreRules rules;
    rules.namespaces = {"cocos2d"};
    rules.classes = {"Special"};
    CHECK(rules.matches("cocos2d::CCNode"));
    CHECK(rules.matches("Special"));
    CHECK_FALSE(rules.matches("cocos2dx::Other"));
}
