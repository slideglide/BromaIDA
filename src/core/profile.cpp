#include <bromaida/files.hpp>
#include <bromaida/profile.hpp>

#include <algorithm>
#include <regex>

#include <asp/iter.hpp>
#include <fmt/format.h>
#include <glaze/glaze.hpp>

namespace bromaida {
    std::optional<GdVersion> GdVersion::parse(std::string_view text) {
        static std::regex const pattern(R"((\d+)\.(\d+))");
        std::string const owned{text};
        std::smatch m;
        if (!std::regex_search(owned, m, pattern)) return std::nullopt;

        std::string minor = m[2].str();
        if (minor.size() > 4) minor.resize(4);
        while (minor.size() < 4)
            minor += '0';
        return GdVersion{std::stoi(m[1].str()), std::stoi(minor)};
    }

    std::optional<GdVersion> GdVersion::guess(PathView path) {
        Path current = path.toPath();
        for (int depth = 0; depth < 3; ++depth) {
            if (auto name = current.filename()) {
                if (auto version = parse(*name)) return version;
            }
            auto parent = current.parent();
            if (!parent || parent->empty()) break;
            current = parent->toPath();
        }
        return std::nullopt;
    }

    std::string GdVersion::to_string() const {
        auto text = fmt::format("{:04}", minor);
        while (text.size() > 3 && text.back() == '0')
            text.pop_back();
        return fmt::format("{}.{}", major, text);
    }

    bool Profile::matches(GdVersion version) const {
        if (auto low = GdVersion::parse(min_version); low && version < *low) return false;
        if (auto high = GdVersion::parse(max_version); high && version > *high) return false;
        return true;
    }

    std::string const* Profile::stl_flavor(Platform platform) const {
        if (auto it = stl.find(std::string{to_string(platform)}); it != stl.end()) return &it->second;
        if (auto it = stl.find("*"); it != stl.end()) return &it->second;
        return nullptr;
    }

    geode::Result<Profile, std::string> load_profile(PathView file) {
        auto text = asp::fs::readToString(file);
        if (text.isErr())
            return geode::Err(fmt::format("cannot read {}: {}", file.string(), text.unwrapErr().message()));

        Profile profile;
        if (auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(profile, text.unwrap())) {
            return geode::Err(fmt::format("{}: {}", file.string(), glz::format_error(err, text.unwrap())));
        }
        if (profile.name.empty()) return geode::Err(fmt::format("{}: a profile needs a \"name\"", file.string()));

        auto const low = GdVersion::parse(profile.min_version);
        auto const high = GdVersion::parse(profile.max_version);
        if (!profile.min_version.empty() && !low) {
            return geode::Err(
                fmt::format("{}: \"min_version\" is not a version: {}", file.string(), profile.min_version));
        }
        if (!profile.max_version.empty() && !high) {
            return geode::Err(
                fmt::format("{}: \"max_version\" is not a version: {}", file.string(), profile.max_version));
        }
        if (low && high && *high < *low) {
            return geode::Err(fmt::format("{}: \"max_version\" is lower than \"min_version\"", file.string()));
        }
        return geode::Ok(std::move(profile));
    }

    void ProfileSet::merge(ProfileSet other) {
        for (auto& incoming : other.profiles) {
            auto existing = std::ranges::find(profiles, incoming.name, &Profile::name);
            if (existing != profiles.end()) {
                *existing = std::move(incoming);
            } else {
                profiles.push_back(std::move(incoming));
            }
        }
        for (auto& error : other.errors)
            errors.push_back(std::move(error));
    }

    ProfileSet load_profiles(PathView folder) {
        ProfileSet set;
        if (!folder.isDir()) return set;

        auto files = list_files(folder, "json");
        if (files.isErr()) {
            set.errors.push_back(files.unwrapErr());
            return set;
        }

        for (auto const& file : files.unwrap()) {
            auto loaded = load_profile(file);
            if (loaded.isErr()) {
                set.errors.push_back(loaded.unwrapErr());
            } else {
                set.profiles.push_back(std::move(loaded).unwrap());
            }
        }
        return set;
    }

    Profile const* find_profile(std::span<Profile const> profiles, std::string_view name) {
        auto it = std::ranges::find(profiles, name, &Profile::name);
        return it == profiles.end() ? nullptr : &*it;
    }

    Profile const* select_profile(std::span<Profile const> profiles, GdVersion version) {
        Profile const* best = nullptr;
        std::optional<GdVersion> best_low;
        for (auto const& profile : profiles) {
            if (!profile.matches(version)) continue;
            auto low = GdVersion::parse(profile.min_version).value_or(GdVersion{});
            if (!best || low > *best_low) {
                best = &profile;
                best_low = low;
            }
        }
        return best;
    }
}  // namespace bromaida
