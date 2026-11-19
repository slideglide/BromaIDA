#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/util/path.hpp>
#include <bromaida/platform/platform.hpp>
#include <bromaida/typegen/profile.hpp>

namespace bromaida {

    struct BundleEntry {
        std::string file;
        std::string enums;
        std::string includes;
        bool optional = false;
    };

    struct BundleManifest {
        int format = 1;
        std::string name;
        std::string title;
        std::string description;
        std::vector<BundleEntry> headers;
    };

    struct Bundle {
        BundleManifest manifest;
        Path dir;
    };

    [[nodiscard]] geode::Result<Bundle, std::string> load_bundle(PathView dir);

    struct FlavorManifest {
        int format = 1;
        std::string name;
        std::string title;
        std::string description;
        std::vector<std::string> provides;
    };

    struct Flavor {
        FlavorManifest manifest;
        Path dir;
        std::set<std::string> provides;

        [[nodiscard]] bool has(std::string const& type) const { return provides.contains(type); }
    };

    [[nodiscard]] geode::Result<Flavor, std::string> load_flavor(PathView dir);

    [[nodiscard]] std::vector<std::string> missing_std_types(std::set<std::string> const& used, Flavor const& flavor);

    [[nodiscard]] std::string_view default_flavor(Platform platform) noexcept;

    struct FlavorChoice {
        geode::Result<Flavor, std::string> flavor = geode::Err(std::string{});
        std::string requested;
        bool fell_back = false;
    };

    [[nodiscard]] FlavorChoice resolve_flavor(Profile const& profile, Platform platform, PathView stl_root);
}
