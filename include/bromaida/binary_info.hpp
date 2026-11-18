#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <Geode/Result.hpp>

#include <bromaida/path.hpp>
#include <bromaida/platform.hpp>

namespace bromaida {
    enum class FileFormat : unsigned char { Unknown, PE, MachO, ELF };
    enum class OperatingSystem : unsigned char { Unknown, Windows, MacOS, iOS, Android, Linux };
    enum class Architecture : unsigned char { Unknown, X86, X86_64, Arm32, Arm64 };

    /// IDA's CM_CC_* values.
    enum class CallingConvention : unsigned char {
        Invalid = 0x00,
        Unknown = 0x10,
        VoidArg = 0x20,
        Cdecl = 0x30,
        Ellipsis = 0x40,
        Stdcall = 0x50,
        Fastcall = 0x70,
        Thiscall = 0x80,
        Special = 0xF0,  // "usercall"
    };

    struct BinaryInfo {
        FileFormat format = FileFormat::Unknown;
        OperatingSystem os = OperatingSystem::Unknown;
        Architecture arch = Architecture::Unknown;
        unsigned pointer_bits = 0;
        std::optional<Platform> platform = std::nullopt;  ///< Empty when the binary isn't one Broma knows about.
        CallingConvention default_cc = CallingConvention::Unknown;

        [[nodiscard]] bool is_itanium_abi() const noexcept { return os != OperatingSystem::Windows; }
    };

    /// Identifies a binary's container format.
    /// @param header The first bytes of the file.
    /// @return PE, Mach-O or ELF, or Unknown.
    [[nodiscard]] FileFormat detect_format(std::span<std::byte const> header) noexcept;

    /// Maps an OS and architecture to a Broma platform. Windows is assumed 64-bit.
    /// @param pointer_bits 32 or 64; tells the Android builds apart.
    /// @return The platform, or nothing if Broma has none for this combination.
    [[nodiscard]] std::optional<Platform> to_platform(
        OperatingSystem os, Architecture arch, unsigned pointer_bits) noexcept;

    /// Fills in `default_cc` and `platform` from the other fields.
    /// @param info A BinaryInfo with `os`, `arch` and `pointer_bits` set.
    /// @return The same info with the derived fields filled in.
    [[nodiscard]] BinaryInfo complete(BinaryInfo info) noexcept;

    /// Reads PE, Mach-O or ELF headers.
    /// @param header The first bytes of the file.
    /// @return What the headers say, or nothing if the format isn't recognized.
    [[nodiscard]] std::optional<BinaryInfo> inspect_header(std::span<std::byte const> header);

    /// Same as inspect_header, for a file on disk.
    /// @param file Path to the binary.
    /// @return What the headers say, or an error message if the file can't be read or isn't recognized.
    [[nodiscard]] geode::Result<BinaryInfo, std::string> inspect_file(PathView file);
}  // namespace bromaida
