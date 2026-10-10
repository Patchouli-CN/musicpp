// stream_demo — 流式解析 + 协程播放：边喂行边出声
//
// 生产者协程逐行喂谱（sleep 模拟慢速来源：AI 生成 / 网络流 / 实时弹奏），
// 播放协程从 channel 消费时间线消息。谱子还没喂完，音乐已经响了。
#include <fstream>

#include "musicpp/musicpp.hpp"

using namespace musicpp;

async::task producer(parse::StreamBuilder& sb, async::Scheduler& loop,
                     const std::string& path, async::channel<engine::TimelineMsg>& ch) {
    std::ifstream in(path);
    if (!in) { log::app.fatal("无法打开文件: {}", path); ch.close(); co_return; }
    std::string line;
    while (std::getline(in, line)) {
        sb.feed_line(line);
        co_await async::sleep_for(loop, 200);   // 模拟慢速生产者
    }
    sb.finish();
    ch.close();
    log::app.info("生产者: 全部喂完，关闭 channel");
}

async::task player(backend::MidiOut& out, async::Scheduler& loop,
                   async::channel<engine::TimelineMsg>& ch) {
    double last = 0.0;
    size_t n = 0;
    for (;;) {
        auto msg = co_await ch.pop();
        if (!msg) break;
        co_await async::sleep_for(loop, msg->t - last);
        last = msg->t;
        engine::send(out, msg->raw);
        ++n;
    }
    log::app.info("播放协程: 消费 {} 消息，结束", n);
}

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "demo/demo.music";
    try {
        parse::StreamBuilder sb(100, std::filesystem::path(path).parent_path().string());
        async::channel<engine::TimelineMsg> ch;
        sb.set_sink([&ch](std::vector<engine::TimelineMsg> msgs) {
            for (auto& m : msgs) ch.push(std::move(m));
        });

        auto out = backend::create_default();
        async::Scheduler loop;
        loop.spawn(producer(sb, loop, path, ch));
        loop.spawn(player(*out, loop, ch));
        loop.run();
    } catch (const musicpp::ScoreError& e) {
        log::app.error("{}", e.what());
        return 2;
    } catch (const std::exception& e) {
        log::app.fatal("{}", e.what());
        return 1;
    }
    return 0;
}
