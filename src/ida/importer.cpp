#include "importer.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <ranges>
#include <regex>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <unordered_map>

#include <fmt/format.h>

#include <bromaida/broma_io.hpp>
#include <bromaida/bundle.hpp>
#include <bromaida/class_graph.hpp>
#include <bromaida/codegen.hpp>
#include <bromaida/digest.hpp>
#include <bromaida/log.hpp>
#include <bromaida/profile.hpp>
#include <bromaida/signature.hpp>
#include <bromaida/version.hpp>

#include "ida_env.hpp"
#include "ida_state.hpp"
#include "ida_types.hpp"
#include "ida_ui.hpp"

#include <pro.h>
#include <bytes.hpp>
#include <dirtree.hpp>
#include <funcs.hpp>
#include <ida.hpp>
#include <kernwin.hpp>
#include <name.hpp>
#include <nalt.hpp>
#include <srclang.hpp>
#include <typeinf.hpp>

namespace bromaida::ida {

static constexpr Log log{"Importer"};
static constexpr auto kTypesDir = "/BromaIDA";

// ---- which binary a class lives in (GD ships cocos and fmod as separate DLLs on Windows) ---------

static std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool is_fmod_class(std::string_view name) { return name.starts_with("FMOD::") || name == "FMOD"; }

static bool is_class_present(Platform platform, std::string_view class_name, std::string_view binary_file) {
    auto const binary = lower(std::string(binary_file));

    if (platform == Platform::Windows) {
        // taken from Geode's bindings codegen as of GD 2.2082
        bool const is_cocos = class_name.starts_with("cocos2d::") || class_name == "cocos2d" ||
                              class_name.starts_with("pugi::") || class_name == "pugi" ||
                              class_name == "DS_Dictionary" || class_name == "ObjectDecoder" ||
                              class_name == "ObjectDecoderDelegate" || class_name == "CCContentManager";
        bool const is_cocos_ext = class_name.starts_with("cocos2d::extension");
        bool const is_exception =
            class_name == "cocos2d::CCLightning";  // custom RobTop class, lives in GeometryDash.exe

        if (binary.starts_with("libcocos2d")) return is_cocos && !is_cocos_ext && !is_exception;
        if (binary.starts_with("libextensions")) return is_cocos_ext;
        if (binary.starts_with("fmod")) return is_fmod_class(class_name);
        return !is_cocos || is_exception;
    }
    if (is_android(platform) && binary.starts_with("libfmod")) return is_fmod_class(class_name);
    return true;
}

static bool is_function_present(
    Platform platform, std::string_view name, std::string_view binary_file, std::string_view source_file) {
    auto const binary = lower(std::string(binary_file));
    bool const is_fmod = name.starts_with("FMOD_");

    if (platform == Platform::Windows) {
        if (binary.starts_with("libcocos2d")) return source_file == "Kazmath.bro";
        if (binary.starts_with("fmod")) return is_fmod;
        return true;
    }
    if (is_android(platform) && binary.starts_with("libfmod")) return is_fmod;
    return true;
}

// ---- parser setup ---------------------------------------------------------------------------------

static std::string parser_argv(Platform platform) {
    // -ferror-limit=0: clang gives up after 20 errors by default, and a few harmless ones in the bundled headers
    // (std::list, size_t...) were enough to stop the import before the STL stubs at the end of the header.
    return fmt::format(
        "-x c++ -nostdlib -nostdinc -nostdinc++ -ferror-limit=0 -target {}", info(platform).clang_target);
}

namespace {

// ---- type presence checks -------------------------------------------------------------------------

enum class TypeState { Missing, Forward, Broken, Ok };
}  // namespace

static TypeState type_state(std::string_view name) {
    tinfo_t type;
    if (!type.get_named_type(get_idati(), std::string(name).c_str())) return TypeState::Missing;
    if (type.is_forward_decl()) return TypeState::Forward;
    if (!type.is_correct() || type.get_size() == BADSIZE) return TypeState::Broken;
    return TypeState::Ok;
}

static bool stub_matches(StlStub const& stub, bool must_exist) {
    if (stub.empty()) return true;
    tinfo_t type;
    if (!type.get_named_type(get_idati(), stub.class_name.c_str())) return !must_exist;
    auto const state = type_state(stub.class_name);
    if (state == TypeState::Broken || state == TypeState::Forward) return false;

    udt_type_data_t udt;
    return type.get_udt_details(&udt) && udt.size() == stub.members.size();
}

static bool stl_structs_match(StlStubs const& stubs, bool must_exist) {
    return stub_matches(stubs.by_pointer, must_exist) && stub_matches(stubs.by_value, must_exist);
}

/// True only if the sentinel types exist and are complete.
static bool types_present(StlStubs const& stubs) {
    return std::ranges::all_of(
        std::array<std::string, 5>{
            stubs.by_pointer.class_name, stubs.by_value.class_name, "cocos2d::CCObject", "cocos2d::CCNode",
            "cocos2d::CCDirector"},
        [](std::string const& name) { return type_state(name) == TypeState::Ok; });
}

static constexpr auto kRecoveryAdvice =
    "It is recommended to go to the 'Local Types' subview, delete the 'BromaIDA' dirtree\n"
    "(you might have to right-click -> Show folders), and then save the IDB before importing types again.";

static bool verify_types_postimport(StlStubs const& stubs) {
    if (!stl_structs_match(stubs, true)) {
        show_warning(fmt::format("Faulty STL struct found when checking imported types!\n\n{}", kRecoveryAdvice));
        return false;
    }
    for (char const* name :
         {"cocos2d::CCObject", "cocos2d::CCNode", "cocos2d::CCImage", "cocos2d::CCApplication",
          "cocos2d::CCDirector"}) {
        if (type_state(name) != TypeState::Ok) {
            show_warning(
                fmt::format("Faulty struct found when checking imported types ({})!\n\n{}", name, kRecoveryAdvice));
            return false;
        }
    }
    return true;
}

// ---- local types dirtree --------------------------------------------------------------------------

static std::set<std::string> local_type_names() {
    std::set<std::string> names;
    auto const limit = get_ordinal_limit(get_idati());
    for (uint32 ordinal = 1; ordinal < limit; ++ordinal) {
        if (char const* name = get_numbered_type_name(get_idati(), ordinal)) names.emplace(name);
    }
    return names;
}

/// Empties '/BromaIDA' (deleting the types in it) so that a re-import starts clean.
static void clear_types_dir() {
    auto* tree = get_std_dirtree(DIRTREE_LOCAL_TYPES);
    if (tree->mkdir(kTypesDir) == DTE_OK) return;  // freshly created, nothing to clean

    std::vector<qstring> doomed;
    dirtree_iterator_t it;
    for (bool ok = tree->findfirst(&it, "*"); ok; ok = tree->findnext(&it)) {
        // only direct children of /BromaIDA
        auto const path = tree->get_abspath(it.cursor);
        if (path.starts_with("/BromaIDA/")) doomed.push_back(path);
    }
    for (auto const& path : doomed)
        tree->unlink(path.c_str());
}

static void move_new_types_into_dir(std::set<std::string> const& before) {
    auto* tree = get_std_dirtree(DIRTREE_LOCAL_TYPES);
    for (auto const& name : local_type_names()) {
        if (before.contains(name)) continue;
        tree->rename(("/" + name).c_str(), (std::string(kTypesDir) + "/" + name).c_str());
    }
}

// ---- function helpers -----------------------------------------------------------------------------

static std::vector<std::size_t> thunk_sizes(Platform platform) {
    switch (platform) {
        case Platform::Windows: return {6, 12};  // a jmp, or lea + jmp
        case Platform::MacIntel:
        case Platform::MacArm:
        case Platform::iOS:
        case Platform::Android32: return {12};
        case Platform::Android64: return {16};
    }
    return {};
}

static bool is_library_function(Platform platform, func_t* function) {
    if (function == nullptr) return false;
    bool const ida_is_lib = (function->flags & FUNC_LIB) != 0;

    if (ida_is_lib) {
        auto const sizes = thunk_sizes(platform);
        if (std::ranges::find(sizes, static_cast<std::size_t>(function->size())) != sizes.end()) return true;
    }

    // skimmed through 2.2082: ~450 bytes is where library and garbage functions became real ones
    if (platform == Platform::Windows && ida_is_lib && function->size() >= 450) {
        function->flags &= ~FUNC_LIB;
        update_func(function);
        set_name(function->start_ea, "", SN_NOWARN);
    }
    return false;
}

static std::string name_at(ea_t ea) {
    qstring name;
    get_ea_name(&name, ea);
    return {name.c_str(), name.length()};
}

static std::string without_numeric_suffix(std::string const& name) {
    static std::regex const re("_[0-9]+");
    return std::regex_replace(name, re, "");
}

/// Names the function, retrying with `_N` suffixes for overloads that share a name.
static bool rename_function(ea_t ea, std::string const& name, int max_tries = 10) {
    for (int i = 0; i < max_tries; ++i) {
        auto const candidate = i == 0 ? name : fmt::format("{}_{}", name, i);
        if (set_name(ea, candidate.c_str(), SN_NOWARN)) return true;
    }

    ea_t const holder = get_name_ea(BADADDR, name.c_str());
    if (holder != BADADDR && holder != ea) {
        auto const message = fmt::format(
            "{} is already taken at {:#x} while trying to rename {:#x}\n"
            "Overwrite or keep current name?\n(Old location will be renamed to sub_{:X})",
            name, holder - get_imagebase(), ea, holder);
        if (ask_two(message, "Overwrite", "Keep", Icon::Warning) == Choice::First) {
            set_name(holder, fmt::format("sub_{:x}", holder).c_str(), SN_NOWARN);
        }
    }
    return false;
}

/// Asks for consent before replacing a name the user (or an older import) might have chosen.
static void safe_rename(Settings const& settings, ea_t ea, std::string const& current, std::string const& name) {
    if (current.starts_with("sub_")) {
        rename_function(ea, name);
    } else if (without_numeric_suffix(current) != name) {
        if (settings.always_overwrite_idb ||
            ask_two(
                fmt::format(
                    "Mismatch between Broma ({}) and IDB ({})!\nOverwrite from Broma or keep current name?", name,
                    current),
                "Overwrite", "Keep", Icon::Warning) == Choice::First) {
            rename_function(ea, name);
        }
    }
}

namespace {

// ---- the importer ---------------------------------------------------------------------------------

class Importer {
public:
    Importer(
        Settings& settings, BinaryInfo const& binary, PathView source, PathView data_dir,
        std::span<Profile const> profiles)
        : m_settings(settings),
          m_binary(binary),
          m_platform(*binary.platform),
          m_source(source.toPath()),
          m_data_dir(data_dir.toPath()),
          m_profiles(profiles.begin(), profiles.end()),
          m_types(binary) {}

