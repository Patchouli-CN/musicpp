#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::log — 内置日志
//
// 格式: [note] LEVEL / message
// 等级: TRACE DEBUG INFO WARN ERROR FATAL
// 使用: musicpp::log::info("解析完成: {} 个事件", n);
// ════════════════════════════════════════════════════════════════════════════

#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace musicpp::log {

enum class Level { trace, debug, info, warn, error, fatal };

namespace detail {

inline Level& current_level() {
    static Level lv = Level::info;
    return lv;
}

inline std::mutex& output_mutex() {
    static std::mutex m;
    return m;
}

// Windows 控制台一次性初始化：输出/输入切到 UTF-8 代码页（否则中文日志在
// 默认 GBK 控制台里乱码），并启用 ANSI 虚拟终端序列（颜色）。
// 重定向到文件时 GetConsoleMode 失败，自动跳过颜色开关，UTF-8 字节流不受影响。
inline void ensure_console() {
#ifdef _WIN32
    static const bool once = [] {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        if (HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            h && h != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(h, &mode))
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        return true;
    }();
    (void)once;
#endif
}

inline constexpr std::string_view level_name(Level lv) {
    switch (lv) {
        case Level::trace: return "TRACE";
        case Level::debug: return "DEBUG";
        case Level::info:  return "INFO";
        case Level::warn:  return "WARN";
        case Level::error: return "ERROR";
        case Level::fatal: return "FATAL";
    }
    return "?";
}

inline constexpr std::string_view level_color(Level lv) {
    switch (lv) {
        case Level::trace: return "\033[90m";   // gray
        case Level::debug: return "\033[34m";   // blue
        case Level::info:  return "\033[36m";   // cyan
        case Level::warn:  return "\033[33m";   // yellow
        case Level::error: return "\033[31m";   // red
        case Level::fatal: return "\033[35m";   // magenta
    }
    return "";
}

inline void emit(Level lv, std::string_view tag, std::string msg) {
    if (lv < current_level()) return;
    ensure_console();
    std::lock_guard lk(output_mutex());
    std::cout << level_color(lv)
              << std::format("[{}] {:<5} / {}", tag, level_name(lv), msg)
              << "\033[0m" << std::endl;
}

} // namespace detail

inline void set_level(Level lv) { detail::current_level() = lv; }
inline Level get_level() { return detail::current_level(); }

// 自由函数 = 解析模块的通道（[note] 本意：parse 进度笔记）
template <class... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::trace, "note", std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::debug, "note", std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::info, "note", std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::warn, "note", std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::error, "note", std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void fatal(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::fatal, "note", std::format(fmt, std::forward<Args>(args)...));
}

// 其他模块的标签实例：log::play.info(...)、log::midi.debug(...)
struct Tag {
    std::string_view name;
    template <class... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) const {
        detail::emit(Level::trace, name, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) const {
        detail::emit(Level::debug, name, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) const {
        detail::emit(Level::info, name, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) const {
        detail::emit(Level::warn, name, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) const {
        detail::emit(Level::error, name, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    void fatal(std::format_string<Args...> fmt, Args&&... args) const {
        detail::emit(Level::fatal, name, std::format(fmt, std::forward<Args>(args)...));
    }
};

inline constexpr Tag play{"play"};   // engine
inline constexpr Tag midi{"midi"};   // backend
inline constexpr Tag app{"app"};     // CLI 入口

} // namespace musicpp::log
