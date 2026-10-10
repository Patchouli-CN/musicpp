// api_demo — musicpp 的编程调用方式（非命令行）
#include <iostream>
#include <thread>

#include "musicpp/musicpp.hpp"

using namespace musicpp;

// ① 自定义后端：实现 MidiOut 即可接入任何东西（这里是打印而非发声）
class PrintMidiOut final : public backend::MidiOut {
public:
    void note_on(int channel, int midi, int velocity) override {
        std::cout << std::format("  ♪ ON  ch={} midi={} vel={}\n", channel, midi, velocity);
    }
    void note_off(int channel, int midi) override {
        std::cout << std::format("  ♪ OFF ch={} midi={}\n", channel, midi);
    }
    void program_change(int channel, int program) override {
        std::cout << std::format("  ♪ PC  ch={} program={}\n", channel, program);
    }
};

int main() {
    log::set_level(log::Level::debug);

    // ② 解析：IR 是你的数据结构，可以任意探查
    auto score = parse::build_from_file("demo/demo.music", 100);
    std::cout << std::format("声部数: {}\n", score.tracks.size());
    if (!score.tracks.empty() && !score.tracks[0].notes.empty()) {
        const auto& first = score.tracks[0].notes.front();
        std::cout << std::format("第一个音: midi={} @ {:.0f}ms\n",
                                 first.midi,
                                 core::ticks_to_ms(score.tempo, score.waits, first.tick));
    }

    // ③ 用自定义后端播放（这里只演示前几个音，真实场景换成你的后端）
    {
        PrintMidiOut printer;
        int count = 0;
        for (const auto& ev : score.tracks[0].notes) {
            if (++count > 4) break;
            printer.note_on(0, ev.midi, ev.velocity);
        }
    }

    // ④ BGM 式后台播放 + 随时中断
    {
        auto out = backend::create_default();
        engine::Engine bgm(*out);
        std::jthread bgm_thread([&] { bgm.play(score); });
        std::this_thread::sleep_for(std::chrono::seconds(3));
        log::app.info("3 秒到了，打断 BGM");
        bgm.stop_requested = true;   // 引擎在下一个消息点退出
    }

    log::app.info("演示结束");
    return 0;
}
