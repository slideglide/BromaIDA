#include <doctest/doctest.h>

#include <bromaida/broma/bindings.hpp>
#include <bromaida/exporter/exporter.hpp>

#include "common.hpp"

using namespace bromaida;

static Binding bind_at(std::string cls, std::string name, std::uint64_t offset) {
    Binding binding;
    binding.class_name = std::move(cls);
    binding.name = std::move(name);
    binding.bind = Bind{BindState::Bound, offset};
    return binding;
}

static Strings names(std::vector<Binding> const& group) {
    Strings out;
    for (auto const& b : group)
        out.push_back(b.qualified_name());
    return out;
}

TEST_CASE("bindings that share an address are grouped, the rest stay in order") {
    BindingSet set;
    set.add(bind_at("A", "one", 0x10));
    set.add(bind_at("B", "two", 0x20));
    set.add(bind_at("C", "three", 0x10));
    set.add(bind_at("D", "four", 0x10));
    set.add(bind_at("E", "five", 0x30));

    CHECK(names(set.bindings) == Strings{"B::two", "E::five"});
    REQUIRE(set.duplicates.size() == 1);
    REQUIRE(set.duplicates.contains(0x10));
    CHECK(names(set.duplicates.at(0x10)) == Strings{"A::one", "C::three", "D::four"});
}

TEST_CASE("a binding added after a group was formed joins it, and later addresses stay indexed") {
    BindingSet set;
    set.add(bind_at("A", "one", 0x10));
    set.add(bind_at("B", "two", 0x20));
    set.add(bind_at("C", "three", 0x10));  // group at 0x10 forms, 0x20 shifts to index 0
    set.add(bind_at("D", "four", 0x20));   // must still find B::two at its new index

    CHECK(set.bindings.empty());
    REQUIRE(set.duplicates.size() == 2);
    CHECK(names(set.duplicates.at(0x10)) == Strings{"A::one", "C::three"});
    CHECK(names(set.duplicates.at(0x20)) == Strings{"B::two", "D::four"});
}

TEST_CASE("the same function, or another function of the same class, at one address is dropped") {
    BindingSet set;
    set.add(bind_at("A", "one", 0x10));
    set.add(bind_at("A", "one", 0x10));
    set.add(bind_at("A", "other", 0x10));
    CHECK(names(set.bindings) == Strings{"A::one"});
    CHECK(set.duplicates.empty());
}

TEST_CASE("append keeps free functions without merging them") {
    BindingSet set;
    set.append(bind_at("", "free_a", 0x10));
    set.append(bind_at("", "free_b", 0x10));
    CHECK(names(set.bindings) == Strings{"free_a", "free_b"});
    CHECK(set.duplicates.empty());
}

TEST_CASE("windows classes are told apart by the binary they live in") {
    struct Row {
        std::string_view cls;
        std::string_view binary;
        bool present;
    };
    for (auto const& row : {
             Row{"cocos2d::CCNode", "libcocos2d.dll", true},
             Row{"cocos2d::CCNode", "GeometryDash.exe", false},
             Row{"cocos2d::CCNode", "LIBCOCOS2D.DLL", true},
             Row{"cocos2d::CCLightning", "GeometryDash.exe", true},
             Row{"cocos2d::CCLightning", "libcocos2d.dll", false},
             Row{"PlayLayer", "GeometryDash.exe", true},
             Row{"PlayLayer", "libcocos2d.dll", false},
             Row{"DS_Dictionary", "libcocos2d.dll", true},
             Row{"cocos2d::extension::CCScale9Sprite", "libExtensions.dll", true},
             Row{"cocos2d::extension::CCScale9Sprite", "libcocos2d.dll", false},
             Row{"cocos2d::extension::CCScale9Sprite", "GeometryDash.exe", false},
             Row{"FMOD::System", "fmod.dll", true},
             Row{"PlayLayer", "fmod.dll", false},
         }) {
        CAPTURE(row.cls);
        CAPTURE(row.binary);
        CHECK(is_class_present(Platform::Windows, row.cls, row.binary) == row.present);
    }
    CHECK(is_class_present(Platform::MacArm, "cocos2d::CCNode", "GeometryJump"));
    CHECK(is_class_present(Platform::Android64, "FMOD::System", "libfmod.so"));
    CHECK_FALSE(is_class_present(Platform::Android64, "PlayLayer", "libfmod.so"));
    CHECK(is_class_present(Platform::Android64, "PlayLayer", "libcocos2dcpp.so"));
}

TEST_CASE("windows free functions are told apart by the binary and the file that declares them") {
    CHECK(is_function_present(Platform::Windows, "FMOD_System_Create", "fmod.dll", "Fmod.bro"));
    CHECK_FALSE(is_function_present(Platform::Windows, "kmMat4Fill", "fmod.dll", "Kazmath.bro"));
    CHECK(is_function_present(Platform::Windows, "kmMat4Fill", "libcocos2d.dll", "Kazmath.bro"));
    CHECK_FALSE(is_function_present(Platform::Windows, "other", "libcocos2d.dll", "Other.bro"));
    CHECK(is_function_present(Platform::Windows, "anything", "GeometryDash.exe", "Other.bro"));
    CHECK(is_function_present(Platform::Android64, "FMOD_System_Create", "libfmod.so", "Fmod.bro"));
    CHECK_FALSE(is_function_present(Platform::Android64, "other", "libfmod.so", "Other.bro"));
}

TEST_CASE("export_function_line writes constructors and destructors without a return type") {
    FunctionSignature ctor;
    ctor.class_name = "ns::Foo";
    ctor.name = "Foo";
    ctor.ret = TypeRef{"void"};
    CHECK(export_function_line(ctor, Bind{BindState::Bound, 0x10}, Platform::Windows) == "Foo() = win 0x10;");

    FunctionSignature dtor = ctor;
    dtor.name = "~Foo";
    CHECK(export_function_line(dtor, Bind{BindState::Bound, 0x20}, Platform::Windows) == "~Foo() = win 0x20;");

    FunctionSignature plain = ctor;
    plain.name = "run";
    plain.ret = TypeRef{"int"};
    plain.params = {Param{TypeRef{"float"}, "x", {}}};
    CHECK(export_function_line(plain, Bind{BindState::Bound, 0x30}, Platform::MacArm) == "int run(float x) = m1 0x30;");

    plain.ret = TypeRef{"TodoReturn"};
    CHECK(
        export_function_line(plain, Bind{BindState::Inlined, 0}, Platform::Windows) ==
        "TodoReturn run(float x) = win inline;");
}
