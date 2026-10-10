#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::engine — 协程播放接口
//
// play_async: 与同步 Engine::play 同一条时间线，但每个消息点 co_await
// 挂起而非阻塞线程。多个 play_async 派生进同一 Scheduler 即为合奏。
// ════════════════════════════════════════════════════════════════════════════

#include <atomic>

#include "async.hpp"
#include "backend.hpp"
#include "engine.hpp"
#include "log.hpp"
#include "score.hpp"

namespace musicpp::engine {

inline async::task play_async(async::Scheduler& sched, backend::MidiOut& out,
                                    const core::Score& score,
                                    std::atomic<bool>* stop = nullptr) {
    const Timeline tl = build_timeline(score);
    log::play.info("开始播放(async): {} 音符, {} 消息, 时长 {:.1f}s, 模式 {}",
                   tl.notes, tl.msgs.size(), tl.hold_ms / 1000.0,
                   tl.articulate ? "articulated" : "sustained");

    double last = 0.0;
    for (const auto& m : tl.msgs) {
        if (stop && stop->load(std::memory_order_relaxed)) {
            log::play.warn("播放被中断");
            out.all_notes_off();
            co_return;
        }
        co_await async::sleep_for(sched, m.t - last);   // 挂起，循环线程可跑别的协程
        last = m.t;
        send(out, m.raw);
    }
    co_await async::sleep_for(sched, tl.hold_ms - last);   // 尾部保持
    log::play.info("播放结束(async)");
}

} // namespace musicpp::engine