    geode::Result<ImportSummary, std::string> run() {
        if (auto loaded = load_sources(); loaded.isErr()) return geode::Err(loaded.unwrapErr());

        collect_classes();
        ClassGraph graph{m_class_list, m_platform};

        import_types(graph);
        collect_bindings();

        log.info(
            "Read {} {} bindings, {} duplicates and {} Broma files from {}", m_bindings.size(),
            info(m_platform).printable, m_duplicates.size(), m_file_count, m_source.string());

        apply_bindings();
        apply_linked_bindings();
        return geode::Ok(m_summary);
    }

private:
    /// A folder is loaded as a whole (see load_broma_folder); a single .bro file is loaded with what it includes.
    geode::Result<void, std::string> load_sources() {
        if (m_source.isDir()) {
            auto folder = load_broma_folder(m_source);
            if (folder.isErr())
                return geode::Err(fmt::format("Couldn't load '{}':\n\n{}", m_source.string(), folder.unwrapErr()));

            auto loaded = std::move(folder).unwrap();
            if (loaded.used_entry) {
                log.info(
                    "Using {} from {}", loaded.files.front().filename().value_or(PathView{}).string(),
                    m_source.string());
            } else {
                log.info("No Entry.bro in {}: loaded {} .bro files", m_source.string(), loaded.files.size());
            }
            for (auto const& duplicate : loaded.duplicates)
                log.warn("Duplicate class definition! ({})", duplicate);

            m_root = std::move(loaded.root);
            m_file_count = m_root.sources().size();
            m_broma_dir = m_source;
            return geode::Ok();
        }

        auto root = load_broma(m_source);
        if (root.isErr()) {
            return geode::Err(
                fmt::format(
                    "Couldn't parse '{}':\n\n{}", m_source.filename().value_or(m_source).string(), root.unwrapErr()));
        }
        m_root = std::move(root).unwrap();
        m_file_count = m_root.sources().size();
        m_broma_dir = parent_dir(m_source);
        return geode::Ok();
    }

