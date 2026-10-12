#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::i18n — 报错信息国际化
//
//   键值对目录：内置中文（zh-CN）为默认；可用语言文件覆盖：
//     <exe目录>/lang/<语言>.txt     每行  key = value，# 注释，UTF-8
//   语言选择：环境变量 MUSICPP_LANG（如 en / zh-CN），默认 zh-CN。
//
//   消息中的 {} 占位符走 std::vformat；带参数的消息里的花括号字面量
//   要写成 {{ }} 转义（与 std::format 规则一致）。
//   无参数的消息不经过 vformat，花括号随意。
//
//   库用户不调用 init 也能工作（内置中文目录兜底）。
// ════════════════════════════════════════════════════════════════════════════

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#include "log.hpp"

namespace musicpp::i18n {

// 内置目录（zh-CN 默认）。语言文件逐键覆盖这里的值。
inline std::unordered_map<std::string, std::string>& catalog() {
    static std::unordered_map<std::string, std::string> m = {
        // ── 限制 / 防护 ──
        {"error.limit.expansion_steps", "展开规模超出上限（{} 行，Billion Laughs 防护；可用 musicpp::config::max_expansion_steps 调整）"},
        {"error.limit.events",          "音符事件总数超出上限（{}，Billion Laughs 防护；可用 musicpp::config::max_events 调整）"},
        {"error.limit.ticks",           "全曲总时长超出上限（{} tick；可用 musicpp::config::max_ticks 调整）"},
        // ── 段落 / 结构 ──
        {"error.section.nested",        "段落定义不能嵌套或出现在此处"},
        {"error.section.duplicate",     "段落 <{}> 重复定义"},
        {"error.section.unclosed",      "段落 <{}> 的定义未闭合（缺少 '}}'）"},
        {"error.section.not_ready",     "段落 <{}> 正在解析中，尚不能 play（要求先闭合后引用）"},
        {"error.section.undefined",     "未定义（或尚未解析到）的段落 <{}>"},
        {"error.section.cycle",         "段落引用形成环: {}"},
        {"error.section.too_deep",      "段落嵌套过深（>64 层）"},
        {"error.brace.orphan",          "孤立的 '}'（没有匹配的段落定义）"},
        {"error.ensemble.format",       "ensemble 指令格式应为 ensemble {{: {}"},
        {"error.ensemble.unclosed",     "ensemble block 未闭合（缺少 '}'）"},
        {"error.ensemble.member_play",  "ensemble 成员必须是 play 指令（成员内的 speed/wait 请改用音符时值表达）"},
        // ── 速度 ──
        {"error.tempo.range",           "速度超出范围: {}"},
        {"error.speed.missing_value",   "speed 指令缺少数值: {}"},
        {"error.speed.bpm_needs_number","bpm 需要数值（timedelta 是毫秒量纲，请用于 speed milisec）: {}"},
        {"error.speed.bad_number",      "速度数值无法解析: {}"},
        {"error.speed.bpm_range",       "bpm 超出范围: {}"},
        {"error.speed.unknown_unit",    "未知速度单位 '{}'（支持 bpm / milisec）"},
        // ── play / 移调 ──
        {"error.play.format",           "play 指令格式应为 play 名 或 play <名>: {}"},
        {"error.play.transpose_format", "play 移调参数格式应为 名(+N)/名(-N): {}"},
        {"error.play.transpose_sign",   "play 移调参数需要带符号的整数（半音数）: {}"},
        {"error.play.transpose_range",  "移调超出范围（±36 半音）: {}"},
        {"error.play.transpose_pitch",  "移调 {:+d} 后音高超出 MIDI 范围（{}）"},
        // ── import ──
        {"error.import.top_level_only", "import 只能在顶层使用"},
        {"error.import.format",         "import 指令格式应为 import \"路径\": {}"},
        {"error.import.cycle",          "import 形成环: {}"},
        {"error.import.not_found",      "import 的文件不存在: {}"},
        // ── 音色 / 回音 ──
        {"error.instrument.missing",    "instrument 指令缺少乐器名: {}"},
        {"warn.instrument.unknown",     "未知乐器 '{}'（第 {} 行），回退标准音色 [钢琴]"},
        {"error.echo.arg",              "echo 指令参数应为 on/off: {}"},
        {"error.noteoff.arg",           "noteoff 指令参数应为 on/off: {}"},
        // ── 时间 ──
        {"error.wait.missing",          "wait 指令缺少时间值: {}"},
        {"error.timedelta.in_section",  "timedelta 声明不能出现在段落内（请移到顶层）"},
        {"error.timedelta.format",      "timedelta 声明格式应为 timedelta <名> = <时间值>: {}"},
        {"error.timedelta.missing_eq",  "timedelta 声明缺少 '=': {}"},
        {"error.timedelta.bad_name",    "timedelta 名不合法: {}"},
        {"error.timedelta.duplicate",   "timedelta '{}' 重复声明（常量的值不可变）"},
        {"error.time.missing_number",   "时间值缺少数值: {}"},
        {"error.time.bad_number",       "时间数值无法解析: {}"},
        {"error.time.unknown_unit",     "未知时间单位 '{}'（支持 ms/s/m/h/d）: {}"},
        {"error.time.unit_order",       "时间单位应按从大到小书写（如 1m30s，而非 {}）"},
        {"error.time.range",            "时间值超出范围: {}"},
        {"error.time.undefined_var",    "未定义的 timedelta '{}'"},
        // ── 音符行 ──
        {"error.chord.nested",          "和弦括号嵌套"},
        {"error.chord.mismatch",        "括号不匹配"},
        {"error.chord.unclosed",        "和弦括号未闭合"},
        {"error.line.illegal_char",     "非法字符 '{}'"},
        {"error.pitch.octave_range",    "八度超出范围"},
        {"error.pitch.midi_range",      "音高超出 MIDI 范围"},
        // ── 文件 / 提示 ──
        {"error.file.open",             "无法打开文件: {}"},
        {"hint.did_you_mean",           "，你的意思是 <{}> 吗？"},
    };
    return m;
}

// 加载语言文件：<dir>/<lang>.txt，逐键覆盖内置目录。文件不存在返回 false。
inline bool load(const std::string& dir, const std::string& lang) {
    const std::string fn = dir + "/" + lang + ".txt";
    const std::filesystem::path fsp(
        std::u8string(reinterpret_cast<const char8_t*>(fn.data()), fn.size()));
    std::ifstream in(fsp, std::ios::binary);
    if (!in) return false;
    std::string raw;
    for (std::string line; std::getline(in, line);) {
        const auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos || line[b] == '#') continue;
        const auto eq = line.find('=', b);
        if (eq == std::string::npos) continue;
        const auto eb = line.find_last_not_of(" \t\r", eq - 1);
        const auto vb = line.find_first_not_of(" \t", eq + 1);
        const auto ve = line.find_last_not_of(" \t\r");
        if (vb == std::string::npos) continue;
        catalog()[line.substr(b, eb - b + 1)] = line.substr(vb, ve - vb + 1);
    }
    return true;
}

