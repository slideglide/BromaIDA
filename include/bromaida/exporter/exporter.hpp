#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <asp/detail/Function.hpp>
#include <Geode/Result.hpp>
#include <ast.hpp>

#include <bromaida/util/path.hpp>
#include <bromaida/platform/platform.hpp>
#include <bromaida/exporter/symbols.hpp>

namespace bromaida {

    struct IgnoreRules {
        std::vector<std::string> namespaces;
        std::vector<std::string> classes;

        [[nodiscard]] bool matches(std::string_view class_name) const;
    };

    struct ExportOptions {
        Platform platform = Platform::Windows;
        bool include_return_types = false;
        bool include_param_names = false;
        bool only_template_classes = false;
        bool mark_link = true;
        IgnoreRules ignore;

        Path default_file;
    };

    struct ExportPrompter {

        asp::MoveOnlyFunction<bool(std::string_view qualified_name, std::string_view existing, std::string_view found)>
            overwrite_address;
    };

    struct ExportStats {
        std::size_t bindings_added = 0;
        std::size_t bindings_updated = 0;
        std::size_t bindings_created = 0;
        std::size_t methods_created = 0;
        std::size_t classes_created = 0;
        std::size_t classes_skipped = 0;
        std::size_t return_types = 0;
        std::size_t param_names = 0;
        std::size_t mismatches_kept = 0;
        std::vector<std::string> warnings;
    };

    struct ExportPlan {

        std::map<Path, std::string> files;
        ExportStats stats;
    };

    [[nodiscard]] geode::Result<ExportPlan, std::string> plan_export(
        ExtractedBinary const& binary, broma::Root const* template_root, ExportOptions const& options,
        ExportPrompter& prompter);

    [[nodiscard]] geode::Result<ExportPlan, std::string> plan_export(
        std::span<ExtractedBinary const> binaries, broma::Root const* template_root, ExportOptions const& options,
        ExportPrompter& prompter);

    [[nodiscard]] geode::Result<void, std::string> apply_plan(ExportPlan const& plan);

    [[nodiscard]] std::string export_function_line(FunctionSignature const& sig, Bind bind, Platform platform);
}
