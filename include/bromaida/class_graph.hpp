#pragma once

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ast.hpp>

#include <bromaida/platform.hpp>
#include <bromaida/signature.hpp>

namespace bromaida {

/// `class __BromaSTLTypesPtr { public: std::vector<Foo*> m_0; ... }`
/// Gives IDA a place to instantiate the STL types Broma mentions.
struct StlStub {
    struct Member {
        std::string type;
        std::string name;
    };

    std::string class_name;
    std::vector<Member> members;

    [[nodiscard]] std::string emit() const;
    [[nodiscard]] bool empty() const noexcept { return members.empty(); }
};

struct StlStubs {
    StlStub by_pointer{"__BromaSTLTypesPtr", {}};
    StlStub by_value{"__BromaSTLTypesValue", {}};
};

/// Inheritance, virtual and type-reference topology of every Broma class.
class ClassGraph {
public:
    using ClassMap = std::unordered_map<std::string, broma::Class const*>;

    /// `classes` must outlive the graph. Order of `classes` is the emission tiebreaker.
    ClassGraph(std::vector<broma::Class const*> classes, Platform target);

    [[nodiscard]] std::vector<std::string> const& class_order() const noexcept { return m_order; }
    [[nodiscard]] broma::Class const* find(std::string_view name) const;

    [[nodiscard]] std::vector<std::string> const& forward_declarations() const noexcept { return m_forward_decls; }
    [[nodiscard]] StlStubs const& stl_stubs() const noexcept { return m_stubs; }

    /// Every `std::` type the classes use anywhere, without template arguments (`std::vector`, `std::mutex`).
    [[nodiscard]] std::set<std::string> used_std_types() const;

    /// Every non-`std::` type name the classes refer to: bases, members, parameters and return types, including
    /// the arguments of templates (`Foo` in `std::vector<Foo*>`). Primitive types are left out.
    [[nodiscard]] std::set<std::string> referenced_types() const;

    /// Virtuals declared by the class itself (not inherited), in declaration order.
    [[nodiscard]] std::vector<FunctionSignature> const& own_virtuals(std::string_view cls) const;

    /// True if `sig` overrides a virtual of any base.
    [[nodiscard]] bool is_override(std::string_view cls, FunctionSignature const& sig) const;
    /// True if `sig` overrides a virtual that comes from a non-primary base.
    [[nodiscard]] bool is_secondary_override(broma::Class const& cls, FunctionSignature const& sig) const;

    struct BrokenOverride {
        FunctionSignature own;
        std::vector<FunctionSignature> conflicting;
    };
    /// Virtuals sharing a name with a base virtual without matching its signature.
    [[nodiscard]] std::vector<BrokenOverride> broken_overrides(std::string_view cls) const;

    /// Bases that apply to the target platform.
    [[nodiscard]] std::vector<std::string> bases_of(broma::Class const& cls) const;

private:
    struct TypeUse {
        std::string bare;
        bool by_value;
        bool from_function;
    };
    struct Info {
        std::vector<FunctionSignature> own_virtuals;
        std::unordered_set<std::string> own_keys;
        std::unordered_set<std::string> inherited_keys;
        std::unordered_map<std::string, std::vector<FunctionSignature>> inherited_by_name;
        std::unordered_map<std::string, std::vector<TypeUse>> type_uses;  // keyed by normalized type
        std::vector<std::string> type_use_order;
    };

    void build_info(std::string const& name, broma::Class const& cls);
    void collect_inherited(std::string const& name, std::unordered_set<std::string>& visiting);
    void compute_stl();
    void compute_order();
    void compute_forward_declarations();
    [[nodiscard]] std::vector<std::string> hard_dependencies(std::string const& name) const;

    Platform m_target;
    std::vector<std::string> m_names;
    ClassMap m_classes;
    std::unordered_map<std::string, Info> m_info;
    std::unordered_set<std::string> m_namespace_prefixes;
    std::unordered_set<std::string> m_inherited_done;

    std::vector<std::string> m_order;
    std::vector<std::string> m_forward_decls;
    std::unordered_set<std::string> m_stl_forward;
    StlStubs m_stubs;
};

}  // namespace bromaida
