#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp — 版本与构建信息
//
// 语义化版本：语言新增指令/语法 → minor；行为不兼容变更 → major（1.0 前
// 允许 minor 承载）。改这里，--version 与日志自动跟随。
// ════════════════════════════════════════════════════════════════════════════

#include <string>
#include <string_view>

namespace musicpp {

inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 7;
inline constexpr int VERSION_PATCH = 0;
inline constexpr std::string_view VERSION = "0.7.0";

inline constexpr std::string_view COMPILER =
#if defined(__clang__)
    "clang " __VERSION__;
#elif defined(_MSC_VER)
    "msvc";
#elif defined(__GNUC__)
    "g++ " __VERSION__;
#else
    "unknown compiler";
#endif

inline constexpr std::string_view CPP_STD =
#if __cplusplus >= 202302L
    "C++23";
#elif __cplusplus >= 202002L
    "C++20";
#else
    "C++17";
#endif

// "musicpp 0.6.0 (g++ 16.1.0, C++23, built Oct 10 2026 21:00:00)"
inline std::string version_banner() {
    return "musicpp " + std::string(VERSION) + " (" + std::string(COMPILER) +
           ", " + std::string(CPP_STD) + ", built " __DATE__ " " __TIME__ ")";
}

} // namespace musicpp
