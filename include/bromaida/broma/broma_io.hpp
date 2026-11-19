#pragma once

#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>
#include <ast.hpp>

#include <bromaida/util/path.hpp>

namespace bromaida {

    [[nodiscard]] geode::Result<broma::Root, std::string> load_broma(PathView entry);

    struct BromaFolder {
        broma::Root root;
        std::vector<Path> files;
        bool used_entry = false;
        std::vector<std::string> duplicates;
    };

    [[nodiscard]] geode::Result<BromaFolder, std::string> load_broma_folder(PathView folder);

    [[nodiscard]] geode::Result<broma::Root, std::string> parse_broma(
        std::string_view source, PathView include_base = ".", std::string const& source_name = "<string>");

    template <class Class>
    [[nodiscard]] auto bind_fields(Class& cls) {
        namespace v = std::views;
        return cls.fields |
               v::transform([](auto& field) { return field.template get_as<broma::FunctionBindField>(); }) |
               v::filter([](auto* fn) { return fn != nullptr; }) | v::transform([](auto* fn) -> auto& { return *fn; });
    }

    [[nodiscard]] std::string write_attributes(
        broma::Attributes const& attrs, std::string_view indent = "", broma::Attributes const* inherited = nullptr,
        std::vector<broma::Superclass> const* superclasses = nullptr);

    [[nodiscard]] std::string write_binds(broma::PlatformNumber const& binds);

    [[nodiscard]] std::string write_function(
        broma::MemberFunctionProto const& proto, broma::PlatformNumber const& binds, std::string_view body = {},
        std::string_view indent = "    ", broma::Attributes const* inherited = nullptr);
    [[nodiscard]] std::string write_free_function(broma::Function const& function);
    [[nodiscard]] std::string write_member(broma::MemberField const& member, std::string_view indent = "    ");
    [[nodiscard]] std::string write_pad(broma::PadField const& pad, std::string_view indent = "    ");
    [[nodiscard]] std::string write_field(
        broma::Field const& field, std::string_view indent = "    ", broma::Attributes const* inherited = nullptr);
    [[nodiscard]] std::string write_class(broma::Class const& cls);

    [[nodiscard]] std::string write_class_header(broma::Class const& cls);

    struct ClassSpan {
        std::size_t begin = 0;
        std::size_t header_end = 0;
        std::size_t body_end = 0;
        std::size_t end = 0;
    };

    struct StatementSpan {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::string text;
    };

    [[nodiscard]] std::optional<ClassSpan> find_class(std::string_view text, std::string_view class_name);
    [[nodiscard]] std::vector<StatementSpan> split_statements(std::string_view text, ClassSpan const& span);

    struct ClassPatch {

        struct Replace {
            std::size_t statement;
            std::string text;
        };
        std::vector<Replace> replacements;
        std::vector<std::string> additions;
        std::optional<std::string> new_header;
    };

    [[nodiscard]] std::optional<std::string> patch_class(
        std::string_view text, std::string_view class_name, ClassPatch const& patch);
}
