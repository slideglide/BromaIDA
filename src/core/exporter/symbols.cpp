#include <bromaida/exporter/symbols.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>

#include <asp/iter.hpp>
#include <fmt/format.h>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#include <cstdlib>
#define BROMAIDA_HAS_CXXABI 1
#endif

#include <bromaida/typegen/stl_types.hpp>
#include <bromaida/util/text.hpp>

namespace bromaida {
    Demangler itanium_demangler() {
#ifdef BROMAIDA_HAS_CXXABI
        return [](std::string_view mangled) -> std::optional<std::string> {
            int status = 0;
            std::string input{mangled};
            char* out = abi::__cxa_demangle(input.c_str(), nullptr, nullptr, &status);
            if (status != 0 || !out) return std::nullopt;
            std::string result{out};
            std::free(out);
            return result;
        };
#else
        return [](std::string_view) -> std::optional<std::string> { return std::nullopt; };
#endif
    }

    struct Parsed {
        FunctionSignature sig;
        RawSymbol const* symbol;
        std::string mangled;
    };

    static constexpr auto kRejectedSymbols = std::to_array<std::string_view>(
        {"thunk to", "vtable for", "typeinfo", "VTT for", "guard variable", "{lambda", "(anonymous namespace)",
         "[clone ", "construction vtable", "reference temporary", "covariant return thunk"});

    std::optional<FunctionSignature> parse_demangled_function(std::string_view text) {
        text = cpp::trim(text);
        if (asp::iter::from(kRejectedSymbols).any([&](std::string_view reject) { return text.contains(reject); }))
            return std::nullopt;

        FunctionSignature sig;

        auto tail_const = [&](std::string_view q) {
            if (text.ends_with(q)) {
                text.remove_suffix(q.size());
                return true;
            }
            return false;
        };
        if (tail_const(" const")) sig.is_const = true;
        tail_const(" volatile");
        if (!text.ends_with(')')) return std::nullopt;

        std::size_t scan_from = 0;
        if (auto const op = text.find("operator"); op != std::string_view::npos) {
            scan_from = op + 8;
            if (text.substr(scan_from).starts_with("()")) scan_from += 2;
        }
        int angle = 0;
        std::size_t open = std::string_view::npos;
        for (std::size_t i = scan_from; i < text.size(); ++i) {
            char const c = text[i];
            if (c == '<' && !(i >= 8 && text.substr(i - 8, 8) == "operator"))
                ++angle;
            else if (c == '>' && angle > 0)
                --angle;
            else if (c == '(' && angle == 0) {
                open = i;
                break;
            }
        }
        if (open == std::string_view::npos) return std::nullopt;

        auto const qualified = cpp::trim(text.substr(0, open));
        if (qualified.empty() || (qualified.contains('<') && !qualified.contains("operator"))) return std::nullopt;

        for (auto const& raw : cpp::split_top_level(text.substr(open + 1, text.size() - open - 2))) {
            if (raw == "...") {
                sig.is_variadic = true;
                continue;
            }
            if (cpp::looks_malformed(raw)) return std::nullopt;
            sig.params.push_back(Param{TypeRef{stl::sugar(cpp::clean_demangled_type(raw))}, {}, {}});
        }

        auto q = cpp::split_qualified(qualified);
        if (q.bare.starts_with("operator") && q.bare.size() > 8 &&
            (std::isalpha(static_cast<unsigned char>(q.bare[8])) || q.bare[8] == ' ')) {
            return std::nullopt;
        }
        sig.name = q.bare;
        sig.class_name = q.scope();
        sig.ret = TypeRef{"TodoReturn"};
        return sig;
    }

    static std::string strip_prefix(std::string text, std::string_view prefix) {
        if (text.starts_with(prefix)) text.erase(0, prefix.size());
        return text;
    }

    static bool is_ctor(FunctionSignature const& sig) {
        auto const last = cpp::split_qualified(sig.class_name).bare;
        return !last.empty() && sig.name == last;
    }

    ExtractedBinary extract_binary(ExtractInput const& input) {
        auto const fallback = input.demangle ? Demangler{} : itanium_demangler();
        auto const& demangle = input.demangle ? input.demangle : fallback;

        std::vector<Parsed> parsed;
        std::set<std::string> known_classes;
        std::unordered_map<std::string, ClassLayout> layouts;

        for (auto const& layout : input.layouts) {
            known_classes.insert(layout.name);
            layouts.emplace(layout.name, layout);
        }

        for (auto const& symbol : input.symbols) {
            if (!symbol.name.starts_with("_Z")) continue;
            auto demangled = demangle(symbol.name);
            if (!demangled) continue;

            if (symbol.name.starts_with("_ZTV")) {
                known_classes.insert(strip_prefix(*demangled, "vtable for "));
                continue;
            }
            if (!symbol.is_function) continue;

            auto sig = parse_demangled_function(*demangled);
            if (!sig) continue;
            if (symbol.is_static) sig->is_static = *symbol.is_static;
            parsed.push_back({std::move(*sig), &symbol, symbol.name});
        }

        for (auto& p : parsed) {
            auto& sig = p.sig;
            if (sig.class_name.empty()) continue;
            if (is_ctor(sig) || sig.name.starts_with('~')) known_classes.insert(sig.class_name);
        }

        ExtractedBinary result;
        result.platform = input.platform;
        std::map<std::string, ExtractedClass> classes;
        std::set<std::string> seen;

        std::ranges::stable_sort(
            parsed, [](Parsed const& a, Parsed const& b) { return a.symbol->offset < b.symbol->offset; });

        for (auto& p : parsed) {
            auto& sig = p.sig;
            bool const in_class = known_classes.contains(sig.class_name);

            if (in_class) {
                sig.is_ctor = is_ctor(sig);
                sig.is_dtor = sig.name.starts_with('~');
                if (sig.is_ctor || sig.is_dtor) sig.ret = TypeRef{};
            }

            auto const key = sig.qualified_name() + "|" + sig.overload_key() + (sig.is_const ? "|c" : "");
            if (!seen.insert(key).second) continue;

            ExtractedMethod method{sig, Bind{BindState::Bound, p.symbol->offset}};
            if (!in_class) {
                method.sig.name = sig.qualified_name();
                method.sig.class_name.clear();
                result.free_functions.push_back(std::move(method));
                continue;
            }

            auto& cls = classes.try_emplace(sig.class_name).first->second;
            cls.name = sig.class_name;
            cls.methods.push_back(std::move(method));
        }

        for (auto const& name : known_classes) {
            auto& cls = classes.try_emplace(name).first->second;
            cls.name = name;
            if (auto it = layouts.find(name); it != layouts.end()) {
                cls.bases = it->second.bases;

                for (auto const& slot : it->second.vtable_symbols) {
                    auto demangled = demangle(slot);
                    auto sig = demangled ? parse_demangled_function(*demangled) : std::nullopt;
                    if (!sig) continue;
                    cls.vtable.push_back(sig->overload_key());

                    auto const key = sig->overload_key();
                    for (auto& m : asp::iter::from(cls.methods).filter([&](auto const& m) {
                             return m.sig.overload_key() == key;
                         })) {
                        m.sig.is_virtual = true;
                    }
                }
            }
        }

        for (auto& [name, cls] : classes)
            result.classes.push_back(std::move(cls));
        return result;
    }
}  // namespace bromaida
