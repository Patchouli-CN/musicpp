#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp — 模块化音乐谱面语言（.music++）
//
//   musicpp::log      日志          [note] LEVEL / message
//   musicpp::core     音高与 IR     Score / NoteEvent / TempoPoint
//   musicpp::parse    统一流式解析器 build_from_string / build_from_file / StreamBuilder
//   musicpp::i18n     报错国际化    lang/<语言>.txt 键值对覆盖，MUSICPP_LANG 选择
//   musicpp::backend  MIDI 后端     MidiOut / winmm / Null
//   musicpp::engine   播放引擎      Engine::play(Score)
// ════════════════════════════════════════════════════════════════════════════

#include "log.hpp"
#include "error.hpp"
#include "version.hpp"
#include "config.hpp"
#include "pitch.hpp"
#include "instrument.hpp"
#include "score.hpp"
#include "async.hpp"
#include "i18n.hpp"
#include "parser.hpp"
#include "backend.hpp"
#include "engine.hpp"
#include "engine_async.hpp"
