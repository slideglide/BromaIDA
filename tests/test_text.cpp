#include <doctest/doctest.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <bromaida/typegen/stl_types.hpp>
#include <bromaida/util/text.hpp>

#include "common.hpp"

using namespace bromaida;

TEST_CASE("normalize_type gives one exact spelling and is idempotent") {
    constexpr std::array cases{
        Case{"gd::string const &", "const std::string&"},
        Case{"gd::string&&", "std::string&&"},
        Case{"gd::string", "std::string"},
        Case{"  std::map< int ,float >  ", "std::map<int, float>"},
        Case{"std::map<int,std::vector<float>>", "std::map<int, std::vector<float>>"},
        Case{"Foo *", "Foo*"},
        Case{"Foo**", "Foo**"},
        Case{"Foo const*", "const Foo*"},
        Case{"Foo const &", "const Foo&"},
        Case{"cocos2d::CCPoint const&", "const cocos2d::CCPoint&"},
        Case{"unsigned   int", "unsigned int"},
        Case{"void(*)(int,int)", "void (*)(int, int)"},
        // `gd::` only counts at an identifier boundary
        Case{"cgd::Foo", "cgd::Foo"},
        Case{"my_gd::string", "my_gd::string"},
        Case{"ns::gd::Foo", "ns::gd::Foo"},
        Case{"std::vector<gd::string>", "std::vector<std::string>"},
        Case{"", ""},
    };
    for (auto const& c : cases) {
        CAPTURE(c.input);
        auto const once = cpp::normalize_type(c.input);
        CHECK(once == c.expected);
        CHECK(cpp::normalize_type(once) == once);
    }
}

TEST_CASE("normalize_type moves const west of every pointer level") {
    CHECK(cpp::normalize_type("Foo const * const") == "const Foo* const");
    CHECK(cpp::normalize_type("Foo const**") == "const Foo**");
    CHECK(cpp::normalize_type("char const* const*") == "const char* const*");
}

TEST_CASE("format_ptr spaces pointers the way IDA prints them and is idempotent") {
    constexpr std::array cases{
        Case{"Foo*", "Foo *"},
        Case{"Foo**", "Foo **"},
        Case{"const char*", "const char *"},
        Case{"Foo *", "Foo *"},
        Case{"std::vector<Foo*>", "std::vector<Foo *>"},
        Case{"Foo", "Foo"},
    };
    for (auto const& c : cases) {
        CAPTURE(c.input);
        auto const once = cpp::format_ptr(c.input);
        CHECK(once == c.expected);
        CHECK(cpp::format_ptr(once) == once);
    }
}

TEST_CASE("strip_crp removes const, pointers and references") {
    constexpr std::array cases{
        Case{"const Foo*&", "Foo"},
        Case{"Foo const*", "Foo"},
        Case{"const Foo", "Foo"},
        Case{"Foo&&", "Foo"},
        Case{"Foo", "Foo"},
        Case{"  const ns::Foo **  ", "ns::Foo"},
        Case{"std::map<int, Foo*>*", "std::map<int, Foo*>"},
    };
    for (auto const& c : cases) {
        CAPTURE(c.input);
        CHECK(cpp::strip_crp(c.input) == c.expected);
    }
}

TEST_CASE("split_top_level ignores commas inside <> and ()") {
    CHECK(
        cpp::split_top_level("int, std::map<int, int>, void(*)(int, int)") ==
        Strings{"int", "std::map<int, int>", "void(*)(int, int)"});
    CHECK(cpp::split_top_level("std::map<int, float>") == Strings{"std::map<int, float>"});
    CHECK(
        cpp::split_top_level("std::map<int, std::vector<std::pair<int, int>>>, float") ==
        Strings{"std::map<int, std::vector<std::pair<int, int>>>", "float"});
    CHECK(cpp::split_top_level("  a ,  b  ") == Strings{"a", "b"});
    CHECK(cpp::split_top_level("std::function<void(int, int)>") == Strings{"std::function<void(int, int)>"});
    CHECK(cpp::split_top_level("void").empty());
    CHECK(cpp::split_top_level("   ").empty());
    CHECK(cpp::split_top_level("").empty());
}

