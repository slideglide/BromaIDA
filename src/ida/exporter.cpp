#include "exporter.hpp"

#include <algorithm>
#include <map>
#include <regex>
#include <set>

#include <asp/iter.hpp>
#include <fmt/format.h>

#include <bromaida/broma_io.hpp>
#include <bromaida/log.hpp>
#include <bromaida/symbols.hpp>
#include <bromaida/text.hpp>

#include "ida_ui.hpp"

#include <pro.h>
#include <bytes.hpp>
#include <funcs.hpp>
#include <ida.hpp>
#include <kernwin.hpp>
#include <name.hpp>
#include <nalt.hpp>
#include <typeinf.hpp>

namespace bromaida::ida {
    static constexpr Log log{"Exporter"};

    static std::string to_std(qstring const& s) { return {s.c_str(), s.length()}; }

    static std::string print_type(tinfo_t const& type) {
        qstring out;
        type.print(&out);
        return cpp::normalize_type(to_std(out));
    }

    namespace detail {
        struct IdaFunction {
            std::string return_type;
            std::vector<Param> params;
            bool is_static = true;
            bool uses_std = false;
        };

        struct Named {
            ea_t ea;
            std::string name;
        };
    }  // namespace detail

    using namespace detail;

    static bool is_generated_name(std::string const& name) {
        static std::regex const re("(p|a)[0-9]+");
        return name.empty() || std::regex_match(name, re);
    }

    static std::optional<IdaFunction> read_function(ea_t ea, std::string_view class_name, bool itanium) {
        tinfo_t type;
        func_type_data_t data;
        if (!get_tinfo(&type, ea) || !type.is_func() || !type.get_func_details(&data)) return std::nullopt;

        IdaFunction out;
        out.return_type = print_type(data.rettype);
        out.uses_std = out.return_type.contains("std::");

        std::vector<funcarg_t> args(data.begin(), data.end());
        // a hidden return pointer sits before `this` on Itanium and after it on MSVC
        auto drop_return_pointer = [&](std::size_t index) {
            if (index < args.size() && (args[index].flags & FAI_RETPTR)) args.erase(args.begin() + index);
        };
        if (itanium) drop_return_pointer(0);
        if (!class_name.empty() && !args.empty() && print_type(args.front().type) == std::string(class_name) + "*") {
            out.is_static = false;
            args.erase(args.begin());
        }
        if (!itanium) drop_return_pointer(0);

        for (auto const& arg : args) {
            Param param;
            param.type = TypeRef{print_type(arg.type)};
            if (param.type.str().contains("std::")) out.uses_std = true;
            if (auto name = to_std(arg.name); !is_generated_name(name)) param.name = std::move(name);
            out.params.push_back(std::move(param));
        }
        return out;
    }

    ExtractedBinary extract_from_idb(BinaryInfo const& binary, Settings const& settings) {
        auto const platform = binary.platform.value_or(Platform::Windows);
        bool const itanium = binary.is_itanium_abi();
        auto const image_base = get_imagebase();

        ExtractInput symbols{.platform = platform};
        std::vector<Named> broma_style;

        for (size_t i = 0, n = get_nlist_size(); i < n; ++i) {
            ea_t const ea = get_nlist_ea(i);
            std::string name = get_nlist_name(i);
            auto* function = get_func(ea);
            bool const is_function = function != nullptr && function->start_ea == ea;

            if (name.starts_with("_Z")) {
                symbols.symbols.push_back(
                    {.name = std::move(name), .offset = ea - image_base, .is_function = is_function});
            } else if (is_function && name.contains("::")) {
                broma_style.push_back({ea, std::move(name)});
            }
        }

        auto result = extract_binary(symbols);
        std::map<std::string, ExtractedClass> classes;
        for (auto& cls : result.classes)
            classes.emplace(cls.name, std::move(cls));

        std::set<std::string> taken;
        for (auto const& named : broma_style)
            taken.insert(named.name);

        for (auto const& named : broma_style) {
            auto name = cpp::split_qualified(named.name);
            if (name.namespaces.empty() || name.bare.empty()) continue;

            static std::regex const overload_suffix(R"((.+)_[0-9]+)");
            std::smatch m;
            if (std::regex_match(name.bare, m, overload_suffix) && taken.contains(name.scope() + "::" + m[1].str())) {
                name.bare = m[1];
            }

            FunctionSignature sig;
            sig.class_name = name.scope();
            sig.name = name.bare;
            sig.is_dtor = sig.name.starts_with('~');
            sig.is_ctor = sig.name == cpp::split_qualified(sig.class_name).bare;

            auto function = read_function(named.ea, sig.class_name, itanium);
            if (function && function->uses_std) continue;

            sig.ret = TypeRef{"TodoReturn"};
            if (function) {
                sig.is_static = function->is_static;
                sig.params = std::move(function->params);
                if (settings.export_return_types && !function->return_type.empty() && !sig.is_ctor && !sig.is_dtor) {
                    sig.ret = TypeRef{function->return_type};
                }
                if (sig.is_ctor || sig.is_dtor) sig.ret = TypeRef{};
            }

            auto& cls = classes.try_emplace(sig.class_name).first->second;
            cls.name = sig.class_name;
            cls.exported = false;
            auto const duplicate = asp::iter::from(cls.methods).any([&](ExtractedMethod const& existing) {
                return existing.bind.offset == named.ea - image_base && existing.sig.same_overload(sig);
            });
            if (!duplicate) cls.methods.push_back({std::move(sig), Bind{BindState::Bound, named.ea - image_base}});
        }

        result.classes.clear();
        for (auto& [_, cls] : classes)
            result.classes.push_back(std::move(cls));
        result.platform = platform;
        return result;
    }

