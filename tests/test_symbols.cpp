#include <doctest/doctest.h>

#include <map>

#include <bromaida/exporter/symbols.hpp>

#include "common.hpp"

using namespace bromaida;

static std::vector<std::string> param_types(FunctionSignature const& sig) {
    std::vector<std::string> out;
    for (auto const& p : sig.params)
        out.push_back(p.type.str());
    return out;
}

TEST_CASE("parse_demangled_function reads class, name, constness and exact parameter types") {
    auto sig = parse_demangled_function("cocos2d::CCNode::setPosition(cocos2d::CCPoint const&) const");
    REQUIRE(sig.has_value());
    CHECK(sig->class_name == "cocos2d::CCNode");
    CHECK(sig->name == "setPosition");
    CHECK(sig->is_const);
    CHECK_FALSE(sig->is_variadic);
    CHECK(sig->ret.str() == "TodoReturn");
    CHECK(param_types(*sig) == Strings{"const cocos2d::CCPoint&"});

    auto free_fn = parse_demangled_function("ns::helper(int, float)");
    REQUIRE(free_fn.has_value());
    CHECK(free_fn->class_name == "ns");  // the scope is a class or a namespace: the caller decides
    CHECK(free_fn->name == "helper");
    CHECK_FALSE(free_fn->is_const);
    CHECK(param_types(*free_fn) == Strings{"int", "float"});

    auto none = parse_demangled_function("Foo::bar()");
    REQUIRE(none.has_value());
    CHECK(none->params.empty());
}

TEST_CASE("parse_demangled_function keeps nested templates, function pointers and ellipses intact") {
    auto map = parse_demangled_function("Foo::set(std::map<int, std::vector<float>, std::less<int>>&, int)");
    REQUIRE(map.has_value());
    CHECK(param_types(*map) == Strings{"std::map<int, std::vector<float>>&", "int"});

    auto fn_ptr = parse_demangled_function("Foo::on(void (*)(int, int), int)");
    REQUIRE(fn_ptr.has_value());
    CHECK(param_types(*fn_ptr) == Strings{"void (*)(int, int)", "int"});

    auto cv = parse_demangled_function("Foo::f(char const*, int&&, Foo**)");
    REQUIRE(cv.has_value());
    CHECK(param_types(*cv) == Strings{"const char*", "int&&", "Foo**"});

    auto variadic = parse_demangled_function("log(char const*, ...)");
    REQUIRE(variadic.has_value());
    CHECK(variadic->is_variadic);
    CHECK(param_types(*variadic) == Strings{"const char*"});

    auto msvc = parse_demangled_function("Foo::f(class Bar const&)");
    REQUIRE(msvc.has_value());
    CHECK(param_types(*msvc) == Strings{"const Bar&"});

    auto ndk = parse_demangled_function(
        "Foo::name(std::__ndk1::basic_string<char, std::__ndk1::char_traits<char>, std::__ndk1::allocator<char>> "
        "const&)");
    REQUIRE(ndk.has_value());
    CHECK(param_types(*ndk) == Strings{"const std::string&"});
}

TEST_CASE("parse_demangled_function accepts the operator overloads Broma can write") {
    struct Op {
        std::string_view text;
        std::string_view name;
        Strings params;
        bool is_const;
    };
    std::vector<Op> const ops{
        {"Foo::operator==(Foo const&) const", "operator==", {"const Foo&"}, true},
        {"Foo::operator!=(Foo const&) const", "operator!=", {"const Foo&"}, true},
        {"Foo::operator()(int)", "operator()", {"int"}, false},
        {"Foo::operator()()", "operator()", {}, false},
        {"Foo::operator<<(int)", "operator<<", {"int"}, false},
        {"Foo::operator>>(int)", "operator>>", {"int"}, false},
        {"Foo::operator<(Foo const&) const", "operator<", {"const Foo&"}, true},
        {"Foo::operator>(Foo const&) const", "operator>", {"const Foo&"}, true},
        {"Foo::operator[](int)", "operator[]", {"int"}, false},
        {"Foo::operator->()", "operator->", {}, false},
        {"Foo::operator=(Foo const&)", "operator=", {"const Foo&"}, false},
    };
    for (auto const& op : ops) {
        SUBCASE(std::string{op.text}.c_str()) {
            auto sig = parse_demangled_function(op.text);
            REQUIRE(sig.has_value());
            CHECK(sig->class_name == "Foo");
            CHECK(sig->name == op.name);
            CHECK(sig->is_const == op.is_const);
            CHECK(param_types(*sig) == op.params);
        }
    }
}