    void collect_classes() {
        for (auto const& cls : m_root.classes) {
            if (has_platform(cls.attributes.missing, m_platform)) {
                m_unavailable.push_back(cls.name);
                continue;
            }
            if (cls.fields.empty()) log.info("Found empty class definition: {} from {}", cls.name, cls.source);
            m_class_list.push_back(&cls);
        }
        m_summary.platform = m_platform;
    }

    void collect_bindings() {
        auto const binary_file = root_filename();

        for (auto const* cls : m_class_list) {
            bool const present = is_class_present(m_platform, cls->name, binary_file);
            for (auto const& fn : bind_fields(*cls)) {
                auto binding = Binding::from_field(cls->name, fn, m_platform);

                if (present && binding.has_address()) add_binding(std::move(binding));
                if (has_platform(fn.prototype.attributes.links, m_platform) &&
                    !has_platform(fn.prototype.attributes.missing, m_platform)) {
                    m_linked.push_back(Binding::from_field(cls->name, fn, m_platform));
                }
            }
        }

        for (auto const& function : m_root.functions) {
            auto binding = Binding::from_function(function, m_platform);
            if (is_function_present(
                    m_platform, binding.name, binary_file,
                    Path{function.source}.filename().value_or(PathView{}).string()) &&
                binding.has_address()) {
                m_bindings.push_back(binding);
            }
            if (has_platform(function.prototype.attributes.links, m_platform) &&
                !has_platform(function.prototype.attributes.missing, m_platform)) {
                m_linked.push_back(std::move(binding));
            }
        }
    }

