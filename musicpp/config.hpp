#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::config — 运行时全局配置（inline 变量，随时可改，改即生效）
//
// 目前是一组解析上限，防 Billion Laughs 攻击：play 是宏展开语义，
// DAG 式段落引用（L1{play L0 ×2}、L2{play L1 ×2}…）可让 40 行谱面
// 指数膨胀出 2^40 个事件。展开不防环（另有 call_stack）也不防深（另有
// 64 层限制），只限总量——正常谱面离默认值有几个数量级的距离。
//
// 用法（编程调用时）：
//   musicpp::config::max_events = 100'000'000;   // 放宽
//   auto score = musicpp::parse::build_from_file("huge.music");
// ════════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>

namespace musicpp::config {

// 展开处理的内容行总数上限（音符行/指令行各计 1）。防"无音符的展开风暴"
//（例如每层只写 speed 的 2^n 嵌套——不产生事件但烧 CPU 与 tempo 表内存）
inline size_t max_expansion_steps = 10'000'000;

// 展开后的音符事件总数上限（NoteEvent ≈ 20 字节，默认 ≈ 200MB 封顶）
inline size_t max_events = 10'000'000;

// 全曲总时长上限（tick，TICK_UNIT = 3360/四分音符）。
// 默认 1e8 ≈ 29760 个四分音符（bpm 120 下约 4 小时）。
// IR 的 tick 是 int32，有效上限会被钳制到 int32 范围
//（≈ 63.9 万四分音符，bpm 120 下约 89 小时），超出此值的配置按 int32 上限计
inline int64_t max_ticks = 100'000'000;

} // namespace musicpp::config
