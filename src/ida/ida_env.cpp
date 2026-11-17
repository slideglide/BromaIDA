#include "ida_env.hpp"

#include <array>
#include <cstddef>
#include <vector>

#include <bromaida/log.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// IDA's headers poison libc functions and define macros; keep them after everything else.
#include <pro.h>
#include <auto.hpp>
#include <bytes.hpp>
#include <ida.hpp>
#include <idp.hpp>
#include <kernwin.hpp>
#include <loader.hpp>
#include <nalt.hpp>
#include <srclang.hpp>
#include <segment.hpp>

namespace bromaida::ida {

BinaryInfo read_binary_info() {
    bool const is64 = inf_is_64bit();

    switch (inf_get_filetype()) {
        case f_PE: {
            BinaryInfo info{
                .format = FileFormat::PE,
                .os = OperatingSystem::Windows,
                .arch = is64 ? Architecture::X86_64 : Architecture::X86,
                .pointer_bits = is64 ? 64u : 32u};
            return complete(info);
        }
        case f_MACHO: {
            // IDA keeps the Mach-O header and load commands in a "HEADER" segment.
            if (auto* seg = get_segm_by_name("HEADER")) {
                auto const size = static_cast<std::size_t>(std::min<asize_t>(seg->size(), 64 * 1024));
                std::vector<std::byte> bytes(size);
                if (get_bytes(bytes.data(), static_cast<ssize_t>(size), seg->start_ea) > 0) {
                    if (auto info = inspect_header(bytes)) return *info;
                }
            }
            return {.format = FileFormat::MachO};
        }
        case f_ELF: {
            BinaryInfo info{
                .format = FileFormat::ELF,
                .os = OperatingSystem::Android,  // GD only ships ELFs on Android
                .pointer_bits = is64 ? 64u : 32u};
            switch (PH.id) {
                case PLFM_ARM: info.arch = is64 ? Architecture::Arm64 : Architecture::Arm32; break;
                case PLFM_386: info.arch = is64 ? Architecture::X86_64 : Architecture::X86; break;
                default: break;
            }
            return complete(info);
        }
        default: return {};
    }
}

std::string root_filename() {
    char name[QMAXPATH];
    auto const length = get_root_filename(name, sizeof(name));
    if (length <= 0) return {};
    return {name, static_cast<std::size_t>(length)};
}

bool analysis_finished() { return auto_is_ok(); }

bool is_gui() { return is_idaq(); }

std::optional<std::string> select_clang_parser() {
    for (char const* name :
         IDA_SDK_VERSION >= 920 ? std::array{"old_clang", "clang"} : std::array{"clang", "old_clang"}) {
        if (select_parser_by_name(name)) return std::string{name};
    }
    return std::nullopt;
}

Path plugin_data_dir() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&plugin_data_dir), &module);
    wchar_t buffer[MAX_PATH * 4];
    auto const length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
    return parent_dir(Path::fromWide({buffer, length})) / "bromaida";
#else
    Dl_info dl{};
    if (dladdr(reinterpret_cast<void const*>(&plugin_data_dir), &dl) == 0 || dl.dli_fname == nullptr) return {};
    return parent_dir(Path{dl.dli_fname}) / "bromaida";
#endif
}

void install_log_sink() {
    set_log_sink([](LogLevel level, std::string_view component, std::string_view message) {
        char const* tag = "[+]";
        switch (level) {
            case LogLevel::Debug: tag = "[.]"; break;
            case LogLevel::Info: tag = "[+]"; break;
            case LogLevel::Warning: tag = "[!]"; break;
            case LogLevel::Error: tag = "[x]"; break;
        }
        msg("%s %.*s: %.*s\n", tag, static_cast<int>(component.size()), component.data(),
            static_cast<int>(message.size()), message.data());
    });
}

}  // namespace bromaida::ida