    /// Several functions on one address are merged (inlined or folded by the linker);
    /// the same function twice is reported and dropped.
    void add_binding(Binding binding) {
        auto const address = binding.bind.offset;
        auto duplicate = std::ranges::find_if(m_bindings, [&](Binding const& b) { return b.bind.offset == address; });

        if (duplicate != m_bindings.end()) {
            auto const where = fmt::format("{} and {}", binding.qualified_name(), duplicate->short_info());
            if (binding.qualified_name() == duplicate->qualified_name()) {
                log.warn("Duplicate binding with same qualified name! ({})", where);
                return;
            }
            if (binding.class_name == duplicate->class_name) {
                log.warn("Duplicate binding within same class! ({})", where);
                return;
            }
            log.warn("Duplicate binding! ({})", where);
            m_duplicates[address] = {std::move(*duplicate)};
            m_bindings.erase(duplicate);
        }

        if (auto it = m_duplicates.find(address); it != m_duplicates.end()) {
            it->second.push_back(std::move(binding));
            return;
        }
        m_bindings.push_back(std::move(binding));
    }

    // ---- types ----

    /// What a type import is built from, all chosen by the profile.
    struct TypeEnvironment {
        Profile profile;
        Bundle bundle;
        Flavor flavor;
    };

    /// Picks the profile (the settings' choice, else by the game version in the folder's name, else the newest),
    /// loads its SDK bundle and the STL flavor for this platform, and checks that the flavor declares every
    /// `std::` type the bindings use. std::nullopt: the user chose to skip the type import.
    geode::Result<std::optional<TypeEnvironment>, std::string> resolve_environment(ClassGraph const& graph) {
        if (m_profiles.empty()) {
            return geode::Err(
                fmt::format(
                    "No profiles were found in '{}'.\nBromaIDA's data folder looks incomplete.",
                    (m_data_dir / "profiles").string()));
        }

        auto const version = GdVersion::guess(m_source);
        Profile const* profile = nullptr;
        std::string how;
        if (!m_settings.profile.empty()) {
            profile = find_profile(m_profiles, m_settings.profile);
            if (profile) {
                how = "chosen in the settings";
            } else {
                log.warn(
                    "The profile '{}' from the settings doesn't exist; choosing one by version.", m_settings.profile);
            }
        }
        if (!profile && version) {
            profile = select_profile(m_profiles, *version);
            if (profile) how = fmt::format("picked for GD {}", version->to_string());
        }
        if (!profile) {
            profile = select_profile(m_profiles, GdVersion{99, 9999});  // the newest
            if (!profile) profile = &m_profiles.front();
            how = version ? fmt::format("no profile covers GD {}: using the newest", version->to_string())
                          : "the folder name has no game version: using the newest";
            log.warn("{}.", how);
        }

        auto bundle = load_bundle(m_data_dir / "sdk" / profile->sdk);
        if (bundle.isErr())
            return geode::Err(
                fmt::format(
                    "The SDK bundle '{}' of the profile '{}' can't be used:\n{}", profile->sdk, profile->name,
                    bundle.unwrapErr()));

        auto choice = resolve_flavor(*profile, m_platform, m_data_dir / "stl");
        if (choice.flavor.isErr()) {
            return geode::Err(
                fmt::format(
                    "The STL flavor '{}' for {} isn't installed:\n{}", choice.requested, info(m_platform).printable,
                    choice.flavor.unwrapErr()));
        }
        auto flavor = std::move(choice.flavor).unwrap();

        log.info(
            "Profile '{}' ({}), SDK bundle '{}', STL flavor '{}'", profile->name, how, profile->sdk,
            flavor.manifest.name);
        if (choice.fell_back) {
            log.warn(
                "The profile wants the '{}' STL flavor, which isn't installed: using '{}'. Layouts of std:: types on "
                "{} may be wrong.",
                choice.requested, flavor.manifest.name, info(m_platform).printable);
        }

        // IDAClang crashes on a class with a member of an undeclared type, so ask before feeding it one
        auto const missing = missing_std_types(graph.used_std_types(), flavor);
        if (!missing.empty()) {
            std::string list;
            for (auto const& type : missing)
                list += (list.empty() ? "" : ", ") + type;
            log.warn("The '{}' STL flavor doesn't declare: {}", flavor.manifest.name, list);
            auto const answer = ask_two(
                fmt::format(
                    "The bindings use std:: types that BromaIDA's '{}' STL flavor doesn't declare:\n\n{}\n\n"
                    "Classes with such a member are incomplete, and IDA can crash while importing them.\n"
                    "Skipping the type import is the safe choice.",
                    flavor.manifest.name, list),
                "Import Anyway", "Skip Type Import", Icon::Warning, true);
            if (answer == Choice::Second) return geode::Ok(std::optional<TypeEnvironment>{});
        }
        return geode::Ok(std::optional{TypeEnvironment{*profile, std::move(bundle).unwrap(), std::move(flavor)}});
    }

