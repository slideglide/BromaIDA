#include <doctest/doctest.h>

#include <bromaida/stl_types.hpp>
#include <bromaida/text.hpp>

using namespace bromaida;

TEST_CASE("normalize_type") {
    CHECK(cpp::normalize_type("gd::string const &") == "const std::string&");
    CHECK(cpp::normalize_type("  std::map< int ,float >  ") == "std::map<int, float>");
    CHECK(cpp::normalize_type("Foo *") == "Foo*");
    CHECK(cpp::normalize_type("Foo const*") == "const Foo*");
    CHECK(cpp::normalize_type("cgd::Foo") == "cgd::Foo");  // not a gd:: namespace
}

TEST_CASE("split helpers") {
    CHECK(cpp::split_top_level("int, std::map<int, int>, void(*)(int, int)").size() == 3);
    CHECK(cpp::split_top_level("void").empty());
    auto q = cpp::split_qualified("a::b::C");
    CHECK(q.scope() == "a::b");
    CHECK(q.bare == "C");
    CHECK(cpp::split_qualified("std::vector<a::b>::foo").namespaces.size() == 2);  // `a::b` stays inside the template
}

TEST_CASE("STL expansion and sugar") {
    CHECK(stl::expand("std::vector<int>") == "std::vector<int, std::allocator<int>>");
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