TEST_CASE("parse_demangled_function rejects what Broma cannot express") {
    for (std::string_view text :
         {"Foo::operator int()",
          "Foo::operator bool() const",
          "Foo::operator Bar*()",
          "non-virtual thunk to Foo::bar()",
          "virtual thunk to Foo::bar()",
          "covariant return thunk to Foo::bar()",
          "vtable for Foo",
          "typeinfo for Foo",
          "typeinfo name for Foo",
          "VTT for Foo",
          "guard variable for Foo::x",
          "Foo<int>::bar()",
          "Foo::bar<int>(int)",
          "(anonymous namespace)::helper()",
          "Foo::bar() [clone .cold]",
          "Foo::bar()::{lambda(int)#1}::operator()(int) const",
          "Foo::bar",
          "Foo::bar(int*<float>)",
          "",
          "()"}) {
        CAPTURE(text);
        CHECK_FALSE(parse_demangled_function(text).has_value());
    }
}

TEST_CASE("parse_demangled_function handles const and volatile qualifiers on the method") {
    auto cv = parse_demangled_function("Foo::bar() const volatile");
    REQUIRE(cv.has_value());
    CHECK(cv->name == "bar");
    CHECK(cv->is_const);

    auto v = parse_demangled_function("Foo::bar() volatile");
    REQUIRE(v.has_value());
    CHECK_FALSE(v->is_const);
}

using Table = std::map<std::string, std::string, std::less<>>;

static ExtractInput input_with(Table const& table, std::vector<RawSymbol> symbols, std::vector<ClassLayout> layouts) {
    ExtractInput input;
    input.platform = Platform::Android64;
    input.demangle = [&table](std::string_view mangled) -> std::optional<std::string> {
        auto it = table.find(mangled);
        return it == table.end() ? std::nullopt : std::optional{it->second};
    };
    input.symbols = std::move(symbols);
    input.layouts = std::move(layouts);
    return input;
}

static std::vector<std::pair<std::string, std::uint64_t>> summary(ExtractedClass const& cls) {
    std::vector<std::pair<std::string, std::uint64_t>> out;
    for (auto const& m : cls.methods)
        out.emplace_back(m.sig.name, m.bind.offset);
    return out;
}

TEST_CASE("extract_binary merges ABI variants, keeps overloads distinct and marks virtuals") {
    Table const table{
        {"_ZTV3Foo", "vtable for Foo"},   {"_ZFoo_C1", "Foo::Foo()"},          {"_ZFoo_C2", "Foo::Foo()"},
        {"_ZFoo_init", "Foo::init(int)"}, {"_ZFoo_set_i", "Foo::set(int)"},    {"_ZFoo_set_f", "Foo::set(float)"},
        {"_ZFoo_get", "Foo::get()"},      {"_ZFoo_get_c", "Foo::get() const"}, {"_ZFoo_D2", "Foo::~Foo()"},
        {"_ZFoo_D1", "Foo::~Foo()"},      {"_ZFoo_D0", "Foo::~Foo()"},         {"_Zns_free", "ns::helper(int)"},
    };
    auto input = input_with(
        table,
        {
            {"_ZTV3Foo", 0x10, false, std::nullopt},
            {"_ZFoo_C2", 0x180, true, std::nullopt},
            {"_ZFoo_C1", 0x100, true, std::nullopt},
            {"_ZFoo_init", 0x300, true, std::nullopt},
            {"_ZFoo_set_i", 0x500, true, std::nullopt},
            {"_ZFoo_set_f", 0x600, true, std::nullopt},
            {"_ZFoo_get", 0x700, true, std::nullopt},
            {"_ZFoo_get_c", 0x800, true, std::nullopt},
            {"_ZFoo_D1", 0x910, true, std::nullopt},
            {"_ZFoo_D2", 0x900, true, std::nullopt},
            {"_ZFoo_D0", 0x920, true, std::nullopt},
            {"_Zns_free", 0xA00, true, std::nullopt},
            {"not_mangled", 0xB00, true, std::nullopt},
            {"_Z_unknown", 0xC00, true, std::nullopt},
        },
        {{"Foo", {"Base"}, {"_ZFoo_init"}}});

    auto result = extract_binary(input);
    REQUIRE(result.classes.size() == 1);
    auto const& foo = result.classes.front();
    CHECK(foo.name == "Foo");
    CHECK(foo.bases == Strings{"Base"});
    CHECK(foo.vtable == Strings{"init(int)"});

    CHECK(
        summary(foo) == std::vector<std::pair<std::string, std::uint64_t>>{
                            {"Foo", 0x100},
                            {"init", 0x300},
                            {"set", 0x500},
                            {"set", 0x600},
                            {"get", 0x700},
                            {"get", 0x800},
                            {"~Foo", 0x900}});

    auto const& methods = foo.methods;
    REQUIRE(methods.size() == 7);
    CHECK(methods[0].sig.is_ctor);
    CHECK(methods[0].sig.ret.empty());
    CHECK(methods[1].sig.is_virtual);
    CHECK_FALSE(methods[2].sig.is_virtual);
    CHECK(param_types(methods[2].sig) == Strings{"int"});
    CHECK(param_types(methods[3].sig) == Strings{"float"});
    CHECK_FALSE(methods[4].sig.is_const);
    CHECK(methods[5].sig.is_const);
    CHECK(methods[6].sig.is_dtor);
    CHECK(methods[6].sig.ret.empty());

    REQUIRE(result.free_functions.size() == 1);
    CHECK(result.free_functions.front().sig.name == "ns::helper");
    CHECK(result.free_functions.front().sig.class_name.empty());
    CHECK(result.free_functions.front().bind.offset == 0xA00);
    CHECK(result.platform == Platform::Android64);
}