    void import_types(ClassGraph const& graph) {
        if (!m_settings.import_types) return;

        auto parser = select_clang_parser(m_settings.type_parser);
        if (!parser) {
            show_warning("Trying to import types without IDAClang!\nDisabling importing of types...");
            m_settings.import_types = false;
            return;
        }

        auto resolved = resolve_environment(graph);
        if (resolved.isErr()) {
            show_warning(fmt::format("{}\n\nThe type import was skipped.", resolved.unwrapErr()));
            return;
        }
        if (!resolved.unwrap()) {
            log.info("Type import skipped by the user.");
            return;
        }
        auto const environment = std::move(resolved).unwrap().value();
        m_stl_dir = environment.flavor.dir;

        auto const output = generate_types_header(
            CodegenInput{
                .platform = m_platform,
                .classes = m_class_list,
                .unavailable_classes = m_unavailable,
                .bundle = environment.bundle,
                .defines = environment.profile.defines,
                .broma_dir = m_broma_dir,
                .generator_version = std::string(kVersion)},
            graph);

        auto const types_file = user_cache_dir() / "broma_types.hpp";
        if (auto made = asp::fs::createDirAll(parent_dir(types_file)); made.isErr()) {
            log.error("Couldn't create {}: {}", parent_dir(types_file).string(), made.unwrapErr().message());
            return;
        }
        if (auto written = asp::fs::write(types_file, output.text); written.isErr()) {
            log.error("Couldn't write {}: {}", types_file.string(), written.unwrapErr().message());
            return;
        }

        if (output.enum_count == 0) {
            // everything that uses a Geode enum fails to parse without it: say so loudly
            auto const expected = (parent_dir(m_broma_dir) / "include/Geode/Enums.hpp").lexicallyNormal();
            log.warn("No enums were found. Expected Geode's Enums.hpp at {}", expected.string());
            show_warning(
                fmt::format(
                    "Geode's Enums.hpp wasn't found at:\n{}\n\n"
                    "Classes that use its enums (EasingType, PlayerButton...) will be incomplete. In the bindings "
                    "repository it sits next to the version folders, in bindings/include/Geode/.",
                    expected.string()));
        }

        m_parser = *parser;
        auto const digest = digest_hex(
            {output.text, m_parser, parser_argv(m_platform), environment.profile.name,
             environment.flavor.manifest.name});
        auto state = IdbState::load();
        bool do_import = true;

        if (!m_settings.disable_input_hash_check) {
            if (state.types_digest == digest) {
                if (types_present(graph.stl_stubs())) {
                    log.info("Types digest matches the last import.");
                    show_info(
                        "Detected no changes since the last type import.\nType import will be skipped.\n\n"
                        "You can disable this check in 'Settings'.");
                    do_import = false;
                    m_has_types = true;
                } else {
                    log.warn(
                        "Types digest matches but BromaIDA's types are missing from this IDB "
                        "(reverted snapshot?). Re-importing.");
                }
            }
        } else {
            log.info("Hash check disabled. Proceeding with type import.");
        }

        if (do_import) {
            if (m_settings.ignore_mismatched_structs || stl_structs_match(graph.stl_stubs(), false) ||
                ask_two(
                    "Mismatch from previously imported STL types!\n\n"
                    "It is recommended to cancel the current type import, go to the 'Local Types' subview,\n"
                    "delete the 'BromaIDA' dirtree (you might have to right-click -> Show folders),\n"
                    "and then save the IDB.\n\nContinue to overwrite previous types anyway?",
                    "Overwrite", "Cancel", Icon::Warning) == Choice::First) {
                switch (ask_three(
                    "Importing Types...\nThis can possibly freeze IDA for up to minutes.\n"
                    "Click on 'OK' to confirm.",
                    "OK", "Skip This Time", "Always Skip")) {
                    case Choice::Second: log.info("Types import cancelled by user for this time."); break;
                    case Choice::Third:
                        m_settings.import_types = false;
                        log.info("Types import cancelled and disabled by user.");
                        break;
                    case Choice::First:
                        m_has_types = run_parser(types_file, graph.stl_stubs());
                        m_types.clear();
                        break;
                }

                if (m_has_types) {
                    // only remember the digest once the import verified successfully
                    state.types_digest = digest;
                    state.types_platform = std::string(to_string(m_platform));
                    state.save();
                    log.info("Successfully imported types from {} Broma classes.", m_class_list.size());
                } else {
                    m_has_types = types_present(graph.stl_stubs());
                }
            } else {
                m_has_types = types_present(graph.stl_stubs());
            }
        }

        m_summary.types_imported = m_has_types;
    }

