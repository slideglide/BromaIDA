#include <bromaida/broma_io.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include <asp/fs/std.hpp>
#include <asp/iter.hpp>
#include <broma.hpp>
#include <fmt/format.h>

#include <bromaida/platform.hpp>
#include <bromaida/text.hpp>

namespace bromaida {
    namespace detail {
        struct Edit {
            std::size_t begin, end;
            std::string text;
        };
    }  // namespace detail

    using namespace detail;

    static std::string join(std::vector<std::string> const& parts, std::string_view separator) {
        return asp::iter::from(parts).copied().join(separator);
    }

    static std::string platform_list(broma::Platform flags) {
        std::vector<std::string> names;
        if ((flags & broma::Platform::Mac) == broma::Platform::Mac) {
            names.emplace_back("mac");
            flags = flags & ~broma::Platform::Mac;
        }
        if ((flags & broma::Platform::Android) == broma::Platform::Android) {
            names.emplace_back("android");
            flags = flags & ~broma::Platform::Android;
        }
        for (auto p : asp::iter::from(kAllPlatforms).filter([&](Platform p) { return has_platform(flags, p); }))
            names.emplace_back(to_string(p));
        return join(names, ", ");
    }

    static std::string write_params(broma::FunctionProto const& proto) {
        auto parts = asp::iter::from(proto.args)
                         .map([](auto const& arg) {
                             auto const& [type, name] = arg;
                             return name.empty() ? type.name : fmt::format("{} {}", type.name, name);
                         })
                         .collect<std::vector<std::string>>();
        if (proto.is_variadic) parts.emplace_back("...");
        return join(parts, ", ");
    }

    static std::string quote(std::string_view s) { return fmt::format("\"{}\"", s); }

    geode::Result<broma::Root, std::string> load_broma(PathView entry) {
        auto std_entry = entry.toStdPath();
        if (std_entry.isErr())
            return geode::Err(fmt::format("invalid path {}: {}", entry.string(), std_entry.unwrapErr().message()));
        auto result = broma::parse_file(std_entry.unwrap());
        if (result.isOk()) return geode::Ok(std::move(result).unwrap());
        return geode::Err(join(result.unwrapErr().messages, "\n"));
    }

