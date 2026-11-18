#include <bromaida/binary_info.hpp>

#include <bit>
#include <cstring>
#include <vector>

namespace bromaida {
    template <class T>
    static std::optional<T> read_at(std::span<std::byte const> data, std::size_t offset, bool swap = false) {
        if (offset + sizeof(T) > data.size()) return std::nullopt;
        T value;
        std::memcpy(&value, data.data() + offset, sizeof(T));
        if (swap) value = std::byteswap(value);
        return value;
    }

    static constexpr std::uint32_t kMachMagic64 = 0xFEEDFACF;
    static constexpr std::uint32_t kMachMagic32 = 0xFEEDFACE;
    static constexpr std::uint32_t kMachCpuX86_64 = 0x01000007;
    static constexpr std::uint32_t kMachCpuArm64 = 0x0100000C;

    static constexpr std::uint32_t kLcVersionMinMacOS = 0x24;
    static constexpr std::uint32_t kLcVersionMinIPhoneOS = 0x25;
    static constexpr std::uint32_t kLcBuildVersion = 0x32;
    static constexpr std::uint32_t kPlatformMacOS = 1;
    static constexpr std::uint32_t kPlatformIOS = 2;

    static BinaryInfo inspect_pe(std::span<std::byte const> data) {
        BinaryInfo info{.format = FileFormat::PE, .os = OperatingSystem::Windows};
        auto const pe_offset = read_at<std::uint32_t>(data, 0x3C);
        if (!pe_offset) return info;
        auto const machine = read_at<std::uint16_t>(data, *pe_offset + 4);
        if (machine == 0x8664) {
            info.arch = Architecture::X86_64;
            info.pointer_bits = 64;
        } else if (machine == 0x014C) {
            info.arch = Architecture::X86;
            info.pointer_bits = 32;
        } else if (machine == 0xAA64) {
            info.arch = Architecture::Arm64;
            info.pointer_bits = 64;
        }
        return info;
    }

    static BinaryInfo inspect_macho(std::span<std::byte const> data) {
        BinaryInfo info{.format = FileFormat::MachO, .os = OperatingSystem::MacOS};
        auto const magic = read_at<std::uint32_t>(data, 0);
        if (!magic) return info;

        bool const is64 = *magic == kMachMagic64;
        info.pointer_bits = is64 ? 64 : 32;
        auto const cpu = read_at<std::uint32_t>(data, 4).value_or(0);
        if (cpu == kMachCpuX86_64)
            info.arch = Architecture::X86_64;
        else if (cpu == kMachCpuArm64)
            info.arch = Architecture::Arm64;

        std::size_t const header_size = is64 ? 32 : 28;
        auto const ncmds = read_at<std::uint32_t>(data, 16).value_or(0);
        std::size_t offset = header_size;
        for (std::uint32_t i = 0; i < ncmds; ++i) {
            auto const cmd = read_at<std::uint32_t>(data, offset);
            auto const size = read_at<std::uint32_t>(data, offset + 4);
            if (!cmd || !size || *size < 8) break;

            if (*cmd == kLcBuildVersion) {
                auto const platform = read_at<std::uint32_t>(data, offset + 8).value_or(0);
                if (platform == kPlatformIOS)
                    info.os = OperatingSystem::iOS;
                else if (platform == kPlatformMacOS)
                    info.os = OperatingSystem::MacOS;
                break;
            }
            if (*cmd == kLcVersionMinIPhoneOS) {
                info.os = OperatingSystem::iOS;
                break;
            }
            if (*cmd == kLcVersionMinMacOS) break;
            offset += *size;
        }
        return info;
    }

    static BinaryInfo inspect_elf(std::span<std::byte const> data) {
        BinaryInfo info{.format = FileFormat::ELF, .os = OperatingSystem::Android};
        auto const elf_class = read_at<std::uint8_t>(data, 4).value_or(0);
        info.pointer_bits = elf_class == 2 ? 64 : 32;

        auto const machine = read_at<std::uint16_t>(data, 18).value_or(0);
        switch (machine) {
            case 3: info.arch = Architecture::X86; break;
            case 62: info.arch = Architecture::X86_64; break;
            case 40: info.arch = Architecture::Arm32; break;
            case 183: info.arch = Architecture::Arm64; break;
            default: break;
        }
        return info;
    }

    FileFormat detect_format(std::span<std::byte const> header) noexcept {
        if (header.size() >= 2 && header[0] == std::byte{'M'} && header[1] == std::byte{'Z'}) return FileFormat::PE;
        if (header.size() >= 4 && header[0] == std::byte{0x7F} && header[1] == std::byte{'E'} &&
            header[2] == std::byte{'L'} && header[3] == std::byte{'F'}) {
            return FileFormat::ELF;
        }
        if (auto const magic = read_at<std::uint32_t>(header, 0);
            magic && (*magic == kMachMagic64 || *magic == kMachMagic32)) {
            return FileFormat::MachO;
        }
        return FileFormat::Unknown;
    }

    std::optional<Platform> to_platform(OperatingSystem os, Architecture arch, unsigned pointer_bits) noexcept {
        switch (os) {
            case OperatingSystem::Windows: return Platform::Windows;
            case OperatingSystem::MacOS:
                if (arch == Architecture::X86_64) return Platform::MacIntel;
                if (arch == Architecture::Arm64) return Platform::MacArm;
                return std::nullopt;
            case OperatingSystem::iOS: return arch == Architecture::Arm64 ? std::optional{Platform::iOS} : std::nullopt;
            case OperatingSystem::Android:
                if (pointer_bits == 32) return Platform::Android32;
                if (pointer_bits == 64) return Platform::Android64;
                return std::nullopt;
            case OperatingSystem::Linux:
            case OperatingSystem::Unknown: break;
        }
        return std::nullopt;
    }

    BinaryInfo complete(BinaryInfo info) noexcept {
        info.platform = to_platform(info.os, info.arch, info.pointer_bits);
        if (info.os == OperatingSystem::Windows && info.pointer_bits == 32) {
            info.default_cc = CallingConvention::Thiscall;
        } else {
            info.default_cc = CallingConvention::Unknown;
        }
        return info;
    }

    std::optional<BinaryInfo> inspect_header(std::span<std::byte const> header) {
        switch (detect_format(header)) {
            case FileFormat::PE: return complete(inspect_pe(header));
            case FileFormat::MachO: return complete(inspect_macho(header));
            case FileFormat::ELF: return complete(inspect_elf(header));
            case FileFormat::Unknown: break;
        }
        return std::nullopt;
    }

    geode::Result<BinaryInfo, std::string> inspect_file(PathView file) {
        auto opened = asp::fs::File::open(file);
        if (opened.isErr())
            return geode::Err(fmt::format("cannot open {}: {}", file.string(), opened.unwrapErr().message()));
        auto input = std::move(opened).unwrap();

        std::vector<std::byte> buffer(64 * 1024);
        auto const count = input.read({reinterpret_cast<std::uint8_t*>(buffer.data()), buffer.size()});
        if (count.isErr())
            return geode::Err(fmt::format("cannot read {}: {}", file.string(), count.unwrapErr().message()));
        buffer.resize(count.unwrap());

        if (auto info = inspect_header(buffer)) return geode::Ok(*info);
        return geode::Err(fmt::format("{} is not a PE, Mach-O or ELF file", file.string()));
    }
}  // namespace bromaida
