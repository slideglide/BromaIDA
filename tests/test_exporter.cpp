#include <doctest/doctest.h>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/exporter/exporter.hpp>

#include "common.hpp"

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

static broma::Root load_template(Path const& file, std::string_view text) {
    write_file(file, text);
    auto loaded = load_broma(file);
    REQUIRE(loaded.isOk());
    return std::move(loaded).unwrap();
}

static ExtractedBinary binary_of(Platform platform, ExtractedClass cls) {
    ExtractedBinary binary;
    binary.platform = platform;
    binary.classes = {std::move(cls)};
    return binary;
}

static ExtractedClass class_of(std::string name, std::vector<ExtractedMethod> methods, bool exported = false) {
    ExtractedClass cls;
    cls.name = std::move(name);
    cls.methods = std::move(methods);
    cls.exported = exported;
    return cls;
}

static ExportPlan plan_for(
    ExtractedBinary const& binary, broma::Root const* tmpl, ExportOptions options, ExportPrompter& prompter) {
    options.platform = binary.platform;
    auto plan = plan_export(binary, tmpl, options, prompter);
    REQUIRE(plan.isOk());
    return std::move(plan).unwrap();
}

TEST_CASE("exporter fills addresses into a template and changes nothing else") {
    auto file = fresh_dir("export-fill") / "template.bro";
    auto tmpl = load_template(
        file, "// keep me\nclass Foo {\n    // Foo();\n    bool init(int a) = win 0x10;\n    void other();\n}\n");
    auto binary = binary_of(
        Platform::MacArm, class_of(
                              "Foo", {method("Foo", "init", {"int"}, 0x40), method("Foo", "other", {}, 0x50),
                                      method("Foo", "brandNew", {"float"}, 0x60)}));

    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);

    CHECK(plan.stats.bindings_added == 2);
    CHECK(plan.stats.bindings_updated == 0);
    CHECK(plan.stats.bindings_created == 1);
    CHECK(plan.stats.methods_created == 1);
    CHECK(plan.stats.classes_created == 0);
    REQUIRE(plan.files.size() == 1);
    CHECK(
        plan.files.at(file) == "// keep me\nclass Foo {\n    // Foo();\n    bool init(int a) = win 0x10, m1 0x40;\n"
                               "    void other() = m1 0x50;\n    TodoReturn brandNew(float p0) = m1 0x60;\n}\n");
    CHECK(parse_broma(plan.files.at(file)).isOk());
}

TEST_CASE("an overloaded method is matched by its parameter types") {
    auto file = fresh_dir("export-overload") / "t.bro";
    auto tmpl = load_template(file, "class Foo {\n    void set(int a);\n    void set(float a);\n}\n");
    auto binary = binary_of(
        Platform::Windows,
        class_of("Foo", {method("Foo", "set", {"float"}, 0x20), method("Foo", "set", {"int"}, 0x10)}));

    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(
        plan.files.at(file) == "class Foo {\n    void set(int a) = win 0x10;\n    void set(float a) = win 0x20;\n}\n");
    CHECK(plan.stats.bindings_added == 2);
    CHECK(plan.stats.methods_created == 0);
}

TEST_CASE("const and non-const overloads stay distinct") {
    auto file = fresh_dir("export-const") / "t.bro";
    auto tmpl = load_template(file, "class Foo {\n    int get();\n    int get() const;\n}\n");
    auto plain = method("Foo", "get", {}, 0x10);
    auto constant = method("Foo", "get", {}, 0x20);
    constant.sig.is_const = true;
    auto binary = binary_of(Platform::Windows, class_of("Foo", {plain, constant}));

    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(plan.files.at(file) == "class Foo {\n    int get() = win 0x10;\n    int get() const = win 0x20;\n}\n");
}

TEST_CASE("an address mismatch is kept unless the prompter agrees, and the prompter sees the exact values") {
    auto file = fresh_dir("export-mismatch") / "mismatch.bro";
    auto tmpl = load_template(file, "class Foo {\n    void run() = win 0x10;\n}\n");
    auto binary = binary_of(Platform::Windows, class_of("Foo", {method("Foo", "run", {}, 0x99)}, true));

    ExportOptions options;
    options.default_file = file;
    options.mark_link = false;

    ExportPrompter keep;
    auto kept = plan_for(binary, &tmpl, options, keep);
    CHECK(kept.stats.mismatches_kept == 1);
    CHECK(kept.stats.bindings_updated == 0);
    CHECK(kept.files.empty());

    Strings seen;
    ExportPrompter refuse{
        .overwrite_address = [&](std::string_view name, std::string_view existing, std::string_view found) {
            seen = {std::string{name}, std::string{existing}, std::string{found}};
            return false;
        }};
    auto refused = plan_for(binary, &tmpl, options, refuse);
    CHECK(seen == Strings{"Foo::run", "0x10", "0x99"});
    CHECK(refused.stats.mismatches_kept == 1);
    CHECK(refused.files.empty());

    ExportPrompter overwrite{.overwrite_address = [](auto, auto, auto) { return true; }};
    auto changed = plan_for(binary, &tmpl, options, overwrite);
    CHECK(changed.stats.bindings_updated == 1);
    CHECK(changed.stats.mismatches_kept == 0);
    CHECK(changed.files.at(file) == "class Foo {\n    void run() = win 0x99;\n}\n");
}

