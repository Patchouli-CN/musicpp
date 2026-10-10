#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::core — 音高模型
//
// 简谱 1~7 → MIDI 音高。中央八度（无高低音号）为 C4~B4（MIDI 60~71），
// 与简谱惯例一致。等音处理遵循乐理：3# = 4（E#=F），7# = 高八度 1（B#=C）。
// ════════════════════════════════════════════════════════════════════════════

namespace musicpp::core {

// 1~7 在自然大调上的半音偏移（C D E F G A B）
inline constexpr int SEMITONE[7] = {0, 2, 4, 5, 7, 9, 11};

inline constexpr int MIDI_MIN = 21;   // A0
inline constexpr int MIDI_MAX = 108;  // C8

// x: 0~6（do~si）, lvl: 0~6（C1~C7 八度）, sharp: 升半音
// 返回 MIDI 音高；越界返回 -1（由调用方报错，附上原始记号）
inline int midi_note(int x, int lvl, bool sharp) {
    int v = 24 + lvl * 12 + SEMITONE[x] + (sharp ? 1 : 0);
    if (v < MIDI_MIN || v > MIDI_MAX) return -1;
    return v;
}

} // namespace musicpp::core
