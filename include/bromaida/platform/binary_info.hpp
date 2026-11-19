#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <bromaida/platform/platform.hpp>

namespace bromaida {
    enum class FileFormat : unsigned char { Unknown, PE, MachO, ELF };
    enum class OperatingSystem : unsigned char { Unknown, Windows, MacOS, iOS, Android };
    enum class Architecture : unsigned char { Unknown, X86, X86_64, Arm32, Arm64 };

    enum class CallingConvention : unsigned char {
        Invalid = 0x00,
        Unknown = 0x10,
        VoidArg = 0x20,
        Cdecl = 0x30,
        Ellipsis = 0x40,
        Stdcall = 0x50,
        Fastcall = 0x70,
        Thiscall = 0x80,
        Special = 0xF0,
    };

    struct BinaryInfo {
        FileFormat format = FileFormat::Unknown;
        OperatingSystem os = OperatingSystem::Unknown;
        Architecture arch = Architecture::Unknown;
        unsigned pointer_bits = 0;
        std::optional<Platform> platform = std::nullopt;
        CallingConvention default_cc = CallingConvention::Unknown;

        [[nodiscard]] bool is_itanium_abi() const noexcept { return os != OperatingSystem::Windows; }
    };

    [[nodiscard]] FileFormat detect_format(std::span<std::byte const> header) noexcept;

    [[nodiscard]] std::optional<Platform> to_platform(
        OperatingSystem os, Architecture arch, unsigned pointer_bits) noexcept;

    [[nodiscard]] BinaryInfo complete(BinaryInfo info) noexcept;

    [[nodiscard]] std::optional<BinaryInfo> inspect_header(std::span<std::byte const> header);
}