TEST_CASE("extract_binary finds a class from its vtable symbol alone and ignores non-functions") {
    Table const table{{"_ZTV3Bar", "vtable for Bar"}, {"_ZTV3Foo", "vtable for Foo"}, {"_ZFoo_x", "Foo::x()"}};
    auto input = input_with(
        table,
        {
            {"_ZTV3Bar", 0x10, false, std::nullopt},
            {"_ZTV3Foo", 0x20, false, std::nullopt},
            {"_ZFoo_x", 0x30, true, std::nullopt},
            {"_ZFoo_x", 0x40, false, std::nullopt},
        },
        {});
    auto result = extract_binary(input);
    REQUIRE(result.classes.size() == 2);
    CHECK(result.classes[0].name == "Bar");
    CHECK(result.classes[0].methods.empty());
    CHECK(result.classes[1].name == "Foo");
    CHECK(summary(result.classes[1]) == std::vector<std::pair<std::string, std::uint64_t>>{{"x", 0x30}});
    CHECK(result.free_functions.empty());
}

TEST_CASE("extract_binary takes staticness from the symbol when it is known") {
    Table const table{{"_ZTV3Foo", "vtable for Foo"}, {"_ZFoo_a", "Foo::a()"}, {"_ZFoo_b", "Foo::b()"}};
    auto input = input_with(
        table,
        {
            {"_ZTV3Foo", 0x10, false, std::nullopt},
            {"_ZFoo_a", 0x20, true, true},
            {"_ZFoo_b", 0x30, true, false},
        },
        {});
    auto result = extract_binary(input);
    REQUIRE(result.classes.size() == 1);
    REQUIRE(result.classes.front().methods.size() == 2);
    CHECK(result.classes.front().methods[0].sig.is_static);
    CHECK_FALSE(result.classes.front().methods[1].sig.is_static);
}

TEST_CASE("extract_binary marks a virtual override only when the vtable names it") {
    Table const table{
        {"_ZTV3Foo", "vtable for Foo"},
        {"_ZFoo_a", "Foo::a(int)"},
        {"_ZFoo_b", "Foo::a(float)"},
        {"_ZFoo_c", "Foo::c()"}};
    auto input = input_with(
        table,
        {
            {"_ZTV3Foo", 0x10, false, std::nullopt},
            {"_ZFoo_a", 0x20, true, std::nullopt},
            {"_ZFoo_b", 0x30, true, std::nullopt},
            {"_ZFoo_c", 0x40, true, std::nullopt},
        },
        {{"Foo", {}, {"_ZFoo_b"}}});
    auto result = extract_binary(input);
    REQUIRE(result.classes.size() == 1);
    auto const& methods = result.classes.front().methods;
    REQUIRE(methods.size() == 3);
    CHECK_FALSE(methods[0].sig.is_virtual);  // a(int)
    CHECK(methods[1].sig.is_virtual);        // a(float)
    CHECK_FALSE(methods[2].sig.is_virtual);  // c()
}
