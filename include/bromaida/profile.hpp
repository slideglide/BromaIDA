#pragma once

#include <compare>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/path.hpp>
#include <bromaida/platform.hpp>

namespace bromaida {

/// A Geometry Dash version as the bindings folders name it. The minor part is always four digits, so
/// `2.209` (= 2.2090) sorts before `2.2091`, and `2.2081` before both.
struct GdVersion {
    int major = 0;
    int minor = 0;

    [[nodiscard]] static std::optional<GdVersion> parse(std::string_view text);
    /// Looks for a version in the last few components of a path: `bindings/2.2091` and `bindings/2.2091/Entry.bro`.
    [[nodiscard]] static std::optional<GdVersion> guess(PathView path);

    [[nodiscard]] std::string to_string() const;
    friend auto operator<=>(GdVersion const&, GdVersion const&) = default;
};

/// What differs between game versions: which bundle of SDK headers to include, which `#define`s to set before it,
/// and which STL flavor stands in for the game's standard library on each platform.
/// Loaded from JSON; unknown keys are ignored so that newer profile files keep working.
struct Profile {
    int format = 1;
    std::string name;
    std::string title;
    std::string min_version;  ///< Inclusive; empty = no lower bound.
    std::string max_version;  ///< Inclusive; empty = no upper bound.
    std::string sdk = "legacy";
    std::vector<std::string> defines;
    std::map<std::string, std::string> stl;  ///< Platform name ("win", "android64"...) -> flavor. "*" matches the rest.

    [[nodiscard]] bool matches(GdVersion version) const;
    /// The flavor this profile wants for a platform, or nullptr if it doesn't say.
    [[nodiscard]] std::string const* stl_flavor(Platform platform) const;
};

[[nodiscard]] geode::Result<Profile, std::string> load_profile(PathView file);

struct ProfileSet {
    std::vector<Profile> profiles;
    std::vector<std::string> errors;  ///< Files that couldn't be read; the rest still load.

    /// Adds `other`; a profile with the same name replaces the one already there (user profiles win over built-ins).
    void merge(ProfileSet other);
};

/// Loads every `*.json` in a folder. A missing folder is an empty set.
[[nodiscard]] ProfileSet load_profiles(PathView folder);

[[nodiscard]] Profile const* find_profile(std::span<Profile const> profiles, std::string_view name);
/// The matching profile with the highest lower bound, or nullptr.
[[nodiscard]] Profile const* select_profile(std::span<Profile const> profiles, GdVersion version);

}  // namespace bromaida