// 初始化：lang 来自 MUSICPP_LANG 环境变量（默认 zh-CN，即内置目录）；
// exe_dir 为可执行文件所在目录，语言文件在 <exe_dir>/lang/ 下
inline void init(const std::string& exe_dir) {
    const char* env = std::getenv("MUSICPP_LANG");
    const std::string lang = (env && *env) ? env : "zh-CN";
    if (lang == "zh-CN" || lang == "zh") return;   // 内置中文
    if (!load(exe_dir + "/lang", lang))
        log::app.warn("语言文件不存在: lang/{}.txt（回退内置中文）", lang);
}

// 取消息：无参数直接返回模式串（不经过 vformat，花括号字面量安全）；
// 有参数走 std::vformat（模式中的花括号字面量需 {{ }} 转义）
template <class... Args>
inline std::string tr(const std::string& key, Args&&... args) {
    auto& m = catalog();
    const auto it = m.find(key);
    if (it == m.end()) return key;
    if constexpr (sizeof...(Args) == 0)
        return it->second;
    else {
        // make_format_args 只收左值：复制进 tuple 再展开
        auto tup = std::make_tuple(std::forward<Args>(args)...);
        return std::apply([&](auto&... a) {
            return std::vformat(it->second, std::make_format_args(a...));
        }, tup);
    }
}

} // namespace musicpp::i18n