TEST_CASE("an address that matches is not touched and an inline one gets the real address") {
    auto file = fresh_dir("export-same") / "t.bro";
    auto tmpl = load_template(file, "class Foo {\n    void a() = win 0x10;\n    void b() = win inline;\n}\n");
    auto binary =
        binary_of(Platform::Windows, class_of("Foo", {method("Foo", "a", {}, 0x10), method("Foo", "b", {}, 0x77)}));

    ExportOptions options;
    options.default_file = file;
    options.mark_link = false;
    int asked = 0;
    ExportPrompter prompter{.overwrite_address = [&](auto, auto, auto) {
        ++asked;
        return true;
    }};
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(asked == 0);
    CHECK(plan.stats.mismatches_kept == 0);
    CHECK(plan.stats.bindings_updated == 0);
    CHECK(plan.stats.bindings_added == 1);
    CHECK(plan.files.at(file) == "class Foo {\n    void a() = win 0x10;\n    void b() = win 0x77;\n}\n");
}

TEST_CASE("return types and argument names replace the placeholders only when asked") {
    auto file = fresh_dir("export-names") / "t.bro";
    auto tmpl = load_template(file, "class Foo {\n    TodoReturn get();\n    void set(int p0, float scale);\n}\n");
    auto get = method("Foo", "get", {}, 0x10);
    get.sig.ret = TypeRef{"int"};
    auto set = method("Foo", "set", {"int", "float"}, 0x20);
    set.sig.params[0].name = "count";
    set.sig.params[1].name = "other";
    auto binary = binary_of(Platform::Windows, class_of("Foo", {get, set}));

    ExportOptions off;
    off.default_file = file;
    ExportPrompter prompter;
    auto plain = plan_for(binary, &tmpl, off, prompter);
    CHECK(
        plain.files.at(file) ==
        "class Foo {\n    TodoReturn get() = win 0x10;\n    void set(int p0, float scale) = win 0x20;\n}\n");
    CHECK(plain.stats.return_types == 0);
    CHECK(plain.stats.param_names == 0);

    ExportOptions on = off;
    on.include_return_types = true;
    on.include_param_names = true;
    auto full = plan_for(binary, &tmpl, on, prompter);
    CHECK(
        full.files.at(file) ==
        "class Foo {\n    int get() = win 0x10;\n    void set(int count, float scale) = win 0x20;\n}\n");
    CHECK(full.stats.return_types == 1);
    CHECK(full.stats.param_names == 1);  // `scale` was named by hand and stays
}

TEST_CASE("doc comments above a patched method survive") {
    auto file = fresh_dir("export-docs") / "t.bro";
    auto tmpl = load_template(file, "class Foo {\n    /// docs here\n    void run();\n}\n");
    auto binary = binary_of(Platform::Windows, class_of("Foo", {method("Foo", "run", {}, 0x9)}));
    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(plan.files.at(file) == "class Foo {\n    /// docs here\n    void run() = win 0x9;\n}\n");
}

TEST_CASE("replaced and added methods keep the file's indentation") {
    auto file = fresh_dir("export-indent") / "indent.bro";
    auto tmpl = load_template(file, "class Foo {\n    [[since(\"2.0\")]]\n    void run() = win 0x10;\n}\n");
    auto binary = binary_of(
        Platform::MacArm, class_of("Foo", {method("Foo", "run", {}, 0x40), method("Foo", "extra", {}, 0x50)}));

    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(
        plan.files.at(file) == "class Foo {\n    [[since(\"2.0\")]]\n    void run() = win 0x10, m1 0x40;\n    "
                               "TodoReturn extra() = m1 0x50;\n}\n");
}

TEST_CASE("android classes are linked by symbol and new classes follow vtable order") {
    ExtractedClass cls;
    cls.name = "ns::Thing";
    cls.bases = {"Base"};
    cls.methods = {
        method("ns::Thing", "b", {}, 0x1, true), method("ns::Thing", "a", {}, 0x2, true),
        method("ns::Thing", "z", {}, 0x3)};
    cls.vtable = {"b()", "a()"};
    auto binary = binary_of(Platform::Android64, cls);

    ExportOptions options;
    options.default_file = fresh_dir("export-android") / "fresh.bro";
    ExportPrompter prompter;
    auto plan = plan_for(binary, nullptr, options, prompter);

    CHECK(plan.stats.classes_created == 1);
    CHECK(plan.stats.bindings_created == 0);
    CHECK(
        plan.files.at(options.default_file) ==
        "[[link(android)]]\nclass ns::Thing : Base {\n    virtual TodoReturn b();\n    virtual TodoReturn a();\n"
        "    TodoReturn z();\n}\n");
    CHECK(parse_broma(plan.files.at(options.default_file)).isOk());
}

