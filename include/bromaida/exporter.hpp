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

/// Decision hooks. The IDA plugin implements them with dialogs, the tests with canned answers.
class ExportPrompter {
public:
    virtual ~ExportPrompter() = default;

    /// Where a class that isn't in the template should be written. std::nullopt skips the class.
    [[nodiscard]] virtual std::optional<Path> choose_destination(
        std::string_view class_name, std::span<Path const> candidates, Path const& fallback) {
        (void)class_name, (void)candidates;
        return fallback;
    }

    /// The template already has a different address. True overwrites it.
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

/// Works out what every touched .bro file should look like. Nothing is written.
/// `template_root` may be null to export from scratch into `options.default_file`.
[[nodiscard]] geode::Result<ExportPlan, std::string> plan_export(
    ExtractedBinary const& binary, broma::Root const* template_root, ExportOptions const& options,
    ExportPrompter& prompter);

/// Several platforms in one go (e.g. android32 + android64): the plan of each is applied on top of the previous one.
[[nodiscard]] geode::Result<ExportPlan, std::string> plan_export(
    std::span<ExtractedBinary const> binaries, broma::Root const* template_root, ExportOptions options,
    ExportPrompter& prompter);

/// Writes the plan; each file goes to a temporary sibling first and is renamed over the original.
[[nodiscard]] geode::Result<void, std::string> apply_plan(ExportPlan const& plan);

/// One Broma declaration for a single function, for "export current function".
[[nodiscard]] std::string export_function_line(FunctionSignature const& sig, Bind bind, Platform platform);

}  // namespace bromaida
