#pragma once

#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <string>

namespace bromaida {

/// What Broma says about a function on one platform.
enum class BindState : unsigned char {
    Unbound,  ///< No address known (Broma: -1).
    Inlined,  ///< Explicitly inlined (Broma: -2).
    Bound,    ///< Has an offset from the image base.
};

struct Bind {
    BindState state = BindState::Unbound;
    std::uint64_t offset = 0;

    [[nodiscard]] static constexpr Bind from_broma(std::ptrdiff_t raw) noexcept {
        if (raw == -2) return {BindState::Inlined, 0};
        if (raw < 0) return {};
        return {BindState::Bound, static_cast<std::uint64_t>(raw)};
    }

    [[nodiscard]] constexpr std::ptrdiff_t to_broma() const noexcept {
        switch (state) {
            case BindState::Bound: return static_cast<std::ptrdiff_t>(offset);
            case BindState::Inlined: return -2;
            case BindState::Unbound: break;
        }
        return -1;
    }

    [[nodiscard]] constexpr bool has_address() const noexcept { return state == BindState::Bound; }
    [[nodiscard]] constexpr bool is_inlined() const noexcept { return state == BindState::Inlined; }

    [[nodiscard]] std::string to_string() const {
        switch (state) {
            case BindState::Bound: return fmt::format("{:#x}", offset);
            case BindState::Inlined: return "inline";
            case BindState::Unbound: break;
        }
        return "unbound";
    }

    friend constexpr bool operator==(Bind const&, Bind const&) noexcept = default;
};

}  // namespace bromaida