TEST_CASE("a class linked by symbol gets no android address, but a windows export of it does") {
    auto file = fresh_dir("export-linked") / "t.bro";
    auto tmpl = load_template(file, "[[link(android)]]\nclass Foo {\n    void run();\n}\n");

    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;

    auto droid = plan_for(
        binary_of(Platform::Android64, class_of("Foo", {method("Foo", "run", {}, 0x1)}, true)), &tmpl, options,
        prompter);
    CHECK(droid.files.empty());
    CHECK(droid.stats.bindings_added == 0);

    auto win = plan_for(
        binary_of(Platform::Windows, class_of("Foo", {method("Foo", "run", {}, 0x9)})), &tmpl, options, prompter);
    CHECK(win.files.at(file) == "[[link(android)]]\nclass Foo {\n    void run() = win 0x9;\n}\n");
}

TEST_CASE("a from-scratch export lists methods by name and counts exactly what it wrote") {
    auto named = method("Foo", "set", {"int", "float"}, 0x40);
    named.sig.params[0].name = "count";
    named.sig.params[1].name = "scale";
    named.sig.ret = TypeRef{"bool"};

    auto hidden = class_of("(anonymous namespace)::Hidden", {method("(anonymous namespace)::Hidden", "run", {}, 0x60)});
    ExtractedBinary binary;
    binary.platform = Platform::Windows;
    binary.classes = {class_of("Foo", {named, method("Foo", "other", {}, 0x50)}), hidden};

    auto file = fresh_dir("export-scratch") / "counted.bro";
    ExportPrompter prompter;

    ExportOptions off;
    off.default_file = file;
    auto plain = plan_for(binary, nullptr, off, prompter);
    CHECK(plain.stats.classes_created == 1);
    CHECK(plain.stats.classes_skipped == 1);
    CHECK(plain.stats.methods_created == 2);
    CHECK(plain.stats.bindings_created == 2);
    CHECK(plain.stats.return_types == 0);
    CHECK(plain.stats.param_names == 0);
    REQUIRE(plain.stats.warnings.size() == 1);
    CHECK(plain.stats.warnings[0].contains("anonymous namespace"));
    CHECK(
        plain.files.at(file) ==
        "class Foo {\n    TodoReturn other() = win 0x50;\n    TodoReturn set(int p0, float p1) = win 0x40;\n}\n");

    ExportOptions on = off;
    on.include_return_types = true;
    on.include_param_names = true;
    auto full = plan_for(binary, nullptr, on, prompter);
    CHECK(full.stats.return_types == 1);
    CHECK(full.stats.param_names == 2);
    CHECK(
        full.files.at(file) ==
        "class Foo {\n    TodoReturn other() = win 0x50;\n    bool set(int count, float scale) = win 0x40;\n}\n");
}

TEST_CASE("a new class is appended after the existing text with one blank line between") {
    auto file = fresh_dir("export-append") / "t.bro";
    auto tmpl = load_template(file, "class Old {\n    void a();\n}\n");
    auto binary = binary_of(Platform::Windows, class_of("Fresh", {method("Fresh", "b", {}, 0x1)}));

    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(plan.files.at(file) == "class Old {\n    void a();\n}\n\nclass Fresh {\n    TodoReturn b() = win 0x1;\n}\n");
    CHECK(plan.stats.classes_created == 1);
}

TEST_CASE("free functions are written once, in the default file") {
    ExtractedBinary binary;
    binary.platform = Platform::Windows;
    auto fn = method("", "ns::helper", {"int"}, 0x7);
    binary.free_functions = {fn, fn};

    auto file = fresh_dir("export-free") / "t.bro";
    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, nullptr, options, prompter);
    CHECK(plan.files.at(file) == "TodoReturn ns::helper(int p0) = win 0x7;\n");
    CHECK(plan.stats.methods_created == 1);
}

TEST_CASE("only_template_classes skips what the template lacks, ignore rules skip by name") {
    auto file = fresh_dir("export-only") / "t.bro";
    auto tmpl = load_template(file, "class Known {\n    void a();\n}\n");
    ExtractedBinary binary;
    binary.platform = Platform::Windows;
    binary.classes = {
        class_of("Known", {method("Known", "a", {}, 0x1)}), class_of("Unknown", {method("Unknown", "b", {}, 0x2)}),
        class_of("cocos2d::CCNode", {method("cocos2d::CCNode", "c", {}, 0x3)})};

    ExportOptions options;
    options.default_file = file;
    options.only_template_classes = true;
    options.ignore.namespaces = {"cocos2d"};
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(plan.stats.classes_skipped == 2);
    CHECK(plan.stats.classes_created == 0);
    CHECK(plan.files.at(file) == "class Known {\n    void a() = win 0x1;\n}\n");
}

