#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::core — General MIDI 音色表
//
// instrument 指令的名称解析：支持中文别名、英文 GM 名（大小写/空格不敏感）、
// 裸数字（0~127 直接作为 program）。未知名称返回 nullopt，由调用方
// log warning 并回退标准音色（program 0 = 大钢琴）。
//
// 打击乐不走 program：GM 规定 channel 10（0 基 9）为打击乐专用通道，
// 通道分配见 score.hpp 的 channel_for_track()。
// ════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace musicpp::core {

// GM 标准音色表（0 基 program 号）
inline constexpr std::string_view GM_NAMES[128] = {
    // 0~7 钢琴
    "acoustic grand piano", "bright acoustic piano", "electric grand piano",
    "honky-tonk piano", "electric piano 1", "electric piano 2",
    "harpsichord", "clavinet",
    // 8~15 色彩打击
    "celesta", "glockenspiel", "music box", "vibraphone",
    "marimba", "xylophone", "tubular bells", "dulcimer",
    // 16~23 风琴
    "drawbar organ", "percussive organ", "rock organ", "church organ",
    "reed organ", "accordion", "harmonica", "tango accordion",
    // 24~31 吉他
    "acoustic guitar nylon", "acoustic guitar steel", "electric guitar jazz",
    "electric guitar clean", "electric guitar muted", "overdriven guitar",
    "distortion guitar", "guitar harmonics",
    // 32~39 贝斯
    "acoustic bass", "electric bass finger", "electric bass pick",
    "fretless bass", "slap bass 1", "slap bass 2", "synth bass 1", "synth bass 2",
    // 40~47 弦乐
    "violin", "viola", "cello", "contrabass",
    "tremolo strings", "pizzicato strings", "orchestral harp", "timpani",
    // 48~55 合奏
    "string ensemble 1", "string ensemble 2", "synth strings 1", "synth strings 2",
    "choir aahs", "voice oohs", "synth voice", "orchestra hit",
    // 56~63 铜管
    "trumpet", "trombone", "tuba", "muted trumpet",
    "french horn", "brass section", "synth brass 1", "synth brass 2",
    // 64~71 簧管
    "soprano sax", "alto sax", "tenor sax", "baritone sax",
    "oboe", "english horn", "bassoon", "clarinet",
    // 72~79 笛
    "piccolo", "flute", "recorder", "pan flute",
    "blown bottle", "shakuhachi", "whistle", "ocarina",
    // 80~87 合成主音
    "lead 1 square", "lead 2 sawtooth", "lead 3 calliope", "lead 4 chiff",
    "lead 5 charang", "lead 6 voice", "lead 7 fifths", "lead 8 bass lead",
    // 88~95 合成垫
    "pad 1 new age", "pad 2 warm", "pad 3 polysynth", "pad 4 choir",
    "pad 5 bowed", "pad 6 metallic", "pad 7 halo", "pad 8 sweep",
    // 96~103 合成效果
    "fx 1 rain", "fx 2 soundtrack", "fx 3 crystal", "fx 4 atmosphere",
    "fx 5 brightness", "fx 6 goblins", "fx 7 echoes", "fx 8 sci-fi",
    // 104~111 民族
    "sitar", "banjo", "shamisen", "koto",
    "kalimba", "bagpipe", "fiddle", "shanai",
    // 112~119 打击
    "tinkle bell", "agogo", "steel drums", "woodblock",
    "taiko drum", "melodic tom", "synth drum", "reverse cymbal",
    // 120~127 音效
    "guitar fret noise", "breath noise", "seashore", "bird tweet",
    "telephone ring", "helicopter", "applause", "gunshot",
};

// 中文别名 → program（常用乐器；未收录的名字回退英文表，再不行 warning）
inline const std::unordered_map<std::string, int>& chinese_aliases() {
    static const std::unordered_map<std::string, int> m = {
        {"钢琴", 0}, {"大钢琴", 0}, {"亮音钢琴", 1}, {"电钢琴", 4},
        {"羽管键琴", 6}, {"古钢琴", 6},
        {"钢片琴", 8}, {"钟琴", 9}, {"八音盒", 10}, {"音乐盒", 10},
        {"颤音琴", 11}, {"马林巴", 12}, {"木琴", 13}, {"管钟", 14}, {"扬琴", 15},
        {"风琴", 16}, {"教堂风琴", 19}, {"手风琴", 21}, {"口琴", 22},
        {"吉他", 24}, {"古典吉他", 24}, {"民谣吉他", 25},
        {"电吉他", 27}, {"失真吉他", 30},
        {"贝斯", 32}, {"电贝斯", 33}, {"合成贝斯", 38},
        {"小提琴", 40}, {"中提琴", 41}, {"大提琴", 42}, {"低音提琴", 43},
        {"拨弦", 45}, {"竖琴", 46}, {"定音鼓", 47},
        {"弦乐", 48}, {"弦乐合奏", 48}, {"合成弦乐", 50},
        {"合唱", 52}, {"人声", 53}, {"管弦乐齐奏", 55},
        {"小号", 56}, {"长号", 57}, {"大号", 58}, {"圆号", 60}, {"铜管", 61},
        {"萨克斯", 65}, {"高音萨克斯", 64}, {"中音萨克斯", 65},
        {"次中音萨克斯", 66}, {"上低音萨克斯", 67},
        {"双簧管", 68}, {"英国管", 69}, {"巴松", 70}, {"大管", 70}, {"单簧管", 71},
        {"短笛", 72}, {"长笛", 73}, {"竖笛", 74}, {"排箫", 75},
        {"尺八", 77}, {"哨笛", 78}, {"陶笛", 79},
        {"西塔琴", 104}, {"班卓琴", 105}, {"三味线", 106}, {"古筝", 107},
        {"卡林巴", 108}, {"风笛", 109}, {"木鱼", 115}, {"太鼓", 116},
        {"海浪", 122}, {"鸟鸣", 123}, {"掌声", 126},
    };
    return m;
}

// 名称 → program。裸数字按 0~127 program 号解析；查不到返回 nullopt
inline std::optional<int> instrument_program(std::string_view raw) {
    const auto b = raw.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) return std::nullopt;
    const auto e = raw.find_last_not_of(" \t\r");
    const std::string name(raw.substr(b, e - b + 1));

    if (!name.empty() && std::ranges::all_of(name, [](char c) { return c >= '0' && c <= '9'; })) {
        int v = 0;
        const auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), v);
        if (ec == std::errc{} && ptr == name.data() + name.size() && v >= 0 && v <= 127)
            return v;
        return std::nullopt;
    }
    if (const auto it = chinese_aliases().find(name); it != chinese_aliases().end())
        return it->second;

    // 英文：小写、去空白/连字符/下划线后比较
    auto normalize = [](std::string_view s) {
        std::string out;
        for (const char c : s)
            if (c != ' ' && c != '-' && c != '_')
                out += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        return out;
    };
    const std::string key = normalize(name);
    for (int i = 0; i < 128; ++i)
        if (normalize(GM_NAMES[i]) == key) return i;
    return std::nullopt;
}

} // namespace musicpp::core
