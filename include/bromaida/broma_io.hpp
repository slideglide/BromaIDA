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
    /// Parses a .bro file and everything it includes.
    /// @param entry The .bro file to start from.
    /// @return The parsed AST, or an error message.
    [[nodiscard]] geode::Result<broma::Root, std::string> load_broma(PathView entry);

    struct BromaFolder {
        broma::Root root;
        std::vector<Path> files;              ///< In parse order.
        bool used_entry = false;              ///< `Entry.bro` was found; it pulls in the rest through its includes.
        std::vector<std::string> duplicates;  ///< Classes defined in two different files; the first definition wins.
    };

    /// Loads the .bro files of a folder. If `Entry.bro` exists, only it is loaded; it includes the rest.
    /// @param folder Folder holding the .bro files. Subfolders are not searched.
    /// @return The merged AST plus what was loaded, or an error message.
    [[nodiscard]] geode::Result<BromaFolder, std::string> load_broma_folder(PathView folder);

    /// Parses Broma source held in memory.
    /// @param source The Broma text.
    /// @param include_base Folder that relative includes resolve against.
    /// @param source_name Name to use in error messages.
    /// @return The parsed AST, or an error message.
    [[nodiscard]] geode::Result<broma::Root, std::string> parse_broma(
        std::string_view source, PathView include_base = ".", std::string const& source_name = "<string>");

    /// @param cls A class, const or mutable.
    /// @return A view over its function declarations, in order.
    template <class Class>
    [[nodiscard]] auto bind_fields(Class& cls) {
        namespace v = std::views;
        return cls.fields |
               v::transform([](auto& field) { return field.template get_as<broma::FunctionBindField>(); }) |
               v::filter([](auto* fn) { return fn != nullptr; }) | v::transform([](auto* fn) -> auto& { return *fn; });
    }

    /// Writes a declaration's `/// docs` lines followed by `[[link(...)]]`, `[[since(...)]]`, ... one per line.
    /// @param attrs The attributes to write.
    /// @param indent Prefix for every line.
    /// @param inherited The class's attributes. Broma copies `link`, `missing` and `depends` from the class onto its
    ///        members, and passing them here keeps those copies from being written back.
    /// @return The text, or an empty string if there is nothing to write.
    [[nodiscard]] std::string write_attributes(
        broma::Attributes const& attrs, std::string_view indent = "", broma::Attributes const* inherited = nullptr,
        std::vector<broma::Superclass> const* superclasses = nullptr);

    /// Writes the right-hand side of a binding, such as `win 0x1234, imac inline`.
    /// @param binds Per-platform addresses.
    /// @return The text, or an empty string when nothing is bound.
    [[nodiscard]] std::string write_binds(broma::PlatformNumber const& binds);

    /// Writes one member function with its attributes.
    /// @param proto The function's declaration.
    /// @param binds Its addresses.
    /// @param body Inline body such as `{ return 1; }`. When empty the declaration ends with `;`.
    /// @param indent Prefix for every line.
    /// @param inherited See write_attributes.
    /// @return The text.
    [[nodiscard]] std::string write_function(
        broma::MemberFunctionProto const& proto, broma::PlatformNumber const& binds, std::string_view body = {},
        std::string_view indent = "    ", broma::Attributes const* inherited = nullptr);
    [[nodiscard]] std::string write_free_function(broma::Function const& function);
    [[nodiscard]] std::string write_member(broma::MemberField const& member, std::string_view indent = "    ");
    [[nodiscard]] std::string write_pad(broma::PadField const& pad, std::string_view indent = "    ");
    [[nodiscard]] std::string write_field(
        broma::Field const& field, std::string_view indent = "    ", broma::Attributes const* inherited = nullptr);
    [[nodiscard]] std::string write_class(broma::Class const& cls);
    /// Writes only the header: attributes and `class Name : Bases {`.
    [[nodiscard]] std::string write_class_header(broma::Class const& cls);

    // Edits patch the original text, which keeps comments and formatting

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
        /// Replaces the statement at `statement`, an index into split_statements.
        struct Replace {
            std::size_t statement;
            std::string text;
        };
        std::vector<Replace> replacements;
        std::vector<std::string> additions;     ///< Appended after the last statement, one per entry.
        std::optional<std::string> new_header;  ///< Replaces everything before the opening `{` (attributes included).
    };

    /// Applies a patch to one class in a file's text.
    /// @param text The file's contents.
    /// @param class_name The class to patch.
    /// @param patch What to change.
    /// @return The new text, or nothing if `class_name` isn't in `text`.
    [[nodiscard]] std::optional<std::string> patch_class(
        std::string_view text, std::string_view class_name, ClassPatch const& patch);
}  // namespace bromaida
