#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::parse::detail — 解析器公共件
//
//   行/段落结构、trim、指令关键字表（全语言唯一登记处）、错误抛出。
//   加新指令语法时：关键字加进 DIRECTIVE_KEYWORDS，handler 注册到
//   StreamBuilder::DIRECTIVE_HANDLERS（parser.hpp），两处编译期对齐检查。
// ════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../error.hpp"
#include "../i18n.hpp"

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

[[noreturn]] inline void musicpp_seterror(const std::string& what, int line,
                              const std::vector<CallFrame>& stack) {
    throw ScoreError(what, line, stack);
}

// ── "did you mean" 建议（编译器式报错）──────────────────────────────────────
// UTF-8 码点级 Levenshtein 编辑距离（按字节算会把一个汉字算成 3 次编辑）
inline size_t edit_distance(std::string_view a, std::string_view b) {
    auto to_cp = [](std::string_view s) {
        std::vector<uint32_t> v;
        for (size_t i = 0; i < s.size();) {
            const unsigned char c = (unsigned char)s[i];
            if (c < 0x80) { v.push_back(c); ++i; }
            else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
                v.push_back(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2;
            } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
                v.push_back(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F));
                i += 3;
            } else if (i + 3 < s.size()) {
                v.push_back(((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) |
                            ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F));
                i += 4;
            } else { v.push_back(0xFFFD); ++i; }
        }
        return v;
    };
    const auto x = to_cp(a), y = to_cp(b);
    std::vector<size_t> prev(y.size() + 1), cur(y.size() + 1);
    for (size_t j = 0; j <= y.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= x.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= y.size(); ++j) {
            const size_t sub = prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1);
            cur[j] = std::min({sub, prev[j] + 1, cur[j - 1] + 1});
        }
        std::swap(prev, cur);
    }
    return prev[y.size()];
}

// 在候选表中找最像的名字：距离 ≤2 且不等于整词长度（完全不同不提）。
// 找到返回提示后缀（如 "，did you mean <副歌>？"），否则空串
inline std::string did_you_mean(std::string_view name,
                                const std::vector<std::string_view>& candidates) {
    size_t best = SIZE_MAX;
    std::string_view hit;
    for (const auto& c : candidates) {
        const size_t d = edit_distance(name, c);
        if (d < best) { best = d; hit = c; }
    }
    if (best == 0 || best > 2) return {};
    const auto cps = [](std::string_view s) {   // 粗略码点数（用于阈值）
        size_t n = 0;
        for (const char c : s) if (((unsigned char)c & 0xC0) != 0x80) ++n;
        return n;
    };
    if (best >= cps(hit)) return {};
    return i18n::tr("hint.did_you_mean", hit);
}

} // namespace musicpp::parse::detail