TEST_CASE("split_qualified separates namespaces from the bare name") {
    auto simple = cpp::split_qualified("a::b::C");
    CHECK(simple.namespaces == Strings{"a", "b"});
    CHECK(simple.bare == "C");
    CHECK(simple.scope() == "a::b");

    auto tpl = cpp::split_qualified("std::vector<a::b>::foo");
    CHECK(tpl.namespaces == Strings{"std", "vector<a::b>"});
    CHECK(tpl.bare == "foo");

    auto bare = cpp::split_qualified("Foo");
    CHECK(bare.namespaces.empty());
    CHECK(bare.bare == "Foo");
    CHECK(bare.scope().empty());

    auto call_op = cpp::split_qualified("ns::Foo::operator()");
    CHECK(call_op.namespaces == Strings{"ns", "Foo"});
    CHECK(call_op.bare == "operator()");

    auto shift = cpp::split_qualified("ns::Foo::operator<<");
    CHECK(shift.namespaces == Strings{"ns", "Foo"});
    CHECK(shift.bare == "operator<<");
}

TEST_CASE("STL expansion writes out default template arguments exactly") {
    CHECK(stl::expand("std::vector<int>") == "std::vector<int, std::allocator<int>>");
    CHECK(stl::expand("std::vector<char const*>") == "std::vector<const char*, std::allocator<const char*>>");
    CHECK(stl::expand("std::vector<const char*>") == "std::vector<const char*, std::allocator<const char*>>");
    CHECK(
        stl::expand("std::map<int, float>") ==
        "std::map<int, float, std::less<int>, std::allocator<std::pair<const int, float>>>");
    CHECK(stl::expand("std::string") == "std::basic_string<char, std::char_traits<char>, std::allocator<char>>");
    CHECK(
        stl::expand("const std::string&") ==
        "const std::basic_string<char, std::char_traits<char>, std::allocator<char>>&");

    std::string const inner = "std::map<int, float, std::less<int>, std::allocator<std::pair<const int, float>>>";
    CHECK(
        stl::expand("std::vector<std::map<int, float>>") ==
        "std::vector<" + inner + ", std::allocator<" + inner + ">>");
    CHECK(stl::expand("Foo*") == "Foo*");
}

TEST_CASE("STL expansion is idempotent") {
    for (std::string_view type :
         {"std::vector<int>", "std::map<int, float>", "std::set<std::string>", "std::unordered_map<int, Foo*>",
          "std::vector<std::map<int, float>>", "const std::string&", "std::pair<int, std::list<Foo>>"}) {
        CAPTURE(type);
        auto const once = stl::expand(type);
        CHECK(stl::expand(once) == once);
    }
}

TEST_CASE("stl::parse builds the exact node tree") {
    using stl::Node;
    CHECK(stl::parse("const std::vector<int>*") == Node{"const", "std::vector", {Node{"", "int", {}, ""}}, "*"});
    CHECK(
        stl::parse("std::map<int, std::vector<float>>") ==
        Node{"", "std::map", {Node{"", "int", {}, ""}, Node{"", "std::vector", {Node{"", "float", {}, ""}}, ""}}, ""});
    CHECK(stl::parse("Foo const&") == Node{"const", "Foo", {}, "&"});
    CHECK(stl::parse("std::string") == Node{"", "std::string", {}, ""});
    CHECK(stl::parse("") == Node{});
}

TEST_CASE("sugar undoes libc++ inline namespaces and default arguments, and is idempotent") {
    constexpr std::array cases{
        Case{"std::__ndk1::vector<int, std::__ndk1::allocator<int>>", "std::vector<int>"},
        Case{
            "std::__ndk1::basic_string<char, std::__ndk1::char_traits<char>, std::__ndk1::allocator<char>> const&",
            "const std::string&"},
        Case{
            "std::__ndk1::map<int, float, std::__ndk1::less<int>, "
            "std::__ndk1::allocator<std::__ndk1::pair<const int, float>>>",
            "std::map<int, float>"},
        Case{
            "std::__1::vector<std::__1::basic_string<char, std::__1::char_traits<char>, "
            "std::__1::allocator<char>>, std::__1::allocator<std::__1::basic_string<char, "
            "std::__1::char_traits<char>, std::__1::allocator<char>>>>",
            "std::vector<std::string>"},
        Case{"std::__cxx11::list<Foo*, std::allocator<Foo*>>", "std::list<Foo*>"},
        Case{"Foo*", "Foo*"},
        Case{"int", "int"},
    };
    for (auto const& c : cases) {
        CAPTURE(c.input);
        auto const once = stl::sugar(c.input);
        CHECK(once == c.expected);
        CHECK(stl::sugar(once) == once);
    }
}

