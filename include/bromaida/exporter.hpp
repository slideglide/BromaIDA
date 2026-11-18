#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Geode/Result.hpp>
#include <ast.hpp>

#include <bromaida/path.hpp>
#include <bromaida/platform.hpp>
#include <bromaida/symbols.hpp>

namespace bromaida {
    /// Things the exporter never writes.
    struct IgnoreRules {
        std::vector<std::string> namespaces;  ///< `cocos2d` ignores everything inside `cocos2d::`.
        std::vector<std::string> classes;     ///< Exact qualified class names.

        [[nodiscard]] bool matches(std::string_view class_name) const;
    };

    struct ExportOptions {
        Platform platform = Platform::Windows;
        bool include_return_types = false;   ///< Replace `TodoReturn` with what the binary knows.
        bool include_param_names = false;    ///< Replace generated `pN` names with the binary's.
        bool only_template_classes = false;  ///< Skip classes that aren't in the template.
        bool mark_link = true;  ///< Android: link classes by symbol (`[[link(android)]]`) instead of writing addresses.
        IgnoreRules ignore;
        /// New classes go here unless the prompter picks another file.
        Path default_file;
    };

    /// Decision hooks: dialogs in the plugin, canned answers in the tests.
    class ExportPrompter {
    public:
        virtual ~ExportPrompter() = default;

        /// Picks the file for a class that isn't in the template.
        /// @param candidates The files of the template.
        /// @param fallback Where the class goes by default.
        /// @return The file to write to, or nothing to skip the class.
        [[nodiscard]] virtual std::optional<Path> choose_destination(
            std::string_view class_name, std::span<Path const> candidates, Path const& fallback) {
            (void)class_name, (void)candidates;
            return fallback;
        }

        /// Called when the template already has a different address.
        /// @param existing The address in the template.
        /// @param found The address in the binary.
        /// @return True to overwrite the existing address.
        [[nodiscard]] virtual bool overwrite_address(
            std::string_view qualified_name, std::string_view existing, std::string_view found) {
            (void)qualified_name, (void)existing, (void)found;
            return false;
        }
    };

    struct ExportStats {
        std::size_t bindings_added = 0;    ///< Addresses written to declarations that existed.
        std::size_t bindings_updated = 0;  ///< Existing addresses overwritten.
        std::size_t bindings_created = 0;  ///< Addresses written into methods that were created.
        std::size_t methods_created = 0;
        std::size_t classes_created = 0;
        std::size_t classes_skipped = 0;
        std::size_t return_types = 0;
        std::size_t param_names = 0;
        std::size_t mismatches_kept = 0;
        std::vector<std::string> warnings;
    };

    struct ExportPlan {
        /// Final text of every file that changes, keyed by path.
        std::map<Path, std::string> files;
        ExportStats stats;
    };

    /// Computes the new text of every touched .bro file. Writes nothing.
    /// @param binary Symbols and classes read from the binary.
    /// @param template_root Existing bindings to merge into. Null exports from scratch into `options.default_file`.
    /// @param options What to export.
    /// @param prompter Answers the questions that come up along the way.
    /// @return The new file contents and statistics, or an error message.
    [[nodiscard]] geode::Result<ExportPlan, std::string> plan_export(
        ExtractedBinary const& binary, broma::Root const* template_root, ExportOptions const& options,
        ExportPrompter& prompter);

    /// Same as above for several platforms at once (e.g. android32 + android64). Each platform's plan builds on the
    /// previous one.
    /// @param binaries One entry per platform.
    [[nodiscard]] geode::Result<ExportPlan, std::string> plan_export(
        std::span<ExtractedBinary const> binaries, broma::Root const* template_root, ExportOptions options,
        ExportPrompter& prompter);

    /// Writes the plan to disk.
    /// @param plan A plan from plan_export.
    /// @return Nothing, or an error message.
    [[nodiscard]] geode::Result<void, std::string> apply_plan(ExportPlan const& plan);

    /// Writes one Broma declaration, for "export current function".
    /// @param sig The function.
    /// @param bind Its address on `platform`.
    /// @param platform The platform the address belongs to.
    /// @return The declaration line.
    [[nodiscard]] std::string export_function_line(FunctionSignature const& sig, Bind bind, Platform platform);
}  // namespace bromaida
