#include <doctest/doctest.h>

#include <map>

#include <bromaida/symbols.hpp>

using namespace bromaida;

TEST_CASE("parse_demangled_function") {
    auto sig = parse_demangled_function("cocos2d::CCNode::setPosition(cocos2d::CCPoint const&) const");
    REQUIRE(sig);
    CHECK(sig->class_name == "cocos2d::CCNode");
    CHECK(sig->name == "setPosition");
    CHECK(sig->is_const);
    REQUIRE(sig->params.size() == 1);
    CHECK(sig->params[0].type.str() == "const cocos2d::CCPoint&");

    CHECK_FALSE(parse_demangled_function("non-virtual thunk to Foo::bar()"));
    CHECK_FALSE(parse_demangled_function("Foo::operator int()"));
    CHECK(parse_demangled_function("Foo::operator==(Foo const&) const"));
    CHECK(parse_demangled_function("Foo::operator()(int)"));
    CHECK_FALSE(parse_demangled_function("Foo<int>::bar()"));
}

TEST_CASE("extract_binary groups classes, merges ctor variants, marks virtuals") {
    static std::map<std::string, std::string, std::less<>> const table{
        {"_ZTV3Foo", "vtable for Foo"},    {"_Z_Foo_C1", "Foo::Foo()"},    {"_Z_Foo_C2", "Foo::Foo()"},
        {"_Z_Foo_init", "Foo::init(int)"}, {"_Z_Foo_dtor", "Foo::~Foo()"}, {"_Z_ns_free", "ns::helper(int)"},
    };

    ExtractInput input;
    input.platform = Platform::Android64;
    input.demangle = [](std::string_view mangled) -> std::optional<std::string> {
        auto it = table.find(mangled);
        return it == table.end() ? std::nullopt : std::optional{it->second};
    };
    input.symbols = {
        {"_ZTV3Foo", 0x10, false, std::nullopt},    {"_Z_Foo_C1", 0x100, true, std::nullopt},
        {"_Z_Foo_C2", 0x200, true, std::nullopt},   {"_Z_Foo_init", 0x300, true, std::nullopt},
        {"_Z_Foo_dtor", 0x400, true, std::nullopt}, {"_Z_ns_free", 0x500, true, std::nullopt},
    };
    input.layouts = {{"Foo", {"Base"}, {"_Z_Foo_init"}}};

    auto result = extract_binary(input);
    REQUIRE(result.classes.size() == 1);
    auto const& foo = result.classes[0];
    CHECK(foo.name == "Foo");
    CHECK(foo.bases == std::vector<std::string>{"Base"});
    CHECK(foo.methods.size() == 3);  // one ctor, init, dtor
    REQUIRE(result.free_functions.size() == 1);
    CHECK(result.free_functions[0].sig.name == "ns::helper");
    for (auto const& m : foo.methods) {
        if (m.sig.name == "init") CHECK(m.sig.is_virtual);
        if (m.sig.is_ctor) CHECK(m.bind.offset == 0x100);
    }
}
