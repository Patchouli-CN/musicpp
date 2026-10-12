#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::parse — 单行音符解析
//
//   把一行音符记号（1~7 0 [] {} ^ , # - _ . * % & |）解析为相对 tick 的
//   事件序列。不依赖 StreamBuilder；错误带行号与调用栈抛出 ScoreError。
// ════════════════════════════════════════════════════════════════════════════

#include <format>
#include <string>
#include <vector>

#include "../log.hpp"
#include "../pitch.hpp"
#include "../score.hpp"
#include "common.hpp"

namespace musicpp::parse::detail {

struct LineResult {
    std::vector<core::NoteEvent> events;
    int length_ticks = 0;
};

inline LineResult parse_line(std::string s, int velocity, int line_no,
                             const std::vector<CallFrame>& stack) {
    using core::TICK_UNIT;
    LineResult out;
    std::vector<int> nbuf;      // 待发声的 MIDI 音（0 = 休止占位）
    bool isChord = false;
    int ctn = TICK_UNIT;        // 当前记号累积的时值
    int tick = 0;

    auto bad = [&](const std::string& what) -> void {
        fail(std::format("{}: {}", what, trim(s)), line_no, stack);
    };

    s += ' ';  // 尾部补空格，保证最后一个记号也被结算
    const int n = (int)s.size();
    for (int i = 0; i < n; ++i) {
        const char c = s[i];
        switch (c) {
            case '[': case '{': {
                if (isChord) bad("和弦括号嵌套");
                isChord = true;
                break;
            }
            case ']': case '}': {
                if (!isChord) bad("括号不匹配");
                isChord = false;
                break;
            }
            case ' ': case '\t': {
                if (!isChord && !nbuf.empty()) {
                    for (const int note : nbuf) {
                        if (note > 0)
                            out.events.push_back({tick, note, velocity, ctn});
                    }
                    nbuf.clear();
                    tick += ctn;
                    ctn = TICK_UNIT;
                }
                break;
            }
            case '|': break;                      // 小节线：仅排版
            case '-': ctn += TICK_UNIT; break;    // 延音
            case '_': ctn /= 2; break;            // 分音
            case '*': ctn /= 3; break;
            case '%': ctn /= 5; break;
            case '&': ctn /= 7; break;
            case '.': ctn = ctn * 3 / 2; break;   // 附点
            case '0': nbuf.push_back(0); break;   // 休止符
            default: {
                if (c < '1' || c > '7') bad(std::format("非法字符 '{}'", c));
                const int x = (int)c - '1';
                int lvl = 3;   // 中央八度
                bool sharp = false;
                for (int j = i + 1; j < n; ++j) {
                    if (s[j] == '^') ++lvl;
                    else if (s[j] == ',') --lvl;
                    else if (s[j] == '#') sharp = true;
                    else break;
                    ++i;
                }
                if (lvl < 0 || lvl > 6) bad("八度超出范围");
                const int midi = core::midi_note(x, lvl, sharp);
                if (midi < 0) bad("音高超出 MIDI 范围");
                nbuf.push_back(midi);
                break;
            }
        }
    }
    if (isChord) bad("和弦括号未闭合");
    out.length_ticks = tick;
    log::trace("音符行: {} 事件, {} tick (第 {} 行)", out.events.size(), tick, line_no);
    return out;
}

} // namespace musicpp::parse::detail
