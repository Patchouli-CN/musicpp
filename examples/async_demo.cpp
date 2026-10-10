// async_demo — 协程播放：同一 Scheduler 里两首歌错开 1.5s，卡农效果
#include "musicpp/musicpp.hpp"
#include "musicpp/engine_async.hpp"

using namespace musicpp;

async::task canon(async::Scheduler& loop, backend::MidiOut& out,
                        const core::Score& score, double delay_ms, int id) {
    if (delay_ms > 0) {
        log::app.info("声部 {} 延迟 {:.0f}ms 进入", id, delay_ms);
        co_await async::sleep_for(loop, delay_ms);
    }
    co_await engine::play_async(loop, out, score);
}

int main() {
    try {
        auto score = parse::build_from_file("demo/demo.music");
        auto out = backend::create_default();

        async::Scheduler loop;
        loop.spawn(canon(loop, *out, score, 0, 1));
        loop.spawn(canon(loop, *out, score, 1500, 2));   // 晚 1.5 秒进入
        log::app.info("两个任务已派生，事件循环启动");
        loop.run();
        log::app.info("全部任务完成");
    } catch (const std::exception& e) {
        log::app.fatal("{}", e.what());
        return 1;
    }
    return 0;
}
