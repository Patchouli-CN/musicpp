#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::engine — 播放引擎
//
// Score IR → (time_ms, midi_msg) 时间线 → chrono 精调调度。
// echo off（断奏）的音符时值结束发送 note-off；
// echo on（默认）音符自由混响——地灵殿效果。
//
// 时间线展开独立为 build_timeline()，同步引擎与协程引擎共用。
// ════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "backend.hpp"
#include "log.hpp"
#include "score.hpp"

namespace musicpp::engine {

// 时间线消息：t 毫秒时刻发送 raw（MIDI short message）
using TimelineMsg = core::TimedMsg;

struct Timeline {
    std::vector<TimelineMsg> msgs;
    size_t notes = 0;
    double total_ms = 0.0;   // 最后一条消息的时刻
    double hold_ms = 0.0;    // 最后一个音符的结束时刻（播放需保持到此）
    bool articulate = false;
};

// Score IR → 绝对毫秒时间线（排序规则：同一时刻 note-off < program change < note-on，
// 同音重触发干净、音色切换先于该时刻的音符生效）
inline int msg_rank(uint32_t raw) {
    switch (raw & 0xF0) {
        case 0x80: return 0;   // note-off
        case 0xC0: return 1;   // program change
        case 0x90: return 2;   // note-on
        default:   return 3;
    }
}

inline Timeline build_timeline(const core::Score& score) {
    Timeline tl;
    tl.articulate = score.force_artic;
    for (size_t ti = 0; ti < score.tracks.size(); ++ti) {
        const uint32_t ch = (uint32_t)core::channel_for_track(ti);
        for (const auto& ev : score.tracks[ti].notes) {
            const double t0 = core::ticks_to_ms(score.tempo, score.waits, ev.tick);
            tl.msgs.push_back({t0, 0x90u | ch | ((uint32_t)ev.midi << 8) | ((uint32_t)ev.velocity << 16)});
            const double t1 = core::ticks_to_ms(score.tempo, score.waits, ev.tick + ev.dur_ticks);
            tl.hold_ms = std::max(tl.hold_ms, t1);
            if (tl.articulate || ev.artic) {   // 全局强制 或 该音解析于 echo off 区间
                tl.msgs.push_back({t1, 0x80u | ch | ((uint32_t)ev.midi << 8)});
            }
            ++tl.notes;
        }
    }
    for (const auto& pc : score.programs) {
        const double t = core::ticks_to_ms(score.tempo, score.waits, pc.tick);
        tl.msgs.push_back({t, 0xC0u | ((uint32_t)core::channel_for_track(pc.track)) |
                              ((uint32_t)pc.program << 8)});
    }
    std::stable_sort(tl.msgs.begin(), tl.msgs.end(),
                     [](const TimelineMsg& a, const TimelineMsg& b) {
                         if (a.t != b.t) return a.t < b.t;
                         return msg_rank(a.raw) < msg_rank(b.raw);
                     });
    tl.total_ms = tl.msgs.empty() ? 0.0 : tl.msgs.back().t;
    tl.hold_ms = std::max(tl.hold_ms, tl.total_ms);
    return tl;
}

// 发送一条时间线消息（raw==0 为纯时刻标记：流式尾部的保持点，不发声）
inline void send(backend::MidiOut& out, uint32_t raw) {
    const int ch = (int)(raw & 0x0F);
    switch (raw & 0xF0) {
        case 0x90: out.note_on(ch, (raw >> 8) & 0xFF, (raw >> 16) & 0xFF); break;
        case 0x80: out.note_off(ch, (raw >> 8) & 0xFF); break;
        case 0xC0: out.program_change(ch, (raw >> 8) & 0xFF); break;
        default: break;
    }
}

class Engine {
public:
    explicit Engine(backend::MidiOut& out) : out_(out) {}

    // 置 true 后尽快停止播放（线程安全）
    std::atomic<bool> stop_requested{false};

    void play(const core::Score& score) {
        const Timeline tl = build_timeline(score);
        log::play.info("开始播放: {} 音符, {} 消息, 时长 {:.1f}s, 模式 {}",
                       tl.notes, tl.msgs.size(), tl.hold_ms / 1000.0,
                       tl.articulate ? "articulated" : "sustained");

        const auto start = std::chrono::steady_clock::now();
        for (const auto& m : tl.msgs) {
            if (stop_requested.load(std::memory_order_relaxed)) {
                log::play.warn("播放被中断");
                out_.all_notes_off();
                return;
            }
            std::this_thread::sleep_until(
                start + std::chrono::duration<double, std::milli>(m.t));
            send(out_, m.raw);
        }
        // 尾部保持：等最后一个音走完时值，否则它被设备关闭掐掉
        std::this_thread::sleep_until(
            start + std::chrono::duration<double, std::milli>(tl.hold_ms));
        log::play.info("播放结束");
    }

private:
    backend::MidiOut& out_;
};

} // namespace musicpp::engine
