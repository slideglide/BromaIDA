#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <Geode/Result.hpp>

#include <bromaida/path.hpp>
#include <bromaida/platform.hpp>
#include <bromaida/profile.hpp>

namespace bromaida {
    /// One step of the generated header: a file copied in, or the Geode enums.
    struct BundleEntry {
        std::string file;
        std::string enums;      ///< Instead of `file`: where the Geode `Enums.hpp` is, relative to the bindings folder.
        std::string includes;   ///< "parse" inlines `#include "..."` lines, "filter" comments them out, "" leaves them.
        bool optional = false;  ///< Skip quietly if the file is missing.
    };

    struct BundleManifest {
        int format = 1;
        std::string name;
        std::string title;
        std::string description;
        std::vector<BundleEntry> headers;  ///< In the order they go into the generated header.
    };

    struct Bundle {
        BundleManifest manifest;
        Path dir;
    };

    /// Loads a bundle folder. Its `bundle.json` lists the headers for what Broma doesn't define (Cocos2d-x, FMOD, helpers).
    /// @param dir The bundle folder.
    /// @return The bundle, or an error message.
    [[nodiscard]] geode::Result<Bundle, std::string> load_bundle(PathView dir);

    struct FlavorManifest {
        int format = 1;
        std::string name;
        std::string title;
        std::string description;
        std::vector<std::string> provides;  ///< Every `std::` type the flavor declares.
    };

    /// A stand-in standard library with the `std::` layouts IDA needs.
    struct Flavor {
        FlavorManifest manifest;
        Path dir;
        std::set<std::string> provides;

        [[nodiscard]] bool has(std::string const& type) const { return provides.contains(type); }
    };

    /// @param dir A flavor folder holding `flavor.json` and the headers.
    /// @return The flavor, or an error message.
    [[nodiscard]] geode::Result<Flavor, std::string> load_flavor(PathView dir);

    /// Finds the `std::` types a flavor can't provide. IDAClang crashes on undeclared member types.
    /// @param used The `std::` types the bindings use.
    /// @param flavor The flavor to check against.
    /// @return The types in `used` that the flavor doesn't declare, sorted.
    [[nodiscard]] std::vector<std::string> missing_std_types(std::set<std::string> const& used, Flavor const& flavor);

    /// @param platform The target platform.
    /// @return The flavor name used when a profile says nothing or names one that isn't installed.
    [[nodiscard]] std::string_view default_flavor(Platform platform) noexcept;

    struct FlavorChoice {
        geode::Result<Flavor, std::string> flavor = geode::Err(std::string{});
        std::string requested;
        bool fell_back = false;  ///< `flavor` is the platform default because `requested` isn't installed.
    };

    /// Loads the profile's flavor for a platform, falling back to the platform default.
    /// @param profile The profile naming the flavor.
    /// @param platform The target platform.
    /// @param stl_root Folder holding the flavor folders (`<stl_root>/<name>`).
    /// @return The flavor and whether the default had to be used.
    [[nodiscard]] FlavorChoice resolve_flavor(Profile const& profile, Platform platform, PathView stl_root);
}  // namespace bromaida