TEST_CASE("stl::value_types reports every leaf and whether it is held by value") {
    using Leaves = std::vector<std::pair<std::string, bool>>;
    CHECK(stl::value_types("std::vector<Foo*>") == Leaves{{"Foo", false}});
    CHECK(stl::value_types("std::vector<Foo>") == Leaves{{"Foo", true}});
    CHECK(stl::value_types("std::map<Foo, Bar*>") == Leaves{{"Foo", true}, {"Bar", false}});
    CHECK(stl::value_types("std::vector<std::pair<A, B&>>") == Leaves{{"A", true}, {"B", false}});
    CHECK(stl::value_types("std::string").empty());
    CHECK(stl::value_types("std::mutex").empty());
}

TEST_CASE("is_generated_arg_name accepts only p/a followed by digits") {
    CHECK(cpp::is_generated_arg_name("p0"));
    CHECK(cpp::is_generated_arg_name("a12"));
    CHECK(cpp::is_generated_arg_name("p007"));
    CHECK_FALSE(cpp::is_generated_arg_name("p"));
    CHECK_FALSE(cpp::is_generated_arg_name("pa1"));
    CHECK_FALSE(cpp::is_generated_arg_name("x1"));
    CHECK_FALSE(cpp::is_generated_arg_name("p1x"));
    CHECK_FALSE(cpp::is_generated_arg_name("P1"));
    CHECK_FALSE(cpp::is_generated_arg_name(""));
}

TEST_CASE("is_identifier_path rejects every malformed path") {
    CHECK(cpp::is_identifier_path("Foo"));
    CHECK(cpp::is_identifier_path("a::B_c"));
    CHECK(cpp::is_identifier_path("_x::y1::z"));
    CHECK_FALSE(cpp::is_identifier_path("::a"));
    CHECK_FALSE(cpp::is_identifier_path("a::"));
    CHECK_FALSE(cpp::is_identifier_path("a::::b"));
    CHECK_FALSE(cpp::is_identifier_path("a:::b"));
    CHECK_FALSE(cpp::is_identifier_path("a:b"));
    CHECK_FALSE(cpp::is_identifier_path("1a"));
    CHECK_FALSE(cpp::is_identifier_path("a::1b"));
    CHECK_FALSE(cpp::is_identifier_path("a<b>"));
    CHECK_FALSE(cpp::is_identifier_path("a b"));
    CHECK_FALSE(cpp::is_identifier_path(""));
}

TEST_CASE("strip_numeric_suffix removes only a trailing _<digits>") {
    CHECK(cpp::strip_numeric_suffix("foo_2") == "foo");
    CHECK(cpp::strip_numeric_suffix("foo_12") == "foo");
    CHECK(cpp::strip_numeric_suffix("foo") == "foo");
    CHECK(cpp::strip_numeric_suffix("foo2") == "foo2");
    CHECK(cpp::strip_numeric_suffix("_2") == "_2");
    CHECK(cpp::strip_numeric_suffix("foo_") == "foo_");
    CHECK(cpp::strip_numeric_suffix("a_1_2") == "a_1");
    CHECK(cpp::strip_numeric_suffix("") == "");
}