    static std::string lower_ascii(std::string_view text) {
        std::string out{text};
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    static void merge_roots(broma::Root& into, broma::Root&& from, std::vector<std::string>& duplicates) {
        std::unordered_map<std::string, std::size_t> class_index;
        for (auto const [i, cls] : asp::iter::enumerate(into.classes))
            class_index.emplace(cls.name, static_cast<std::size_t>(i));

        for (auto& cls : from.classes) {
            auto const [it, inserted] = class_index.emplace(cls.name, into.classes.size());
            if (inserted) {
                into.classes.push_back(std::move(cls));
            } else if (into.classes[it->second].source != cls.source) {
                duplicates.push_back(
                    fmt::format("{} ({} and {})", cls.name, into.classes[it->second].source, cls.source));
            }
        }

        auto function_key = [](broma::Function const& f) {
            return fmt::format("{}:{}:{}", f.source, f.line, f.prototype.name);
        };
        std::unordered_set<std::string> functions;
        for (auto const& f : into.functions)
            functions.insert(function_key(f));
        for (auto& f : from.functions) {
            if (functions.insert(function_key(f)).second) into.functions.push_back(std::move(f));
        }

        auto header_key = [](broma::Header const& h) { return h.source + ":" + h.name; };
        std::unordered_set<std::string> headers;
        for (auto const& h : into.headers)
            headers.insert(header_key(h));
        for (auto& h : from.headers) {
            if (headers.insert(header_key(h)).second) into.headers.push_back(std::move(h));
        }
    }

    geode::Result<BromaFolder, std::string> load_broma_folder(PathView folder) {
        if (!folder.isDir()) return geode::Err(fmt::format("'{}' is not a folder.", folder.string()));

        auto listing = asp::fs::iterDir(folder);
        if (listing.isErr()) {
            return geode::Err(fmt::format("cannot read '{}': {}", folder.string(), listing.unwrapErr().message()));
        }

        auto files = std::move(listing)
                         .unwrap()
                         .filter([](auto const& item) { return item.isOk(); })
                         .map([](auto&& item) { return std::move(item).unwrap(); })
                         .filter([](asp::fs::DirectoryEntry const& entry) {
                             auto extension = entry.path.extension();
                             return !entry.isDir() && extension && lower_ascii(*extension) == "bro";
                         })
                         .map([](asp::fs::DirectoryEntry const& entry) { return entry.path; })
                         .collect<std::vector<Path>>();
        if (files.empty()) return geode::Err(fmt::format("There are no .bro files in '{}'.", folder.string()));
        std::ranges::sort(files);

        BromaFolder result;
        auto entry = std::ranges::find_if(files, [](Path const& p) {
            auto name = p.filename();
            return name && lower_ascii(*name) == "entry.bro";
        });
        if (entry != files.end()) {
            auto loaded = load_broma(*entry);
            if (loaded.isErr()) return geode::Err(loaded.unwrapErr());
            result.root = std::move(loaded).unwrap();
            result.files = {*entry};
            result.used_entry = true;
            return geode::Ok(std::move(result));
        }

        for (auto const& file : files) {
            auto loaded = load_broma(file);
            if (loaded.isErr()) return geode::Err(loaded.unwrapErr());
            merge_roots(result.root, std::move(loaded).unwrap(), result.duplicates);
            result.files.push_back(file);
        }
        return geode::Ok(std::move(result));
    }

    geode::Result<broma::Root, std::string> parse_broma(
        std::string_view source, PathView include_base, std::string const& source_name) {
        auto result = broma::parse_string(source, include_base.toStdPath().unwrapOrDefault(), source_name);
        if (result.isOk()) return geode::Ok(std::move(result).unwrap());
        return geode::Err(join(result.unwrapErr().messages, "\n"));
    }

    std::string write_attributes(
        broma::Attributes const& attrs, std::string_view indent, broma::Attributes const* inherited,
        std::vector<broma::Superclass> const* superclasses) {
        std::string out;
        if (!attrs.docs.empty()) {
            for (auto const& line : cpp::split_lines(attrs.docs))
                out += fmt::format("{}/// {}\n", indent, line);
        }
        auto const own_links = inherited ? attrs.links & ~inherited->links : attrs.links;
        auto const own_missing = inherited ? attrs.missing & ~inherited->missing : attrs.missing;
        if (own_links != broma::Platform::None)
            out += fmt::format("{}[[link({})]]\n", indent, platform_list(own_links));
        if (own_missing != broma::Platform::None) {
            out += fmt::format("{}[[missing({})]]\n", indent, platform_list(own_missing));
        }
        std::vector<std::string> depends;
        for (auto const& dep : attrs.depends) {
            bool const from_base = superclasses && asp::iter::from(*superclasses).any([&](broma::Superclass const& b) {
                return b.name == dep;
            });
            bool const from_class = inherited && asp::iter::contains(inherited->depends, dep);
            if (!from_base && !from_class) depends.push_back(dep);
        }
        if (!depends.empty()) out += fmt::format("{}[[depends({})]]\n", indent, join(depends, ", "));
        if (!attrs.since.empty()) out += fmt::format("{}[[since({})]]\n", indent, quote(attrs.since));
        if (!attrs.renamed_from.empty()) {
            out += fmt::format("{}[[renamed_from({})]]\n", indent, join(attrs.renamed_from, ", "));
        }
        return out;
    }

    static constexpr auto kBindPlatforms = std::to_array<Platform>(
        {Platform::Windows, Platform::MacIntel, Platform::MacArm, Platform::iOS, Platform::Android32,
         Platform::Android64});

    std::string write_binds(broma::PlatformNumber const& binds) {
        std::vector<std::string> parts;
        for (auto p : kBindPlatforms) {
            auto const raw = offset_for(binds, p);
            if (raw == -1) continue;
            parts.push_back(fmt::format("{} {}", to_string(p), raw == -2 ? "inline" : fmt::format("{:#x}", raw)));
        }
        return join(parts, ", ");
    }

    std::string write_function(
        broma::MemberFunctionProto const& proto, broma::PlatformNumber const& binds, std::string_view body,
        std::string_view indent, broma::Attributes const* inherited) {
        std::string out = write_attributes(proto.attributes, indent, inherited);
        out += indent;
        if (proto.is_callback) out += "callback ";
        if (proto.is_static) out += "static ";
        if (proto.is_virtual) out += "virtual ";
        if (proto.type == broma::FunctionType::Normal) out += proto.ret.name + " ";
        out += fmt::format("{}({}){}", proto.name, write_params(proto), proto.is_const ? " const" : "");

        if (!body.empty()) {
            if (auto bound = write_binds(binds); !bound.empty()) out += " = " + bound;
            out += " " + std::string{body};
            return out;
        }
        if (auto bound = write_binds(binds); !bound.empty()) out += " = " + bound;
        return out + ";";
    }

    std::string write_free_function(broma::Function const& function) {
        auto const& proto = function.prototype;
        std::string out = write_attributes(proto.attributes);
        out += fmt::format("{} {}({})", proto.ret.name, proto.name, write_params(proto));
        if (auto bound = write_binds(function.binds); !bound.empty()) out += " = " + bound;
        if (!function.inner.empty()) return out + " " + function.inner;
        return out + ";";
    }

    std::string write_member(broma::MemberField const& member, std::string_view indent) {
        std::string out = write_attributes(member.attributes, indent);
        out += fmt::format(
            "{}{} {}{};", indent, member.type.name, member.name,
            member.count ? fmt::format("[{}]", member.count) : std::string{});
        return out;
    }

    std::string write_pad(broma::PadField const& pad, std::string_view indent) {
        std::vector<std::string> parts;
        for (auto p : kAllPlatforms) {
            if (auto const amount = offset_for(pad.amount, p); amount > 0) {
                parts.push_back(fmt::format("{} {:#x}", to_string(p), amount));
            }
        }
        return fmt::format("{}PAD = {};", indent, join(parts, ", "));
    }

    std::string write_field(broma::Field const& field, std::string_view indent, broma::Attributes const* inherited) {
        if (auto const* fn = field.get_as<broma::FunctionBindField>()) {
            return write_function(fn->prototype, fn->binds, fn->inner, indent, inherited);
        }
        if (auto const* member = field.get_as<broma::MemberField>()) return write_member(*member, indent);
        if (auto const* pad = field.get_as<broma::PadField>()) return write_pad(*pad, indent);
        if (auto const* inl = field.get_as<broma::InlineField>()) return fmt::format("{}{}", indent, inl->inner);
        return {};
    }

    std::string write_class_header(broma::Class const& cls) {
        std::string out = write_attributes(cls.attributes, "", nullptr, &cls.superclasses);
        out += "class " + cls.name;
        for (std::size_t i = 0; i < cls.superclasses.size(); ++i) {
            auto const& base = cls.superclasses[i];
            out += i == 0 ? " : " : ", ";
            if (base.platform != broma::Platform::All) out += platform_list(base.platform) + " ";
            out += base.name;
        }
        return out + " {";
    }

    std::string write_class(broma::Class const& cls) {
        std::string out = write_class_header(cls) + "\n";
        for (auto const& field : cls.fields) {
            out += write_field(field, "    ", &cls.attributes) + "\n";
        }
        return out + "}\n";
    }

    template <class Visit>
    static std::size_t scan_code(std::string_view text, std::size_t from, Visit&& visit) {
        for (std::size_t i = from; i < text.size(); ++i) {
            char const c = text[i];
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                while (i < text.size() && text[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                auto const close = text.find("*/", i + 2);
                i = close == std::string_view::npos ? text.size() : close + 1;
                continue;
            }
            if (c == '"' || c == '\'') {
                for (++i; i < text.size() && text[i] != c; ++i) {
                    if (text[i] == '\\') ++i;
                }
                continue;
            }
            if (!visit(i, c)) return i;
        }
        return text.size();
    }

    static bool is_ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':'; }

    static std::size_t line_start(std::string_view text, std::size_t pos) {
        auto const nl = text.rfind('\n', pos == 0 ? 0 : pos - 1);
        return nl == std::string_view::npos || pos == 0 ? 0 : nl + 1;
    }

    std::optional<ClassSpan> find_class(std::string_view text, std::string_view class_name) {
        std::size_t found = std::string_view::npos;
        int depth = 0;
        scan_code(text, 0, [&](std::size_t i, char c) {
            if (c == '{')
                ++depth;
            else if (c == '}')
                --depth;
            else if (
                depth == 0 && c == 'c' && text.compare(i, 6, "class ") == 0 && (i == 0 || !is_ident(text[i - 1]))) {
                auto rest = cpp::trim(text.substr(i + 6));
                if (rest.starts_with(class_name) &&
                    (rest.size() == class_name.size() || !is_ident(rest[class_name.size()]))) {
                    found = i;
                    return false;
                }
            }
            return true;
        });
        if (found == std::string_view::npos) return std::nullopt;

        ClassSpan span;
        span.begin = line_start(text, found);
        while (span.begin > 0) {
            auto const prev_begin = line_start(text, span.begin - 1);
            auto prev = cpp::trim(text.substr(prev_begin, span.begin - prev_begin));
            if (prev.starts_with("[[") || prev.starts_with("///"))
                span.begin = prev_begin;
            else
                break;
        }

        int brace = 0;
        bool opened = false;
        scan_code(text, found, [&](std::size_t i, char c) {
            if (c == '{') {
                if (!opened) {
                    opened = true;
                    span.header_end = i + 1;
                }
                ++brace;
            } else if (c == '}' && opened && --brace == 0) {
                span.body_end = i;
                span.end = i + 1;
                return false;
            }
            return true;
        });
        if (!opened || span.end == 0) return std::nullopt;
        return span;
    }

    std::vector<StatementSpan> split_statements(std::string_view text, ClassSpan const& span) {
        std::vector<StatementSpan> out;
        std::size_t begin = std::string_view::npos;
        int brace = 0;

        std::size_t i = span.header_end;
        scan_code(text.substr(0, span.body_end), i, [&](std::size_t pos, char c) {
            if (std::isspace(static_cast<unsigned char>(c))) return true;
            if (begin == std::string_view::npos) begin = pos;

            if (c == '{') {
                ++brace;
            } else if (c == '}') {
                if (--brace == 0) {
                    out.push_back({begin, pos + 1, std::string{text.substr(begin, pos + 1 - begin)}});
                    begin = std::string_view::npos;
                }
            } else if (c == ';' && brace == 0) {
                out.push_back({begin, pos + 1, std::string{text.substr(begin, pos + 1 - begin)}});
                begin = std::string_view::npos;
            }
            return true;
        });
        return out;
    }

    std::optional<std::string> patch_class(
        std::string_view text, std::string_view class_name, ClassPatch const& patch) {
        auto span = find_class(text, class_name);
        if (!span) return std::nullopt;
        auto statements = split_statements(text, *span);

        std::vector<Edit> edits;

        if (patch.new_header) edits.push_back({span->begin, span->header_end, *patch.new_header});

        for (auto const& r : patch.replacements) {
            if (r.statement >= statements.size()) continue;
            edits.push_back({statements[r.statement].begin, statements[r.statement].end, r.text});
        }

        if (!patch.additions.empty()) {
            std::string indent = "    ";
            if (!statements.empty()) {
                auto const ls = line_start(text, statements.back().begin);
                indent = std::string{text.substr(ls, statements.back().begin - ls)};
                if (indent.find_first_not_of(" \t") != std::string::npos) indent = "    ";
            }
            std::size_t const anchor = statements.empty() ? span->header_end : statements.back().end;
            std::string added;
            for (auto const& line : patch.additions)
                added += "\n" + indent + line;
            if (statements.empty()) added += "\n";
            edits.push_back({anchor, anchor, std::move(added)});
        }

        std::ranges::sort(edits, [](Edit const& a, Edit const& b) { return a.begin > b.begin; });
        std::string result{text};
        for (auto const& e : edits)
            result.replace(e.begin, e.end - e.begin, e.text);
        return result;
    }
}  // namespace bromaida
