#include <bromaida/symbols.hpp>

#include <algorithm>
#include <ranges>
#include <cctype>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include <asp/fs.hpp>
#include <fmt/format.h>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#include <cstdlib>
#define BROMAIDA_HAS_CXXABI 1
#endif

#include <bromaida/stl_types.hpp>
#include <bromaida/stl_types.hpp>
#include <bromaida/text.hpp>

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

// ---- ELF -----------------------------------------------------------------------------------

template <class T>
static T read_raw(std::vector<std::uint8_t> const& data, std::size_t offset) {
    T value{};
    if (offset + sizeof(T) <= data.size()) std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

namespace {

struct ElfSection {
    std::uint32_t type = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t link = 0;
    std::uint64_t entsize = 0;
};
}  // namespace

geode::Result<std::vector<RawSymbol>, std::string> read_elf_symbols(PathView file) {
    auto loaded = asp::fs::read(file);
    if (loaded.isErr())
        return geode::Err(fmt::format("cannot read {}: {}", file.string(), loaded.unwrapErr().message()));
    auto const data = std::move(loaded).unwrap();

    if (data.size() < 64 || std::memcmp(
                                data.data(),
                                "\x7f"
                                "ELF",
                                4) != 0) {
        return geode::Err(file.string() + " is not an ELF file");
    }
    if (data[5] != 1) return geode::Err("only little-endian ELF files are supported");

    bool const is64 = data[4] == 2;
    auto const machine = read_raw<std::uint16_t>(data, 18);
    std::uint64_t const shoff = is64 ? read_raw<std::uint64_t>(data, 0x28) : read_raw<std::uint32_t>(data, 0x20);
    std::uint16_t const shentsize = read_raw<std::uint16_t>(data, is64 ? 0x3A : 0x2E);
    std::uint16_t const shnum = read_raw<std::uint16_t>(data, is64 ? 0x3C : 0x30);

    std::vector<ElfSection> sections(shnum);
    for (std::size_t i = 0; i < shnum; ++i) {
        std::size_t const base = shoff + i * shentsize;
        auto& s = sections[i];
        s.type = read_raw<std::uint32_t>(data, base + 4);
        if (is64) {
            s.offset = read_raw<std::uint64_t>(data, base + 0x18);
            s.size = read_raw<std::uint64_t>(data, base + 0x20);
            s.link = read_raw<std::uint32_t>(data, base + 0x28);
            s.entsize = read_raw<std::uint64_t>(data, base + 0x38);
        } else {
            s.offset = read_raw<std::uint32_t>(data, base + 0x10);
            s.size = read_raw<std::uint32_t>(data, base + 0x14);
            s.link = read_raw<std::uint32_t>(data, base + 0x18);
            s.entsize = read_raw<std::uint32_t>(data, base + 0x24);
        }
    }

    std::vector<RawSymbol> out;
    std::set<std::pair<std::string, std::uint64_t>> seen;

    for (auto const& table : sections) {
        constexpr std::uint32_t kSymtab = 2, kDynsym = 11;
        if (table.type != kSymtab && table.type != kDynsym) continue;
        if (table.link >= sections.size()) continue;
        auto const& strings = sections[table.link];
        std::uint64_t const entsize = table.entsize ? table.entsize : (is64 ? 24 : 16);

        for (std::uint64_t i = 0; i + entsize <= table.size; i += entsize) {
            std::size_t const at = table.offset + i;
            std::uint32_t name_index;
            std::uint64_t value;
            std::uint8_t info;
            std::uint16_t shndx;
            if (is64) {
                name_index = read_raw<std::uint32_t>(data, at);
                info = data[at + 4];
                shndx = read_raw<std::uint16_t>(data, at + 6);
                value = read_raw<std::uint64_t>(data, at + 8);
            } else {
                name_index = read_raw<std::uint32_t>(data, at);
                value = read_raw<std::uint32_t>(data, at + 4);
                info = data[at + 12];
                shndx = read_raw<std::uint16_t>(data, at + 14);
            }
            if (shndx == 0 || name_index == 0) continue;  // undefined import

            auto const type = static_cast<std::uint8_t>(info & 0xF);
            if (type != 1 && type != 2) continue;  // objects (vtables) and functions only

            std::size_t const name_at = strings.offset + name_index;
            if (name_at >= data.size()) continue;
            std::string name{reinterpret_cast<char const*>(data.data() + name_at)};

            if (type == 2 && machine == 40) value &= ~1ULL;  // ARM Thumb bit
            if (!seen.emplace(name, value).second) continue;
            out.push_back({std::move(name), value, type == 2, std::nullopt});
        }
    }
    return geode::Ok(std::move(out));
}

// ---- demangled-name parsing -------------------------------------------------------------------

std::optional<FunctionSignature> parse_demangled_function(std::string_view text) {
    text = cpp::trim(text);
    // things that aren't plain functions
    for (std::string_view reject :
         {"thunk to", "vtable for", "typeinfo", "VTT for", "guard variable", "{lambda", "(anonymous namespace)",
          "[clone ", "construction vtable", "reference temporary", "covariant return thunk"}) {
        if (text.contains(reject)) return std::nullopt;
    }

    FunctionSignature sig;

    // trailing cv-qualifiers of the method
    auto tail_const = [&](std::string_view q) {
        if (text.ends_with(q)) {
            text.remove_suffix(q.size());
            return true;
        }
        return false;
    };
    if (tail_const(" const")) sig.is_const = true;
    tail_const(" volatile");
    if (!text.ends_with(')')) return std::nullopt;  // function templates keep a return type; skip them

    // find the opening parenthesis of the argument list, honoring `operator()`
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
    if (qualified.empty() || (qualified.contains('<') && !qualified.contains("operator")))
        return std::nullopt;  // templates

    for (auto const& raw : cpp::split_top_level(text.substr(open + 1, text.size() - open - 2))) {
        if (raw == "...") {
            sig.is_variadic = true;
            continue;
        }
        if (cpp::looks_malformed(raw)) return std::nullopt;
        sig.params.push_back(Param{TypeRef{stl::sugar(cpp::clean_demangled_type(raw))}, {}, {}});
    }

    auto q = cpp::split_qualified(qualified);
    // only symbolic operators are expressible; conversion operators (`operator void(*)()`) are not
    if (q.bare.starts_with("operator") && q.bare.size() > 8 &&
        (std::isalpha(static_cast<unsigned char>(q.bare[8])) || q.bare[8] == ' ')) {
        return std::nullopt;
    }
    sig.name = q.bare;
    sig.class_name = q.scope();  // the caller decides if this is a class or a namespace
    sig.ret = TypeRef{"TodoReturn"};
    return sig;
}

// ---- extraction ------------------------------------------------------------------------------------

static std::string strip_prefix(std::string text, std::string_view prefix) {
    if (text.starts_with(prefix)) text.erase(0, prefix.size());
    return text;
}

static bool is_ctor(FunctionSignature const& sig) {
    auto const last = cpp::split_qualified(sig.class_name).bare;
    return !last.empty() && sig.name == last;
}

ExtractedBinary extract_binary(ExtractInput const& input) {
    auto demangle = input.demangle ? input.demangle : itanium_demangler();

    struct Parsed {
        FunctionSignature sig;
        RawSymbol const* symbol;
        std::string mangled;
    };
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

    // constructors and destructors prove their scope is a class
    for (auto& p : parsed) {
        auto& sig = p.sig;
        if (sig.class_name.empty()) continue;
        if (is_ctor(sig) || sig.name.starts_with('~')) known_classes.insert(sig.class_name);
    }

    ExtractedBinary result;
    result.platform = input.platform;
    std::map<std::string, ExtractedClass> classes;
    std::set<std::string> seen;

    // lowest offset first so that C1/C2 and D0/D1/D2 variants collapse deterministically
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
            // a namespace-qualified free function
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

                // the vtable proves virtuality for functions declared in this class
                auto const key = sig->overload_key();
                for (auto& m : cls.methods | std::views::filter([&](auto& m) { return m.sig.overload_key() == key; })) {
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