    bool run_parser(PathView types_file, StlStubs const& stubs) {
        if (m_settings.set_default_parser_args) set_parser_argv(m_parser.c_str(), parser_argv(m_platform).c_str());
        set_c_header_path(m_stl_dir.string().c_str());

        auto const before = local_type_names();
        {
            WaitBox wait{"Importing types..."};
            clear_types_dir();
            parse_decls_with_parser(m_parser.c_str(), nullptr, types_file.string().c_str(), true);
            move_new_types_into_dir(before);
        }
        return verify_types_postimport(stubs);
    }

    // ---- bindings ----

    void apply_bindings() {
        auto const total = m_bindings.size();
        std::size_t resolved = 0;

        for (auto const& binding : m_bindings) {
            if (binding.is_missing) {
                log.warn("Binding has an address but has missing attribute ({})! Skipping.", binding.short_info());
                continue;
            }
            if (apply_one(binding)) ++resolved;
        }

        std::size_t duplicate_total = 0, duplicate_resolved = 0;
        for (auto const& [address, group] : m_duplicates) {
            duplicate_total += group.size();
            if (apply_duplicates(address, group)) duplicate_resolved += group.size();
        }

        m_summary.bindings = total + duplicate_total;
        m_summary.resolved = resolved + duplicate_resolved;
        m_summary.duplicates = duplicate_total;
        if (m_summary.bindings != 0) {
            log.info(
                "Resolved and mapped {}/{} bindings onto their respective addresses ({}/{} from merged duplicates).",
                m_summary.resolved, m_summary.bindings, duplicate_resolved, duplicate_total);
        }
    }

