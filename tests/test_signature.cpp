#include <doctest/doctest.h>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/broma/signature.hpp>

#include "common.hpp"

using namespace bromaida;

static constexpr std::string_view kBroma = R"(
class Foo {
    Foo() = win 0x1;
    ~Foo() = win 0x2;
    static Foo* create(int count) = win 0x10, m1 inline;
    virtual bool init(int a, float b) const = win 0x20;
    void tick(float dt) = win 0x30;
    void tick(int steps) = win 0x31;
    void log(char const* fmt, ...) = win 0x40;
    std::vector<Foo*> list(std::map<int, float>& m) = win 0x50;
    [[missing(win)]] void gone();
}
)";

static Binding binding_of(broma::Root const& root, std::size_t index, Platform platform) {
    auto fns = bind_fields(root.classes.front());
    auto it = std::ranges::begin(fns);
    for (std::size_t i = 0; i < index; ++i) {
        REQUIRE(it != std::ranges::end(fns));
        ++it;
    }
    REQUIRE(it != std::ranges::end(fns));
    return Binding::from_field(root.classes.front().name, *it, platform);
}

static broma::Root parsed_root() {
    auto result = parse_broma(kBroma);
    REQUIRE(result.isOk());
    return std::move(result).unwrap();
}

TEST_CASE("Bind round-trips Broma's raw offsets") {
    CHECK(Bind::from_broma(-1) == Bind{});
    CHECK(Bind::from_broma(-2) == Bind{BindState::Inlined, 0});
    CHECK(Bind::from_broma(-3) == Bind{});
    CHECK(Bind::from_broma(0) == Bind{BindState::Bound, 0});
    CHECK(Bind::from_broma(0x1234) == Bind{BindState::Bound, 0x1234});

    CHECK(Bind{}.to_broma() == -1);
    CHECK(Bind{BindState::Inlined, 0}.to_broma() == -2);
    CHECK(Bind{BindState::Bound, 0x1234}.to_broma() == 0x1234);

    CHECK(Bind{BindState::Bound, 0x1234}.to_string() == "0x1234");
    CHECK(Bind{BindState::Inlined, 0}.to_string() == "inline");
    CHECK(Bind{}.to_string() == "unbound");
    CHECK(Bind{BindState::Bound, 0}.has_address());
    CHECK_FALSE(Bind{BindState::Inlined, 0}.has_address());
    CHECK_FALSE(Bind{}.has_address());
}

TEST_CASE("TypeRef normalizes on construction and knows its decorations") {
    TypeRef plain{"Foo const &"};
    CHECK(plain.str() == "const Foo&");
    CHECK(plain.stripped() == "Foo");
    CHECK(plain.is_const());
    CHECK(plain.is_ref());
    CHECK_FALSE(plain.is_ptr());
    CHECK(plain.is_ptr_or_ref());

    TypeRef ptr{"Foo**"};
    CHECK(ptr.is_ptr());
    CHECK_FALSE(ptr.is_ref());
    CHECK_FALSE(ptr.is_const());

    TypeRef value{"Foo"};
    CHECK_FALSE(value.is_ptr_or_ref());
    CHECK(TypeRef{"gd::string"}.is_stl());
    CHECK(TypeRef{"gd::string"}.str() == "std::string");
    CHECK_FALSE(TypeRef{"Foo"}.is_stl());
    CHECK(TypeRef{"Foo*"} == TypeRef{"Foo *"});
    CHECK(TypeRef{}.empty());
}

TEST_CASE("TypeRef::expanded writes the libc++ form of STL types and leaves the rest alone") {
    CHECK(TypeRef{"std::vector<int>"}.expanded() == "std::vector<int, std::allocator<int>>");
    CHECK(TypeRef{"std::vector<Foo*>"}.expanded() == "std::vector<Foo *, std::allocator<Foo *>>");
    CHECK(TypeRef{"Foo*"}.expanded() == "Foo*");
}

TEST_CASE("unknown return types are TodoReturn, auto and nothing at all") {
    CHECK(TypeRef{"TodoReturn"}.is_unknown_return());
    CHECK(TypeRef{"auto"}.is_unknown_return());
    CHECK(TypeRef{""}.is_unknown_return());
    CHECK_FALSE(TypeRef{"void"}.is_unknown_return());
    CHECK_FALSE(TypeRef{"TodoReturnX"}.is_unknown_return());
    CHECK_FALSE(TypeRef{"std::string"}.is_unknown_return());
}

