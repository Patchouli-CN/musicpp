#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::backend — MIDI 输出后端
//
// 抽象接口 + 平台实现。新增平台（ALSA / CoreMIDI / 文件导出）只需实现
// MidiOut 并在 create_default() 登记，解析器与引擎零改动。
// ════════════════════════════════════════════════════════════════════════════

#include <memory>
#include <stdexcept>

#include "log.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace musicpp::backend {

class MidiOut {
public:
    virtual ~MidiOut() = default;
    virtual void note_on(int channel, int midi, int velocity) = 0;
    virtual void note_off(int channel, int midi) = 0;
    virtual void program_change(int channel, int program) { (void)channel; (void)program; }
    virtual void all_notes_off() {}
};

// ── Windows: winmm ──────────────────────────────────────────────────────────
#ifdef _WIN32
class WinMmMidiOut final : public MidiOut {
public:
    WinMmMidiOut() {
        if (midiOutOpen(&handle_, 0, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            throw std::runtime_error("无法打开 MIDI 输出设备");
        // 系统计时器默认 ~15.6ms 粒度，sleep_until 会让快速音型抖动；
        // 设备存活期间提到 1ms（同步/协程/流式播放都经过后端，一处覆盖）
        timer_1ms_ = timeBeginPeriod(1) == TIMERR_NOERROR;
        log::midi.debug("MIDI 后端: winmm");
    }
    WinMmMidiOut(const WinMmMidiOut&) = delete;
    WinMmMidiOut& operator=(const WinMmMidiOut&) = delete;
    ~WinMmMidiOut() override {
        midiOutClose(handle_);
        if (timer_1ms_) timeEndPeriod(1);
    }

    void note_on(int channel, int midi, int velocity) override {
        midiOutShortMsg(handle_, 0x90 | channel | (midi << 8) | (velocity << 16));
    }
    void note_off(int channel, int midi) override {
        midiOutShortMsg(handle_, 0x80 | channel | (midi << 8));
    }
    void program_change(int channel, int program) override {
        midiOutShortMsg(handle_, 0xC0 | channel | (program << 8));
    }
    void all_notes_off() override {
        for (int ch = 0; ch < 16; ++ch)
            midiOutShortMsg(handle_, 0xB0 | ch | (123 << 16));   // All Notes Off
    }

private:
    HMIDIOUT handle_ = nullptr;
    bool timer_1ms_ = false;
};
#endif

// ── Null：无 MIDI 平台的占位（静默，只计数）────────────────────────────────
class NullMidiOut final : public MidiOut {
public:
    void note_on(int, int, int) override { ++count_; }
    void note_off(int, int) override {}
    size_t count() const { return count_; }
private:
    size_t count_ = 0;
};

inline std::unique_ptr<MidiOut> create_default() {
#ifdef _WIN32
    return std::make_unique<WinMmMidiOut>();
#else
    log::midi.warn("当前平台暂无 MIDI 后端，使用 Null 后端（只解析不发声）");
    return std::make_unique<NullMidiOut>();
#endif
}

} // namespace musicpp::backend