    /// Finds the function at the binding's address, offering to create it when IDA hasn't marked one.
    func_t* function_for(Binding const& binding, ea_t ea) {
        if (name_at(ea).starts_with("loc_")) add_func(ea);

        auto* function = get_func(ea);
        if (is_library_function(m_platform, function)) {
            log.warn("Tried to rename a library function! ({})", binding.short_info());
            return nullptr;
        }
        function = get_func(ea);  // is_library_function can rename or reclassify it

        if (function == nullptr && !m_settings.ignore_unmarked_functions) {
            TemporaryJump jump{ea};
            if (ask_two(
                    fmt::format(
                        "{:#x} is not marked as a function by IDA.\nWould you like to mark it as a "
                        "function now?",
                        ea),
                    "Yes", "No", Icon::Info) != Choice::First) {
                return nullptr;
            }
            if (!add_func(ea)) {
                log.warn("Failed to mark the address at {:#x} as a function! ({})", ea, binding.short_info());
                return nullptr;
            }
            function = get_func(ea);
        }

        if (function == nullptr) {
            log.warn("Couldn't retrieve function at {:#x}! (Function: {})", ea, binding.qualified_name());
            return nullptr;
        }
        if (function->start_ea != ea) {
            log.warn("Function is in the middle of another one! ({})", binding.short_info());
            return nullptr;
        }
        return function;
    }

    bool apply_one(Binding const& binding) {
        ea_t const ea = get_imagebase() + binding.bind.offset;
        auto const current_name = name_at(ea);

        if (function_for(binding, ea) == nullptr) return false;

        if (m_has_types && m_types.has_mismatch(ea, binding) && !m_types.apply_signature(ea, binding)) {
            log.warn("Failed to fix function signature for '{}'!", binding.qualified_name());
        }
        safe_rename(m_settings, ea, name_at(ea), binding.ida_qualified_name(is_visible_cp('~')));
        (void)current_name;
        return true;
    }

    bool apply_duplicates(std::uint64_t address, std::vector<Binding> const& group) {
        ea_t const ea = get_imagebase() + address;
        auto* function = get_func(ea);
        if (function == nullptr) {
            std::string names;
            for (auto const& b : group)
                names += (names.empty() ? "" : ", ") + b.qualified_name();
            log.warn(
                "Couldn't retrieve function for merged duplicates at {:#x}! Skipping. (Would've merged: {})", ea,
                names);
            return false;
        }

        auto const& first = group.front();
        if (m_has_types && m_types.has_mismatch(ea, first) && !m_types.apply_signature(ea, first)) {
            log.warn("Failed to fix function signature for '{}' (merged duplicate)!", first.qualified_name());
        }
        // the first occurrence names the function
        safe_rename(m_settings, ea, name_at(ea), first.ida_qualified_name(is_visible_cp('~')));
        write_merge_comment(ea, address, group);
        return true;
    }

    void write_merge_comment(ea_t ea, std::uint64_t address, std::vector<Binding> const& group) {
        constexpr std::string_view prefix = "Merged with: ";
        std::string names;
        for (auto const& b : group)
            names += (names.empty() ? "" : ", ") + b.qualified_name();
        auto const wanted = std::string(prefix) + names;

        qstring current_raw;
        get_func_cmt_ea(&current_raw, ea, true);
        std::string const current{current_raw.c_str(), current_raw.length()};

        if (current.empty()) {
            set_func_cmt_ea(ea, wanted.c_str(), true);
        } else if (current.starts_with(prefix)) {
            auto const old_names = current.substr(prefix.size());
            if (names == old_names) return;

            // set it anyway, then see whether IDA truncated it
            set_func_cmt_ea(ea, wanted.c_str(), true);
            qstring updated_raw;
            get_func_cmt_ea(&updated_raw, ea, true);
            std::string const updated{updated_raw.c_str(), updated_raw.length()};
            if (updated.starts_with(prefix) && old_names == updated.substr(prefix.size())) return;

            log.warn(
                "Mismatch or new data in merged function list (Current: {} | Correct/Truncated: {})! Correcting...",
                old_names, updated.starts_with(prefix) ? updated.substr(prefix.size()) : updated);
        } else if (
            m_settings.always_overwrite_merge_information ||
            ask_two(
                fmt::format(
                    "{:#x} already has a comment! Would you like to overwrite it with merge "
                    "information or keep the current comment?\n"
                    "This prompt may show up again in future imports if the comment is kept.\n\n"
                    "You can enable 'Always Overwrite Function Comments With Merge Information' "
                    "in settings to automatically overwrite comments on future imports.",
                    address),
                "Overwrite", "Keep") == Choice::First) {
            set_func_cmt_ea(ea, wanted.c_str(), true);
        }
    }

