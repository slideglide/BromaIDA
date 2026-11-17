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

/// Calling conventions as IDA's CM_CC_* bits name them. Values match the SDK.
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

/// Everything about the open binary that importing or exporting depends on.
struct BinaryInfo {
    FileFormat format = FileFormat::Unknown;
    OperatingSystem os = OperatingSystem::Unknown;
    Architecture arch = Architecture::Unknown;
    unsigned pointer_bits = 0;
    std::optional<Platform> platform = std::nullopt;  ///< Empty when the binary isn't one Broma knows about.
    CallingConvention default_cc = CallingConvention::Unknown;

    [[nodiscard]] bool is_itanium_abi() const noexcept { return os != OperatingSystem::Windows; }
};

/// Reads the magic number.
[[nodiscard]] FileFormat detect_format(std::span<std::byte const> header) noexcept;

/// Maps OS and architecture to the Broma platform. Windows is always assumed 64-bit.
[[nodiscard]] std::optional<Platform> to_platform(
    OperatingSystem os, Architecture arch, unsigned pointer_bits) noexcept;

/// Fills in `default_cc` and `platform` from the other fields.
[[nodiscard]] BinaryInfo complete(BinaryInfo info) noexcept;

/// Inspects raw PE, Mach-O or ELF headers (including Mach-O load commands). std::nullopt if unrecognized.
[[nodiscard]] std::optional<BinaryInfo> inspect_header(std::span<std::byte const> header);

/// Same, for a file on disk.
[[nodiscard]] geode::Result<BinaryInfo, std::string> inspect_file(PathView file);

}  // namespace bromaida
