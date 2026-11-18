#pragma once

#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>
#include <ast.hpp>

#include <bromaida/path.hpp>

namespace bromaida {
    /// Parses a .bro file (and everything it includes) into a Root AST.
    [[nodiscard]] geode::Result<broma::Root, std::string> load_broma(PathView entry);

    struct BromaFolder {
        broma::Root root;
        std::vector<Path> files;              ///< In parse order.
        bool used_entry = false;              ///< `Entry.bro` was found; it pulls in the rest through its includes.
        std::vector<std::string> duplicates;  ///< Classes defined in two different files; the first definition wins.
    };

    /// Loads the .bro files of a folder (not subfolders). With an `Entry.bro` only that file is loaded, since it includes the rest.
    [[nodiscard]] geode::Result<BromaFolder, std::string> load_broma_folder(PathView folder);

    /// Parses Broma source held in memory.
    [[nodiscard]] geode::Result<broma::Root, std::string> parse_broma(
        std::string_view source, PathView include_base = ".", std::string const& source_name = "<string>");

    /// Function declarations of a class, in order; works on const and mutable classes.
    template <class Class>
    [[nodiscard]] auto bind_fields(Class& cls) {
        namespace v = std::views;
        return cls.fields |
               v::transform([](auto& field) { return field.template get_as<broma::FunctionBindField>(); }) |
               v::filter([](auto* fn) { return fn != nullptr; }) | v::transform([](auto* fn) -> auto& { return *fn; });
    }

    /// `/// docs` lines followed by `[[link(...)]]`, `[[since(...)]]`, ... one per line.
    /// Pass the class attributes as `inherited` so the `link`/`missing`/`depends` Broma copies onto members aren't written back.
    [[nodiscard]] std::string write_attributes(
        broma::Attributes const& attrs, std::string_view indent = "", broma::Attributes const* inherited = nullptr,
        std::vector<broma::Superclass> const* superclasses = nullptr);

    /// Right-hand side of a binding: `win 0x1234, imac inline`. Empty when nothing is bound.
    [[nodiscard]] std::string write_binds(broma::PlatformNumber const& binds);

    /// One member function, attributes included, without indentation on the first line.
    [[nodiscard]] std::string write_function(
        broma::MemberFunctionProto const& proto, broma::PlatformNumber const& binds, std::string_view body = {},
        std::string_view indent = "    ", broma::Attributes const* inherited = nullptr);
    [[nodiscard]] std::string write_free_function(broma::Function const& function);
    [[nodiscard]] std::string write_member(broma::MemberField const& member, std::string_view indent = "    ");
    [[nodiscard]] std::string write_pad(broma::PadField const& pad, std::string_view indent = "    ");
    [[nodiscard]] std::string write_field(
        broma::Field const& field, std::string_view indent = "    ", broma::Attributes const* inherited = nullptr);
    [[nodiscard]] std::string write_class(broma::Class const& cls);
    /// Header only: attributes and `class Name : Bases {`.
    [[nodiscard]] std::string write_class_header(broma::Class const& cls);

    // Edits go into the original text so comments and formatting survive.

    /// Location of one class declaration (attributes included) inside a file's text.
    struct ClassSpan {
        std::size_t begin = 0;       ///< First attribute/doc line.
        std::size_t header_end = 0;  ///< One past the opening `{`.
        std::size_t body_end = 0;    ///< Position of the closing `}`.
        std::size_t end = 0;         ///< One past the closing `}`.
    };

    /// One declaration inside a class body.
    struct StatementSpan {
        std::size_t begin = 0;  ///< Start of the declaration, comments before it excluded.
        std::size_t end = 0;    ///< One past its `;` or closing `}`.
        std::string text;
    };

    [[nodiscard]] std::optional<ClassSpan> find_class(std::string_view text, std::string_view class_name);
    [[nodiscard]] std::vector<StatementSpan> split_statements(std::string_view text, ClassSpan const& span);

    struct ClassPatch {
        /// Replaces the statement at the given index (into split_statements).
        struct Replace {
            std::size_t statement;
            std::string text;
        };
        std::vector<Replace> replacements;
        std::vector<std::string> additions;     ///< Appended after the last statement, one per entry.
        std::optional<std::string> new_header;  ///< Replaces everything before the opening `{` (attributes included).
    };

    /// Applies the patch to the class named `class_name`. Returns std::nullopt if it isn't in `text`.
    [[nodiscard]] std::optional<std::string> patch_class(
        std::string_view text, std::string_view class_name, ClassPatch const& patch);
}  // namespace bromaida