TEST_CASE("constructors and destructors take their IDA return type from the class") {
    auto root = parsed_root();
    auto ctor = binding_of(root, 0, Platform::Windows);
    CHECK(ctor.is_ctor);
    CHECK(ctor.name == "Foo");
    CHECK(ctor.ida_return_type().str() == "Foo*");
    CHECK(ctor.declaration() == "Foo();");
    CHECK(ctor.ida_qualified_name() == "Foo::Foo");

    auto dtor = binding_of(root, 1, Platform::Windows);
    CHECK(dtor.is_dtor);
    CHECK(dtor.ida_return_type().str() == "void");
    CHECK(dtor.declaration() == "~Foo();");
    CHECK(dtor.ida_qualified_name() == "Foo::~Foo");
    CHECK(dtor.ida_qualified_name(false) == "Foo::dFoo");
}

TEST_CASE("bindings read the address of the requested platform only") {
    auto root = parsed_root();
    auto win = binding_of(root, 2, Platform::Windows);
    CHECK(win.bind == Bind{BindState::Bound, 0x10});
    CHECK(win.has_address());
    CHECK_FALSE(win.is_inline);

    auto arm = binding_of(root, 2, Platform::MacArm);
    CHECK(arm.bind == Bind{BindState::Inlined, 0});
    CHECK(arm.is_inline);
    CHECK_FALSE(arm.has_address());

    auto droid = binding_of(root, 2, Platform::Android64);
    CHECK(droid.bind == Bind{});
    CHECK_FALSE(droid.is_inline);
    CHECK(win.short_info() == "Foo::create @ 0x10");
    CHECK(droid.short_info() == "Foo::create @ unbound");
}

TEST_CASE("declaration and IDA declaration spell static, virtual, const and the return type") {
    auto root = parsed_root();
    auto create = binding_of(root, 2, Platform::Windows);
    CHECK(create.is_static);
    CHECK(create.declaration() == "static Foo* create(int count);");
    CHECK(create.ida_declaration() == "static Foo* Foo::create(int count);");

    auto init = binding_of(root, 3, Platform::Windows);
    CHECK(init.is_virtual);
    CHECK(init.is_const);
    CHECK(init.declaration() == "virtual bool init(int a, float b) const;");
    CHECK(init.ida_declaration() == "virtual bool Foo::init(int a, float b);");
}

TEST_CASE("overloads keep distinct keys while constness does not split them") {
    auto root = parsed_root();
    auto by_float = binding_of(root, 4, Platform::Windows);
    auto by_int = binding_of(root, 5, Platform::Windows);
    CHECK(by_float.overload_key() == "tick(float)");
    CHECK(by_int.overload_key() == "tick(int)");
    CHECK_FALSE(by_float.same_overload(by_int));

    auto const_copy = by_float;
    const_copy.is_const = true;
    CHECK(const_copy.overload_key() == by_float.overload_key());
    CHECK(const_copy.same_overload(by_float));
    CHECK(binding_of(root, 4, Platform::Windows).same_overload(by_float));
}

TEST_CASE("args_string handles variadics, explicit this and parameter names") {
    auto root = parsed_root();
    auto log = binding_of(root, 6, Platform::Windows);
    CHECK(log.is_variadic);
    CHECK(log.args_string() == "const char* fmt, ...");
    CHECK(log.args_string(false, false) == "const char*, ...");
    CHECK(log.args_string(true) == "Foo* this, const char* fmt, ...");
    CHECK(log.args_string(true, false) == "Foo*, const char*, ...");
    CHECK(log.params.size() == 1);

    auto create = binding_of(root, 2, Platform::Windows);
    CHECK(create.args_string(true) == "int count");  // static: no `this`

    auto list = binding_of(root, 7, Platform::Windows);
    CHECK(list.ret.str() == "std::vector<Foo*>");
    CHECK(list.args_string() == "std::map<int, float>& m");
}

TEST_CASE("an explicit this parameter is not added a second time") {
    FunctionSignature sig;
    sig.class_name = "Foo";
    sig.name = "run";
    sig.params = {Param{TypeRef{"Foo*"}, "this", {}}, Param{TypeRef{"int"}, "x", {}}};
    CHECK(sig.args_string(true) == "Foo* this, int x");
    CHECK(sig.args_string(false) == "int x");
}

TEST_CASE("a function marked missing on the target platform says so") {
    auto root = parsed_root();
    auto fns = bind_fields(root.classes.front());
    auto last = Binding{};
    for (auto const& fn : fns)
        last = Binding::from_field("Foo", fn, Platform::Windows);
    CHECK(last.name == "gone");
    CHECK(last.is_missing);

    auto mac = Binding{};
    for (auto const& fn : bind_fields(root.classes.front()))
        mac = Binding::from_field("Foo", fn, Platform::MacArm);
    CHECK(mac.name == "gone");
    CHECK_FALSE(mac.is_missing);
}
