#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::core — Score IR（中间表示）
//
// 解析器的产物、播放引擎的输入。谱面的一切结构（段落、speed 事件、
// 双声部）在这里都被展平为：若干条并行 Track（tick 事件流）+ 一张
// 全局速度映射（tick → 毫秒/四分音符，分段线性）。
//
// 时间单位：每四分音符 TICK_UNIT tick，3360 = 2^5·3·5·7，
// 可被 2/3/5/7 及连续折半整除（分音号 _ * % & 的精确性来源）。
// ════════════════════════════════════════════════════════════════════════════

#include <vector>

namespace musicpp::core {

inline constexpr int TICK_UNIT = 3360;

struct NoteEvent {
    int tick = 0;        // 起始 tick（全曲时间轴）
    int midi = 0;        // MIDI 音高
    int velocity = 100;
    int dur_ticks = 0;   // 时值（articulated 模式下用于 note-off）
    bool artic = false;  // 该音断奏（noteoff on 时解析的音符）：时值结束发 note-off
};

struct TempoPoint {
    int tick = 0;
    double ms_per_quarter = 500.0;
};

// 墙钟停顿点：wait 指令产生。不占 tick，只为之后的时刻平移毫秒
struct WaitPoint {
    int tick = 0;
    double ms = 0.0;
};

// 绝对毫秒时刻的 MIDI 短消息：播放/流式输出的最小单元
struct TimedMsg {
    double t;
    uint32_t raw;
};

// 音色切换点：instrument 指令产生。track 从 tick 起改用 program（GM 音色号）
struct ProgChange {
    int tick = 0;
    size_t track = 0;
    int program = 0;
};

// 声部 → MIDI 通道：跳过 GM 打击乐专用通道 10（0 基 9）；超出 15 条回绕
inline constexpr int channel_for_track(size_t idx) {
    int ch = (int)(idx % 16);
    if (ch >= 9) ++ch;
    return ch;
}

struct Track {
    std::vector<NoteEvent> notes;
};

struct Score {
    std::vector<Track> tracks;          // 并行声部（通常 1~2 条）
    std::vector<TempoPoint> tempo;      // 速度映射，按 tick 升序
    std::vector<WaitPoint> waits;       // 墙钟停顿，按 tick 升序
    std::vector<ProgChange> programs;   // 音色切换，按解析顺序
    bool force_artic = false;           // 全局强制断奏（CLI --noteoff / --no-echo）
};

// tick → 毫秒：沿速度映射分段线性推进，再累加该 tick 之前的墙钟停顿
inline double ticks_to_ms(const std::vector<TempoPoint>& tempo,
                          const std::vector<WaitPoint>& waits, int tick) {
    double t = 0.0;
    int prev_tick = 0;
    double prev_mpq = tempo.empty() ? 500.0 : tempo.front().ms_per_quarter;
    for (const auto& p : tempo) {
        if (p.tick <= prev_tick) { prev_mpq = p.ms_per_quarter; continue; }
        if (p.tick >= tick) break;
        t += (double)(p.tick - prev_tick) / TICK_UNIT * prev_mpq;
        prev_tick = p.tick;
        prev_mpq = p.ms_per_quarter;
    }
    t += (double)(tick - prev_tick) / TICK_UNIT * prev_mpq;
    for (const auto& w : waits) {
        if (w.tick > tick) break;
        t += w.ms;
    }
    return t;
}

} // namespace musicpp::core
