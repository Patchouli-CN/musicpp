#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp — 模块化音乐谱面语言（.music++）
//
//   musicpp::log      日志          [note] LEVEL / message
//   musicpp::core     音高与 IR     Score / NoteEvent / TempoPoint
//   musicpp::parse    两趟解析器    build_from_string / build_from_file
//   musicpp::backend  MIDI 后端     MidiOut / winmm / Null
//   musicpp::engine   播放引擎      Engine::play(Score)
// ════════════════════════════════════════════════════════════════════════════

#include "log.hpp"
#include "error.hpp"
#include "version.hpp"
#include "pitch.hpp"
#include "instrument.hpp"
#include "score.hpp"
#include "async.hpp"
#include "parser.hpp"
#include "backend.hpp"
#include "engine.hpp"
#include "engine_async.hpp"
