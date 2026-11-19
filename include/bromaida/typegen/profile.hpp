#pragma once

#include <compare>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/util/path.hpp>
#include <bromaida/platform/platform.hpp>

namespace bromaida {
    struct GdVersion {
        int major = 0;
        int minor = 0;

        [[nodiscard]] static std::optional<GdVersion> parse(std::string_view text);

        [[nodiscard]] static std::optional<GdVersion> guess(PathView path);

        [[nodiscard]] std::string to_string() const;
        friend auto operator<=>(GdVersion const&, GdVersion const&) = default;
    };

    struct Profile {
        int format = 1;
        std::string name;
        std::string title;
        std::string min_version;
        std::string max_version;
        std::string sdk = "legacy";
        std::vector<std::string> defines;
        std::map<std::string, std::string> stl;

        [[nodiscard]] bool matches(GdVersion version) const;

        [[nodiscard]] std::string const* stl_flavor(Platform platform) const;
    };

    [[nodiscard]] geode::Result<Profile, std::string> load_profile(PathView file);

    struct ProfileSet {
        std::vector<Profile> profiles;
        std::vector<std::string> errors;

        void merge(ProfileSet other);
    };

    [[nodiscard]] ProfileSet load_profiles(PathView folder);

    [[nodiscard]] Profile const* find_profile(std::span<Profile const> profiles, std::string_view name);

    [[nodiscard]] Profile const* select_profile(std::span<Profile const> profiles, GdVersion version);
}
