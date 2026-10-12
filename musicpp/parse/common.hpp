#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::parse::detail — 解析器公共件
//
//   行/段落结构、trim、指令关键字表（全语言唯一登记处）、错误抛出。
//   加新指令语法时：关键字加进 DIRECTIVE_KEYWORDS，handler 注册到
//   StreamBuilder::DIRECTIVE_HANDLERS（parser.hpp），两处编译期对齐检查。
// ════════════════════════════════════════════════════════════════════════════

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../error.hpp"

namespace musicpp::parse::detail {

struct SrcLine {
    std::string text;
    int no = 0;
};

struct Section {
    std::string name;
    int def_line = 0;
    std::vector<SrcLine> lines;
};

inline std::string_view trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r") - b + 1);
}

inline bool is_directive(std::string_view line, std::string_view kw) {
    return line == kw || (line.starts_with(kw) &&
           line.size() > kw.size() && (line[kw.size()] == ' ' || line[kw.size()] == '\t'));
}

// 全部指令关键字（唯一登记处；ensemble 单独走块收录，无 handler）
inline constexpr std::string_view DIRECTIVE_KEYWORDS[] = {
    "speed", "play", "noteoff", "echo", "wait",
    "timedelta", "import", "ensemble", "instrument",
};

inline bool is_any_directive(std::string_view line) {
    for (const std::string_view kw : DIRECTIVE_KEYWORDS)
        if (is_directive(line, kw)) return true;
    return false;
}

// play 的段落名参数: 接受 <名> 或裸名
inline std::string_view play_name(std::string_view arg) {
    if (arg.starts_with('<') && arg.ends_with('>') && arg.size() >= 3)
        return arg.substr(1, arg.size() - 2);
    return arg;
}

[[noreturn]] inline void fail(const std::string& what, int line,
                              const std::vector<CallFrame>& stack) {
    throw ScoreError(what, line, stack);
}

} // namespace musicpp::parse::detail
