#pragma once

#include <asp/detail/Function.hpp>
#include <fmt/format.h>
#include <string>
#include <string_view>

namespace bromaida {
    enum class LogLevel : unsigned char { Debug, Info, Warning, Error };

    using LogSink = asp::MoveOnlyFunction<void(LogLevel, std::string_view component, std::string_view message)>;

    void set_log_sink(LogSink sink);
    void set_log_level(LogLevel minimum) noexcept;
    [[nodiscard]] LogLevel log_level() noexcept;

    namespace detail {
        void emit(LogLevel level, std::string_view component, std::string_view message);
    }

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
}
