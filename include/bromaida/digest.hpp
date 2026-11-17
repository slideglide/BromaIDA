#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace bromaida {

/// Stable 128-bit content digest (two FNV-1a lanes) as hex. Only meant for "did the input change?" checks;
/// the length of every part is mixed in so that {"ab","c"} and {"a","bc"} differ.
[[nodiscard]] inline std::string digest_hex(std::initializer_list<std::string_view> parts) {
    std::uint64_t lanes[2] = {0xcbf29ce484222325ULL, 0x84222325cbf29ce4ULL};
    auto mix = [&](unsigned char byte) {
        lanes[0] = (lanes[0] ^ byte) * 0x100000001b3ULL;
        lanes[1] = (lanes[1] ^ static_cast<unsigned char>(~byte)) * 0x1000193ULL * 0x100000001b3ULL;
    };
    for (auto const part : parts) {
        for (int shift = 0; shift < 64; shift += 8)
            mix(static_cast<unsigned char>(part.size() >> shift));
        for (char const c : part)
            mix(static_cast<unsigned char>(c));
    }
    return fmt::format("{:016x}{:016x}", lanes[0], lanes[1]);
}

}  // namespace bromaida
