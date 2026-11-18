#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include <ast.hpp>

namespace bromaida {
    enum class Platform : unsigned char {
        Windows,
        MacIntel,
        MacArm,
        iOS,
        Android32,
        Android64,
    };

    inline constexpr std::array kAllPlatforms{
        Platform::Windows, Platform::MacIntel,  Platform::MacArm,
        Platform::iOS,     Platform::Android32, Platform::Android64,
    };

    struct PlatformInfo {
        Platform id;
        std::string_view broma_name;    ///< Name used inside .bro files ("win", "imac", ...).
        std::string_view printable;     ///< Name shown to the user.
        std::string_view macro_suffix;  ///< BROMAIDA_PLATFORM_<suffix> in generated headers.
        std::string_view stl_dir;       ///< Folder inside c++stl/ holding the matching STL headers.
        std::string_view clang_target;  ///< Value of clang's -target for this platform.
        broma::Platform broma_flag;
    };

    inline constexpr std::array<PlatformInfo, 6> kPlatformTable{{
        {Platform::Windows, "win", "Windows", "WINDOWS", "windows", "x86_64-pc-win32", broma::Platform::Windows},
        {Platform::MacIntel, "imac", "macOS (Intel)", "INTEL_MACOS", "macho", "x86_64-apple-darwin",
         broma::Platform::MacIntel},
        {Platform::MacArm, "m1", "macOS (ARM)", "M1_MACOS", "macho", "arm64-apple-darwin", broma::Platform::MacArm},
        {Platform::iOS, "ios", "iOS", "IOS", "macho", "arm64-apple-darwin", broma::Platform::iOS},
        {Platform::Android32, "android32", "Android (32)", "ANDROID32", "android", "armv7-none-linux-androideabi",
         broma::Platform::Android32},
        {Platform::Android64, "android64", "Android (64)", "ANDROID64", "android", "aarch64-none-linux-android",
         broma::Platform::Android64},
    }};

    [[nodiscard]] constexpr PlatformInfo const& info(Platform p) noexcept {
        return kPlatformTable[static_cast<std::size_t>(p)];
    }

    [[nodiscard]] constexpr std::string_view to_string(Platform p) noexcept { return info(p).broma_name; }

    [[nodiscard]] constexpr std::optional<Platform> platform_from_string(std::string_view name) noexcept {
        for (auto const& entry : kPlatformTable) {
            if (entry.broma_name == name) return entry.id;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool is_macho(Platform p) noexcept {
        return p == Platform::MacIntel || p == Platform::MacArm || p == Platform::iOS;
    }

    [[nodiscard]] constexpr bool is_android(Platform p) noexcept {
        return p == Platform::Android32 || p == Platform::Android64;
    }

    /// Everything but Windows follows the Itanium C++ ABI.
    [[nodiscard]] constexpr bool is_itanium_abi(Platform p) noexcept { return p != Platform::Windows; }

    [[nodiscard]] constexpr bool is_64bit(Platform p) noexcept { return p != Platform::Android32; }

    /// True when `flags` (a Broma platform bitmask) contains `p`.
    [[nodiscard]] constexpr bool has_platform(broma::Platform flags, Platform p) noexcept {
        return (flags & info(p).broma_flag) != broma::Platform::None;
    }

    /// Offset stored in a Broma PlatformNumber for the given platform.
    [[nodiscard]] constexpr std::ptrdiff_t& offset_for(broma::PlatformNumber& n, Platform p) noexcept {
        switch (p) {
            case Platform::Windows: return n.win;
            case Platform::MacIntel: return n.imac;
            case Platform::MacArm: return n.m1;
            case Platform::iOS: return n.ios;
            case Platform::Android32: return n.android32;
            case Platform::Android64: return n.android64;
        }
        return n.win;
    }

    [[nodiscard]] constexpr std::ptrdiff_t offset_for(broma::PlatformNumber const& n, Platform p) noexcept {
        return offset_for(const_cast<broma::PlatformNumber&>(n), p);
    }
}  // namespace bromaida
