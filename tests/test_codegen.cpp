#include <doctest/doctest.h>

#include <bromaida/broma_io.hpp>
#include <bromaida/class_graph.hpp>
#include <bromaida/codegen.hpp>

using namespace bromaida;

static constexpr std::string_view kBroma = R"(
class Child : Base {
    virtual void tick(float dt) = win 0x1;
    Holder m_holder;
    std::vector<Base*> m_list;
    PAD = win 0x8;
}

class Base {
    virtual void tick(float dt) = win 0x2;
    virtual void tick(int dt) = win 0x3;
}

class Holder {
    int m_x;
}
)";

TEST_CASE("class graph orders by hard dependencies") {
    auto root = parse_broma(kBroma).unwrap();
    std::vector<broma::Class const*> classes;
    for (auto const& c : root.classes)
        classes.push_back(&c);
    ClassGraph graph{classes, Platform::Windows};

    auto const& order = graph.class_order();
    auto pos = [&](std::string_view n) { return std::ranges::find(order, n) - order.begin(); };
    CHECK(pos("Base") < pos("Child"));
    CHECK(pos("Holder") < pos("Child"));

    CHECK(graph.is_override("Child", graph.own_virtuals("Child")[0]));
    CHECK_FALSE(graph.stl_stubs().by_pointer.empty());
}

TEST_CASE("build_class emits virtuals first and padding") {
    auto root = parse_broma(kBroma).unwrap();
    std::vector<broma::Class const*> classes;
    for (auto const& c : root.classes)
        classes.push_back(&c);
    ClassGraph graph{classes, Platform::Windows};

    auto child = std::ranges::find(root.classes, "Child", &broma::Class::name);
    REQUIRE(child != root.classes.end());
    auto text = build_class(*child, graph, Platform::Windows);
    CHECK(text.contains("class Child : public Base {"));
    CHECK(text.find("virtual void tick(float dt);") < text.find("Holder m_holder;"));
    CHECK(text.contains("PAD(0x8);"));
}
