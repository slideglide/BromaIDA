#include <bromaida/log.hpp>

#include <atomic>
#include <cstdio>
#include <mutex>

namespace bromaida {

static std::atomic<LogLevel> g_level{LogLevel::Info};
static std::mutex g_mutex;
static LogSink g_sink;

static constexpr char prefix_for(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return '-';
        case LogLevel::Info: return '+';
        case LogLevel::Warning: return '!';
        case LogLevel::Error: return '!';
    }
    return '?';
}

void set_log_sink(LogSink sink) {
    std::scoped_lock lock{g_mutex};
    g_sink = std::move(sink);
}

void set_log_level(LogLevel minimum) noexcept { g_level.store(minimum, std::memory_order_relaxed); }

LogLevel log_level() noexcept { return g_level.load(std::memory_order_relaxed); }

namespace detail {

void emit(LogLevel level, std::string_view component, std::string_view message) {
    std::scoped_lock lock{g_mutex};
    if (g_sink) {
        g_sink(level, component, message);
        return;
    }
    std::fprintf(
        stderr, "[%c] %.*s: %.*s\n", prefix_for(level), static_cast<int>(component.size()), component.data(),
        static_cast<int>(message.size()), message.data());
}

}  // namespace detail
}  // namespace bromaida