    namespace detail {
        class IdaPrompter final : public ExportPrompter {
        public:
            [[nodiscard]] bool overwrite_address(
                std::string_view qualified_name, std::string_view existing, std::string_view found) override {
                return ask_two(
                           fmt::format(
                               "{} is bound to {} in the Broma file but IDA has it at {}.\n"
                               "Overwrite the address in the file or keep it?",
                               qualified_name, existing, found),
                           "Overwrite", "Keep", Icon::Warning) == Choice::First;
            }
        };
    }  // namespace detail

    geode::Result<ExportSummary, std::string> export_bindings(
        Settings const& settings, BinaryInfo const& binary, PathView file) {
        if (!binary.platform) return geode::Err(std::string{"The open binary isn't one of the platforms Broma knows."});

        std::optional<broma::Root> template_root;
        if (file.exists()) {
            auto loaded = load_broma(file);
            if (!loaded)
                return geode::Err(
                    fmt::format(
                        "'{}' is used as the template for the export, but it isn't valid Broma:\n\n{}\n\n"
                        "Fix it, pick another file, or delete it to export from scratch.",
                        file.filename().value_or(file).string(), loaded.unwrapErr()));
            template_root = std::move(loaded).unwrap();
        }

        WaitBox wait{"Reading functions from the database..."};
        auto extracted = extract_from_idb(binary, settings);

        ExportOptions options;
        options.platform = *binary.platform;
        options.include_return_types = settings.export_return_types;
        options.include_param_names = settings.export_args_names;
        options.only_template_classes = settings.export_only_template_classes;
        options.default_file = file.toPath();

        IdaPrompter prompter;
        auto plan = plan_export(extracted, template_root ? &*template_root : nullptr, options, prompter);
        if (!plan) return geode::Err(plan.unwrapErr());

        auto result = std::move(plan).unwrap();
        for (auto const& warning : result.stats.warnings)
            log.warn("{}", warning);

        if (auto applied = apply_plan(result); !applied) return geode::Err(applied.unwrapErr());
        return geode::Ok(ExportSummary{.stats = result.stats, .files_written = result.files.size()});
    }

    geode::Result<std::string, std::string> export_function(BinaryInfo const& binary, unsigned long long ea) {
        if (!binary.platform) return geode::Err(std::string{"The open binary isn't one of the platforms Broma knows."});

        auto* function = get_func(static_cast<ea_t>(ea));
        if (function == nullptr) return geode::Err(std::string{"There is no function at the cursor."});

        qstring raw;
        get_ea_name(&raw, function->start_ea);
        auto name = cpp::split_qualified(to_std(raw));

        FunctionSignature sig;
        sig.class_name = name.scope();
        sig.name = name.bare.empty() ? to_std(raw) : name.bare;
        sig.ret = TypeRef{"TodoReturn"};

        if (auto info = read_function(function->start_ea, sig.class_name, binary.is_itanium_abi())) {
            sig.is_static = info->is_static;
            sig.params = std::move(info->params);
            if (!info->return_type.empty()) sig.ret = TypeRef{info->return_type};
        }
        return geode::Ok(
            export_function_line(sig, Bind{BindState::Bound, function->start_ea - get_imagebase()}, *binary.platform));
    }
}  // namespace bromaida::ida