TEST_CASE("split_call splits at the paren matching the final )") {
    auto c = cpp::split_call("Foo::bar(int, float)");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::bar");
    CHECK(c->args == "int, float");

    c = cpp::split_call("Foo::bar()");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::bar");
    CHECK(c->args == "");

    c = cpp::split_call("Foo::bar(void(*)(int))");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::bar");
    CHECK(c->args == "void(*)(int)");

    c = cpp::split_call("Foo::operator()(int)");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::operator()");
    CHECK(c->args == "int");

    c = cpp::split_call("Foo::operator()()");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::operator()");
    CHECK(c->args == "");

    c = cpp::split_call("Foo::operator==(Foo const&)");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::operator==");
    CHECK(c->args == "Foo const&");

    c = cpp::split_call("Foo::operator[](int)");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::operator[]");
    CHECK(c->args == "int");

    c = cpp::split_call("Foo::operator->()");
    REQUIRE(c.has_value());
    CHECK(c->name == "Foo::operator->");

    CHECK_FALSE(cpp::split_call("Foo::bar").has_value());
    CHECK_FALSE(cpp::split_call("Foo::bar(int) const").has_value());
    CHECK_FALSE(cpp::split_call("Foo bar(int)").has_value());
    CHECK_FALSE(cpp::split_call("(int)").has_value());
    CHECK_FALSE(cpp::split_call("").has_value());
    CHECK_FALSE(cpp::split_call("bar(int").has_value());
}

TEST_CASE("looks_malformed flags a template list after a pointer") {
    CHECK(cpp::looks_malformed("Foo*<int>"));
    CHECK(cpp::looks_malformed("Foo& <int>"));
    CHECK(cpp::looks_malformed("std::vector<Foo*<int>>"));
    CHECK_FALSE(cpp::looks_malformed("std::vector<Foo*>"));
    CHECK_FALSE(cpp::looks_malformed("Foo<int>*"));
    CHECK_FALSE(cpp::looks_malformed("Foo"));
}

TEST_CASE("clean_demangled_type drops MSVC tag keywords only at identifier boundaries") {
    constexpr std::array cases{
        Case{"class Foo", "Foo"},
        Case{"struct ns::Foo const&", "const ns::Foo&"},
        Case{"enum Bar*", "Bar*"},
        Case{"std::vector<class Foo*>", "std::vector<Foo*>"},
        Case{"subclass Foo", "subclass Foo"},
        Case{"my_struct Foo", "my_struct Foo"},
        Case{"Foo", "Foo"},
    };
    for (auto const& c : cases) {
        CAPTURE(c.input);
        auto const once = cpp::clean_demangled_type(c.input);
        CHECK(once == c.expected);
        CHECK(cpp::clean_demangled_type(once) == once);
    }
}

TEST_CASE("replace_all replaces non-overlapping matches left to right") {
    CHECK(cpp::replace_all("a::b::c", "::", ".") == "a.b.c");
    CHECK(cpp::replace_all("aaa", "a", "aa") == "aaaaaa");
    CHECK(cpp::replace_all("aaaa", "aa", "b") == "bb");
    CHECK(cpp::replace_all("abc", "x", "y") == "abc");
    CHECK(cpp::replace_all("abc", "", "x") == "abc");
    CHECK(cpp::replace_all("", "a", "b") == "");
    CHECK(cpp::replace_all("xx", "x", "") == "");
}

TEST_CASE("wrap_namespaces nests the body in order") {
    Strings const two{"a", "b"};
    CHECK(cpp::wrap_namespaces(two, "class X;", true) == "namespace a { namespace b { class X; } }");
    CHECK(cpp::wrap_namespaces(two, "x;") == "namespace a { namespace b {\nx;\n} }");
    CHECK(cpp::wrap_namespaces(Strings{"only"}, "x;", true) == "namespace only { x; }");
    CHECK(cpp::wrap_namespaces({}, "x;") == "x;");
    CHECK(cpp::wrap_namespaces({}, "x;", true) == "x;");
}

TEST_CASE("trim, lower_ascii and split_lines") {
    CHECK(cpp::trim("  \t x y \n") == "x y");
    CHECK(cpp::trim(" \n ") == "");
    CHECK(cpp::trim("") == "");
    CHECK(cpp::trim("x") == "x");
    CHECK(cpp::lower_ascii("AbC_1") == "abc_1");
    CHECK(cpp::lower_ascii("") == "");
    CHECK(cpp::split_lines("a\nb\r\nc") == Strings{"a", "b", "c"});
    CHECK(cpp::split_lines("a\n\nb") == Strings{"a", "", "b"});
    CHECK(cpp::split_lines("").empty());
}