    /// Symbols IDA already named (exports of libraries, Android builds) get types by name.
    void apply_linked_bindings() {
        m_summary.linked = m_linked.size();
        if (!m_has_types) {
            if (!m_linked.empty()) {
                log.info(
                    "Skipping resolution for {} linked binding(s). Types must be imported to apply type "
                    "information.",
                    m_linked.size());
            }
            return;
        }
        if (m_linked.empty()) return;

        auto index = build_symbol_index();
        for (auto const& binding : m_linked) {
            auto it = index.find(binding.qualified_name());
            if (it == index.end() || it->second.empty()) continue;

            auto matched = m_types.resolve_overload(binding, it->second);
            if (matched.empty()) {
                // the name exists but no symbol has these arguments: usually an overload of another platform or
                // game version, so this is only worth a line when debugging
                log.debug(
                    "No symbol matches the overload '{} {}({})' ({} other overload(s) of that name exist).",
                    binding.ida_return_type().str(), binding.qualified_name(), binding.args_string(false, false),
                    it->second.size());
                continue;
            }
            ++m_summary.linked_resolved;

            for (ea_t const ea : matched) {
                std::erase(it->second, ea);
                if (m_types.has_mismatch(ea, binding) && !m_types.apply_signature(ea, binding)) {
                    log.warn("Failed to fix function signature for '{}' (linked symbol)!", binding.qualified_name());
                }
            }
        }
        log.info("Resolved {}/{} linked bindings by symbol.", m_summary.linked_resolved, m_linked.size());
    }

    /// Every named address by its demangled qualified name, argument list stripped, so overloads share a key.
    static std::unordered_map<std::string, std::vector<ea_t>> build_symbol_index() {
        static std::regex const re(R"((\S+)::(\S+)\(.*\))");
        std::unordered_map<std::string, std::vector<ea_t>> index;
        for (size_t i = 0, n = get_nlist_size(); i < n; ++i) {
            ea_t const ea = get_nlist_ea(i);
            qstring demangled;
            if (get_ea_name(&demangled, ea, GN_SHORT | GN_DEMANGLED) <= 0) continue;
            index[std::regex_replace(std::string(demangled.c_str(), demangled.length()), re, "$1::$2")].push_back(ea);
        }
        return index;
    }

    Settings& m_settings;
    BinaryInfo m_binary;
    Platform m_platform;
    Path m_source;     ///< The folder or file the user picked.
    Path m_broma_dir;  ///< The folder the Broma files live in; `../include/Geode/Enums.hpp` is read from next to it.
    std::size_t m_file_count = 0;
    Path m_data_dir;  ///< The plugin's data folder: profiles/, sdk/ and stl/.
    std::vector<Profile> m_profiles;
    TypeContext m_types;

    broma::Root m_root;
    std::vector<broma::Class const*> m_class_list;
    std::vector<std::string> m_unavailable;  ///< Classes marked missing on this platform.
    std::vector<Binding> m_bindings;
    std::vector<Binding> m_linked;
    std::map<std::uint64_t, std::vector<Binding>> m_duplicates;

    std::string m_parser;
    Path m_stl_dir;  ///< The STL flavor the parser reads `std::` from.
    bool m_has_types = false;
    ImportSummary m_summary;
};
}  // namespace

geode::Result<ImportSummary, std::string> import_bindings(
    Settings& settings, BinaryInfo const& binary, PathView source, PathView data_dir,
    std::span<Profile const> profiles) {
    if (!binary.platform) {
        return geode::Err("The open binary isn't one of the platforms Broma knows (Windows, macOS, iOS, Android).");
    }
    if (!source.exists()) return geode::Err(fmt::format("'{}' doesn't exist.", source.string()));
    return Importer{settings, binary, source, data_dir, profiles}.run();
}

}  // namespace bromaida::ida