TEST_CASE("an export with neither a template nor a default file is refused") {
    ExtractedBinary binary;
    ExportOptions options;
    ExportPrompter prompter;
    auto plan = plan_export(binary, nullptr, options, prompter);
    REQUIRE(plan.isErr());
    CHECK_FALSE(plan.unwrapErr().empty());
}

TEST_CASE("a template class that cannot be located in its file text is reported") {
    auto file = fresh_dir("export-lost") / "t.bro";
    auto tmpl = load_template(file, "class Foo {\n    void a();\n}\n");
    write_file(file, "// the class was deleted since the template was read\n");

    auto binary = binary_of(Platform::Windows, class_of("Foo", {method("Foo", "a", {}, 0x1)}));
    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    CHECK(plan.files.empty());
    REQUIRE(plan.stats.warnings.size() == 1);
    CHECK(plan.stats.warnings[0].contains("'Foo'"));
}

TEST_CASE("virtual functions in a different order from the vtable raise a warning") {
    auto file = fresh_dir("export-order") / "t.bro";
    auto tmpl = load_template(file, "class Foo {\n    virtual void b();\n    virtual void a();\n}\n");
    ExtractedClass cls = class_of("Foo", {method("Foo", "a", {}, 0x1, true), method("Foo", "b", {}, 0x2, true)});
    cls.vtable = {"a()", "b()"};
    auto binary = binary_of(Platform::Windows, cls);

    ExportOptions options;
    options.default_file = file;
    ExportPrompter prompter;
    auto plan = plan_for(binary, &tmpl, options, prompter);
    REQUIRE(plan.stats.warnings.size() == 1);
    CHECK(plan.stats.warnings[0].contains("ordered differently"));
}

TEST_CASE("ignore rules match whole namespaces and exact class names only") {
    IgnoreRules rules;
    rules.namespaces = {"cocos2d"};
    rules.classes = {"Special"};
    CHECK(rules.matches("cocos2d::CCNode"));
    CHECK(rules.matches("cocos2d::extension::CCScale9Sprite"));
    CHECK(rules.matches("Special"));
    CHECK_FALSE(rules.matches("cocos2dx::Other"));
    CHECK_FALSE(rules.matches("cocos2d"));
    CHECK_FALSE(rules.matches("SpecialCase"));
    CHECK_FALSE(rules.matches("ns::Special"));
}

TEST_CASE("a file that exists but can't be read is left alone") {
    auto dir = fresh_dir("export-unreadable") / "folder";
    REQUIRE(asp::fs::createDirAll(dir).isOk());  // reading a folder as text fails

    auto binary = binary_of(Platform::MacArm, class_of("Foo", {method("Foo", "run", {}, 0x40)}));
    ExportOptions options;
    options.default_file = dir;
    ExportPrompter prompter;
    auto plan = plan_for(binary, nullptr, options, prompter);

    CHECK(plan.files.empty());
    CHECK(plan.stats.classes_skipped == 1);
    CHECK(plan.stats.classes_created == 0);
    REQUIRE(plan.stats.warnings.size() == 1);
    CHECK(plan.stats.warnings[0].contains("can't be read"));
}

TEST_CASE("apply_plan writes every file, and names the earlier ones when a later write fails") {
    auto dir = fresh_dir("export-apply");
    ExportPlan plan;
    plan.files[dir / "a.bro"] = "class A {\n}\n";
    plan.files[dir / "sub" / "b.bro"] = "class B {\n}\n";
    REQUIRE(apply_plan(plan).isOk());
    auto a = asp::fs::readToString(dir / "a.bro");
    auto b = asp::fs::readToString(dir / "sub" / "b.bro");
    REQUIRE(a.isOk());
    REQUIRE(b.isOk());
    CHECK(a.unwrap() == "class A {\n}\n");
    CHECK(b.unwrap() == "class B {\n}\n");

    // `z` is a folder, so replacing it with a file fails after a.bro was already written
    auto failing = fresh_dir("export-apply-fail");
    REQUIRE(asp::fs::createDirAll(failing / "z" / "inner").isOk());
    ExportPlan bad;
    bad.files[failing / "a.bro"] = "class A {\n}\n";
    bad.files[failing / "z"] = "class Z {\n}\n";
    auto result = apply_plan(bad);
    REQUIRE(result.isErr());
    CHECK(result.unwrapErr().contains("already changed"));
    CHECK(result.unwrapErr().contains("a.bro"));
}
