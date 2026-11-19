#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <bromaida/broma/broma_io.hpp>
#include <bromaida/exporter/exporter.hpp>
#include <bromaida/exporter/symbols.hpp>
#include <bromaida/util/path.hpp>

namespace bromaida {
    struct ParsedStatement {
        std::size_t index = 0;
        std::string key;
        broma::FunctionBindField field;
    };

    class Planner {
    public:
        Planner(broma::Root const* root, ExportOptions const& options, ExportPrompter& prompter, ExportPlan& plan);
        void run(ExtractedBinary const& binary);

    private:
        std::string* text_of(Path const& file);
        void commit(Path const& file);
        void handle_class(ExtractedClass const& cls, Platform platform);
        void create_class(ExtractedClass const& cls, Platform platform);
        broma::FunctionBindField new_field(ExtractedMethod const& m, bool linked, Platform platform);
        bool links_by_symbol(ExtractedClass const& cls, Platform platform) const;
        std::string return_type_for(FunctionSignature const& sig) const;
        void update_class(ExtractedClass const& cls, Path const& file, Platform platform);
        broma::Class const* find_in_root(std::string_view name) const;
        void warn_if_virtual_order_differs(ExtractedClass const& cls, std::vector<ParsedStatement> const& statements);
        void handle_free_functions(ExtractedBinary const& binary);

        broma::Root const* m_root;
        ExportOptions const& m_options;
        ExportPrompter& m_prompter;
        ExportPlan& m_plan;

        std::map<Path, std::string> m_texts;
        std::set<Path> m_unreadable;
        std::set<Path> m_dirty;
        std::unordered_map<std::string, Path> m_class_file;
    };
}  // namespace bromaida
