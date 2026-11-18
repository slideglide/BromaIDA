#include <doctest/doctest.h>

#include <bromaida/stl_types.hpp>
#include <bromaida/text.hpp>

using namespace bromaida;

TEST_CASE("normalize_type") {
    CHECK(cpp::normalize_type("gd::string const &") == "const std::string&");
    CHECK(cpp::normalize_type("  std::map< int ,float >  ") == "std::map<int, float>");
    CHECK(cpp::normalize_type("Foo *") == "Foo*");
    CHECK(cpp::normalize_type("Foo const*") == "const Foo*");
    CHECK(cpp::normalize_type("cgd::Foo") == "cgd::Foo");
}

TEST_CASE("split helpers") {
    CHECK(cpp::split_top_level("int, std::map<int, int>, void(*)(int, int)").size() == 3);
    CHECK(cpp::split_top_level("void").empty());
    auto q = cpp::split_qualified("a::b::C");
    CHECK(q.scope() == "a::b");
    CHECK(q.bare == "C");
    CHECK(cpp::split_qualified("std::vector<a::b>::foo").namespaces.size() == 2);
}

TEST_CASE("STL expansion and sugar") {
    CHECK(stl::expand("std::vector<int>") == "std::vector<int, std::allocator<int>>");
    CHECK(stl::expand("std::vector<char const*>") == "std::vector<const char*, std::allocator<const char*>>");
    CHECK(stl::expand("std::vector<const char*>") == "std::vector<const char*, std::allocator<const char*>>");
    CHECK(stl::sugar("std::__ndk1::vector<int, std::__ndk1::allocator<int>>") == "std::vector<int>");
    CHECK(
        stl::sugar(
            "std::__ndk1::basic_string<char, std::__ndk1::char_traits<char>, std::__ndk1::allocator<char>> const&") ==
        "const std::string&");
    auto leaves = stl::value_types("std::vector<Foo*>");
    REQUIRE(leaves.size() == 1);
    CHECK(leaves[0].first == "Foo");
    CHECK_FALSE(leaves[0].second);
}

TEST_CASE("is_generated_arg_name") {
    CHECK(cpp::is_generated_arg_name("p0"));
    CHECK(cpp::is_generated_arg_name("a12"));
    CHECK_FALSE(cpp::is_generated_arg_name("p"));
    CHECK_FALSE(cpp::is_generated_arg_name("pa1"));
    CHECK_FALSE(cpp::is_generated_arg_name("x1"));
}

TEST_CASE("is_identifier_path") {
    CHECK(cpp::is_identifier_path("Foo"));
    CHECK(cpp::is_identifier_path("a::B_c"));
    CHECK_FALSE(cpp::is_identifier_path("::a"));
    CHECK_FALSE(cpp::is_identifier_path("a::"));
    CHECK_FALSE(cpp::is_identifier_path("a::::b"));
    CHECK_FALSE(cpp::is_identifier_path("1a"));
    CHECK_FALSE(cpp::is_identifier_path("a<b>"));
    CHECK_FALSE(cpp::is_identifier_path(""));
}

TEST_CASE("strip_numeric_suffix") {
    CHECK(cpp::strip_numeric_suffix("foo_2") == "foo");
    CHECK(cpp::strip_numeric_suffix("foo") == "foo");
    CHECK(cpp::strip_numeric_suffix("_2") == "_2");
    CHECK(cpp::strip_numeric_suffix("foo_") == "foo_");
    CHECK(cpp::strip_numeric_suffix("a_1_2") == "a_1");
}

TEST_CASE("split_call") {
    auto c = cpp::split_call("Foo::bar(int, float)");
    REQUIRE(c);
    CHECK(c->name == "Foo::bar");
    CHECK(c->args == "int, float");

    c = cpp::split_call("Foo::bar()");
    REQUIRE(c);
    CHECK(c->args.empty());

    c = cpp::split_call("Foo::bar(void(*)(int))");
    REQUIRE(c);
    CHECK(c->name == "Foo::bar");
    CHECK(c->args == "void(*)(int)");

    c = cpp::split_call("Foo::operator()(int)");
    REQUIRE(c);
    CHECK(c->name == "Foo::operator()");
    CHECK(c->args == "int");

    CHECK_FALSE(cpp::split_call("Foo::bar"));
    CHECK_FALSE(cpp::split_call("Foo::bar(int) const"));
    CHECK_FALSE(cpp::split_call("Foo bar(int)"));
    CHECK_FALSE(cpp::split_call("(int)"));
}
