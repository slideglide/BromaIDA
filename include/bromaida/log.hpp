#pragma once

#include <fmt/format.h>
#include <functional>
#include <string>
#include <string_view>

namespace bromaida {

enum class LogLevel : unsigned char { Debug, Info, Warning, Error };

[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

/// Receives every record that passes the minimum level.
/// The IDA plugin installs a sink that writes to the Output window,
/// tests leave the default one that writes to stderr.
using LogSink = std::function<void(LogLevel, std::string_view component, std::string_view message)>;

/// Replaces the global sink. Passing an empty function restores the default (stderr).
void set_log_sink(LogSink sink);
void set_log_level(LogLevel minimum) noexcept;
[[nodiscard]] LogLevel log_level() noexcept;

namespace detail {
void emit(LogLevel level, std::string_view component, std::string_view message);
}

/// A named logger. Create one per class/module: `Log log{"ClassGraph"};`
class Log {
public:
    explicit constexpr Log(std::string_view component) noexcept
        : m_component(component) {}

    template <class... Args>
    void debug(fmt::format_string<Args...> f, Args&&... args) const {
        write(LogLevel::Debug, f, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(fmt::format_string<Args...> f, Args&&... args) const {
        write(LogLevel::Info, f, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warn(fmt::format_string<Args...> f, Args&&... args) const {
        write(LogLevel::Warning, f, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(fmt::format_string<Args...> f, Args&&... args) const {
        write(LogLevel::Error, f, std::forward<Args>(args)...);
    }

private:
    template <class... Args>
    void write(LogLevel level, fmt::format_string<Args...> f, Args&&... args) const {
        if (level < log_level()) return;
        detail::emit(level, m_component, fmt::format(f, std::forward<Args>(args)...));
    }

    std::string_view m_component;
};

}  // namespace bromaida
