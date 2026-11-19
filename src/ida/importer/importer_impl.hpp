#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <bromaida/broma/bindings.hpp>
#include <bromaida/broma/broma_io.hpp>
#include <bromaida/config/settings.hpp>
#include <bromaida/platform/binary_info.hpp>
#include <bromaida/typegen/bundle.hpp>
#include <bromaida/typegen/class_graph.hpp>
#include <bromaida/typegen/profile.hpp>
#include <bromaida/util/path.hpp>

#include "importer/ida_types.hpp"
#include "importer/importer.hpp"

#include <pro.h>
#include <funcs.hpp>

namespace bromaida::ida {
    class Importer {
    public:
        Importer(
            Settings& settings, BinaryInfo const& binary, PathView source, PathView data_dir,
            std::span<Profile const> profiles);

        geode::Result<ImportSummary, std::string> run();

    private:
        struct TypeEnvironment {
            Profile profile;
            Bundle bundle;
            Flavor flavor;
        };

        geode::Result<void, std::string> load_sources();
        void collect_classes();
        void collect_bindings();

        geode::Result<std::optional<TypeEnvironment>, std::string> resolve_environment(ClassGraph const& graph);
        void import_types(ClassGraph const& graph);
        bool run_parser(PathView types_file, StlStubs const& stubs);

        void apply_bindings();
        func_t* function_for(Binding const& binding, ea_t ea);
        bool apply_one(Binding const& binding);
        void fix_signature(ea_t ea, Binding const& binding, std::string_view what = {});

        static std::string merged_names(std::vector<Binding> const& group);
        bool apply_duplicates(std::uint64_t address, std::vector<Binding> const& group);
        void write_merge_comment(ea_t ea, std::uint64_t address, std::vector<Binding> const& group);

        void apply_linked_bindings();
        static std::unordered_map<std::string, std::vector<ea_t>> build_symbol_index();

        Settings& m_settings;
        BinaryInfo m_binary;
        Platform m_platform;
        TypeContext m_types;

        Path m_source;
        Path m_broma_dir;
        Path m_data_dir;
        std::vector<Profile> m_profiles;
        broma::Root m_root;
        std::size_t m_file_count = 0;

        std::vector<broma::Class const*> m_class_list;
        std::vector<std::string> m_unavailable;
        BindingSet m_set;
        std::vector<Binding> m_linked;

        std::string m_parser;
        Path m_stl_dir;
        bool m_has_types = false;

        std::optional<bool> m_rename_answer;

        ImportSummary m_summary;
    };
}  // namespace bromaida::ida
