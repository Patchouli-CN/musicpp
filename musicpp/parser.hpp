#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::parse — 统一流式解析器
//
//   StreamBuilder：增量喂行，同时产出两种形态
//     1. Score IR（take_score）：tracks / tempo / waits 随解析增量生长，
//        批处理 = 喂完全部行再取 IR（build_from_string/build_from_file）
//     2. TimedMsg 流（set_sink）：每组音符解析完成立即按绝对毫秒时刻推送，
//        流式播放 = sink 接协程 channel 边解析边出声
//
//   统一语言规则：先定义后引用——段落闭合（'}' 出现）之前不可 play。
//
// 语言要素:
//   音符行      1~7 0 [] {} ^ , # - _ . * % & |（与原 .music 格式兼容）
//   速度指令    speed bpm <n> | speed milisec <n>   （流内事件，可变速）
//   段落        <名> { ... }                         （定义沉默）
//   引用        play <名> / play 名(+N)              （状态快照/恢复，禁环；±N 半音移调）
//   音色        instrument <名字|program号>          （GM 音色，未知回退钢琴）
//   回音        echo on|off                          （on=连奏混响(默认) off=断奏）
//   兼容        裸数字行 = speed milisec；#noteoff / noteoff on = echo off；# 注释
//
// 子模块（musicpp/parse/）:
//   common.hpp    行/段落结构、trim、指令关键字表（唯一登记处）、错误抛出
//   time.hpp      时间字面量与 timedelta 求值（纯函数，expected 返回）
//   line.hpp      单行音符解析（相对 tick 事件序列）
//   sections.hpp  段落头判定、import 模块段落切分
//
// 加新指令语法：
//   1. parse/common.hpp 的 DIRECTIVE_KEYWORDS 加关键字
//   2. StreamBuilder::DIRECTIVE_HANDLERS 注册 handler
//   （两处由 static_assert 编译期对齐检查）
// ════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "error.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include "instrument.hpp"
#include "log.hpp"
#include "pitch.hpp"
#include "score.hpp"
#include "parse/common.hpp"
#include "parse/time.hpp"
#include "parse/line.hpp"
#include "parse/sections.hpp"

namespace musicpp::parse {

using detail::SrcLine;
using detail::Section;

// ════════════════════════════════════════════════════════════════════════════
// StreamBuilder —— 唯一解析器
// ════════════════════════════════════════════════════════════════════════════
class StreamBuilder {
public:
    using Sink = std::function<void(std::vector<core::TimedMsg>)>;

    explicit StreamBuilder(int velocity = 100, std::string base_dir = ".")
        : velocity_(velocity), base_dir_(std::move(base_dir)) {
        score_.tempo.push_back({0, 500.0});   // 初始速度点
    }

    void set_sink(Sink s) { sink_ = std::move(s); }

    // ── 运行时时钟：速度映射 + 墙钟停顿的增量版 ────────────────────────────
    struct RunningClock {
        int base_tick = 0;
        double base_ms = 0.0;
        double mpq = 500.0;
        std::vector<std::pair<int, double>> waits;   // (tick, ms)，base_tick 之后

        double at(int tick) const {
            double t = base_ms + (double)(tick - base_tick) / core::TICK_UNIT * mpq;
            for (const auto& [wt, wm] : waits) {
                if (wt > tick) break;
                t += wm;
            }
            return t;
        }
        void add_wait(int tick, double ms) { waits.push_back({tick, ms}); }
        void set_tempo(int tick, double new_mpq) {
            base_ms = at(tick);   // 旧速度结算到当前 tick
            base_tick = tick;
            waits.clear();
            mpq = new_mpq;
        }
    };

    void feed_line(std::string_view raw) {
        ++line_no_;
        const std::string_view line = detail::trim(raw);

        // 段落收录中：所有行进入段落体，直到 '}'（此时段落才可被 play）
        if (collecting_) {
            if (line == "}") {
                const std::string name = collecting_->name;
                register_section(name, std::move(*collecting_), line_no_);
                log::trace("段落 <{}> block 收录 {} 行（已可引用）", name, collecting_->lines.size());
                collecting_.reset();
            } else {
                collecting_->lines.push_back({std::string(line), line_no_});
            }
            return;
        }

        if (line.empty()) { flush_pending(); return; }
        if (line.starts_with('#')) {   // 注释 / 旧版指令
            if (line == "#noteoff") {
                artic_ = true;
                log::debug("echo off (兼容 #noteoff)");
            }
            return;
        }
        if (auto sec = detail::section_start(line)) {
            flush_pending();
            if (sections_.contains(*sec))
                musicpp_seterror(i18n::tr("error.section.duplicate", *sec), line_no_);
            collecting_ = Section{std::move(*sec), line_no_, {}};
            log::debug("正在解析 <{}> block (第 {} 行)", collecting_->name, line_no_);
            return;
        }
        if (line == "}" && !collecting_ensemble_)
            musicpp_seterror(i18n::tr("error.brace.orphan"), line_no_);

        handle_content_line({std::string(line), line_no_});
    }

    void finish() {
        flush_pending();
        if (collecting_)
            musicpp_seterror(i18n::tr("error.section.unclosed", collecting_->name),
                 collecting_->def_line);
        if (collecting_ensemble_)
            musicpp_seterror(i18n::tr("error.ensemble.unclosed"), line_no_);
        // 尾部保持标记（raw==0）：让消费方等到最后一个音走完时值
        if (sink_ && last_end_ > 0) sink_({{last_end_, 0u}});
        log::debug("解析收尾: {} 段落, {} 行", sections_.size(), line_no_);
    }

    // 取出累积的 Score IR（finish 之后调用）
    core::Score take_score() {
        size_t events = 0;
        for (const auto& t : score_.tracks) events += t.notes.size();
        log::info("解析完成: {} 声部, {} 事件, {} 段落",
                  score_.tracks.size(), events, sections_.size());
        return std::move(score_);
    }

    const core::Score& score() const { return score_; }

private:
    int velocity_;
    int program_ = 0;      // 当前音色（instrument 指令）
    int transpose_ = 0;    // 当前移调（play 参数的累加，半音数）
    bool artic_ = false;   // 当前断奏状态（echo off / noteoff on）
    size_t steps_ = 0;         // 已展开处理的内容行（Billion Laughs 防护）
    size_t total_events_ = 0;  // 已产出的音符事件总数（同上）
    int line_no_ = 0;
    int tick_ = 0;
    RunningClock clock_;
    Sink sink_;
    std::string base_dir_;

    core::Score score_;                                            // 增量生长的 IR
    std::optional<Section> collecting_;                            // 收录中的段落
    bool collecting_ensemble_ = false;                             // 收录中的 ensemble block
    std::vector<SrcLine> ensemble_members_;
    std::optional<SrcLine> pending_;                               // 配对缓冲（一行）
    std::unordered_map<std::string, Section> sections_;            // 已闭合段落
    std::unordered_map<std::string, double> timedeltas_;
    std::vector<std::string> import_stack_;
    std::unordered_set<std::string> imported_;
    std::vector<std::string> call_stack_;
    std::vector<CallFrame> frames_;
    size_t track_base_ = 0;                                        // 声部分配基准
    std::unordered_map<std::string, size_t> ensemble_tracks_;      // 段落名 → 声部基准
    std::vector<int> track_program_;                               // 声部当前音色（增量跟踪）
    double last_end_ = 0.0;                                        // 最后音符的结束时刻（ms）

    [[noreturn]] void musicpp_seterror(const std::string& what, int line) {
        throw ScoreError(what, line, frames_);
    }

    void register_section(std::string qname, Section s, int line) {
        if (!sections_.emplace(qname, std::move(s)).second)
            musicpp_seterror(std::format("段落 <{}> 重复定义", qname), line);
    }

    void set_tempo(double mpq, int line) {
        if (mpq <= 0 || mpq > 60000)
            musicpp_seterror(i18n::tr("error.tempo.range", mpq), line);
        clock_.set_tempo(tick_, mpq);
        if (!score_.tempo.empty() && score_.tempo.back().tick == tick_)
            score_.tempo.back().ms_per_quarter = mpq;   // 同 tick 覆盖
        else
            score_.tempo.push_back({tick_, mpq});
        log::trace("速度变更: {} ms/四分音符 @ tick {}", mpq, tick_);
    }

    // ── 内容行分发（顶层与段落体共用）──────────────────────────────────────
    void handle_content_line(SrcLine sl) {
        const std::string_view line = detail::trim(sl.text);
        if (++steps_ > config::max_expansion_steps)
            musicpp_seterror(i18n::tr("error.limit.expansion_steps", config::max_expansion_steps), sl.no);
        if (collecting_ensemble_) {   // ensemble block 收录中
            if (line == "}") {
                collecting_ensemble_ = false;
                auto members = std::move(ensemble_members_);
                ensemble_members_.clear();
                exec_ensemble(members, sl.no);
            } else {
                ensemble_members_.push_back(sl);
            }
            return;
        }
        if (line.empty()) { flush_pending(); return; }
        if (line.starts_with('#')) {   // 段落体内的注释（与顶层一致）
            if (line == "#noteoff") {
                flush_pending();
                artic_ = true;
                log::debug("echo off (兼容 #noteoff)");
            }
            return;
        }
        if (line.starts_with('<') || detail::section_start(line))
            musicpp_seterror(i18n::tr("error.section.nested"), sl.no);
        if (detail::is_directive(line, "ensemble")) {
            flush_pending();
            if (!line.ends_with('{'))
                musicpp_seterror(i18n::tr("error.ensemble.format", line), sl.no);
            collecting_ensemble_ = true;
            return;
        }
        if (detail::is_any_directive(line)) {
            flush_pending();
            directive(line, sl.no);
            return;
        }
        if (try_legacy_tempo(line, sl.no)) { flush_pending(); return; }

        // 音符行：配对缓冲（双声部需要看下一行，最多延迟一行）
        if (!pending_) {
            pending_ = std::move(sl);
            return;
        }
        play_group({&*pending_, &sl});
        pending_.reset();
    }

    void flush_pending() {
        if (!pending_) return;
        const SrcLine* solo = &*pending_;
        play_group({solo});
        pending_.reset();
    }

    void play_group(std::vector<const SrcLine*> group) {
        const int start = tick_;
        std::vector<core::TimedMsg> msgs;
        int group_len = 0;
        for (size_t t = 0; t < group.size(); ++t) {
            auto res = detail::parse_line(group[t]->text, velocity_, group[t]->no, frames_);
            group_len = std::max(group_len, res.length_ticks);
            total_events_ += res.events.size();
            if (total_events_ > config::max_events)
                musicpp_seterror(i18n::tr("error.limit.events", config::max_events), group[t]->no);
            const size_t track_idx = track_base_ + t;
            if (score_.tracks.size() <= track_idx) score_.tracks.resize(track_idx + 1);
            if (track_program_.size() <= track_idx) track_program_.resize(track_idx + 1, 0);
            if (!res.events.empty() && track_program_[track_idx] != program_) {
                // 该声部音色变化：IR 记 ProgChange，流式输出立即推 program change
                track_program_[track_idx] = program_;
                score_.programs.push_back({start, track_idx, program_});
                if (sink_) sink_({{clock_.at(start),
                                  0xC0u | ((uint32_t)core::channel_for_track(track_idx)) |
                                  ((uint32_t)program_ << 8)}});
            }
            for (const auto& ev : res.events) {
                const int tick = start + ev.tick;
                const uint32_t ch = (uint32_t)core::channel_for_track(track_idx);
                const int midi = ev.midi + transpose_;
                if (midi < core::MIDI_MIN || midi > core::MIDI_MAX)
                    musicpp_seterror(i18n::tr("error.play.transpose_pitch", transpose_, midi), group[t]->no);
                score_.tracks[track_idx].notes.push_back({tick, midi, ev.velocity, ev.dur_ticks, artic_});
                last_end_ = std::max(last_end_, clock_.at(tick + ev.dur_ticks));
                msgs.push_back({clock_.at(tick),
                                0x90u | ch | ((uint32_t)midi << 8) | ((uint32_t)ev.velocity << 16)});
                if (artic_)
                    msgs.push_back({clock_.at(tick + ev.dur_ticks),
                                    0x80u | ch | ((uint32_t)midi << 8)});
            }
        }
        // IR 的 tick 是 int32：有效上限钳制到 int32，保证 tick_ 永不溢出
        const int64_t tick_limit =
            std::min<int64_t>(config::max_ticks, std::numeric_limits<int>::max());
        if ((int64_t)group_len > tick_limit - tick_)
            musicpp_seterror(i18n::tr("error.limit.ticks", config::max_ticks), group[0]->no);
        tick_ += group_len;
        if (sink_ && !msgs.empty()) sink_(std::move(msgs));
    }

    // ── 指令分发：注册表驱动 ────────────────────────────────────────────────
    // 注册表 DIRECTIVE_HANDLERS 在类尾部（静态成员初始化器不是完整类上下文，
    // 需要 handler 声明先出现）。加新指令 = 写 handler + 注册一行
    // + common.hpp 关键字表加一行（两表由 static_assert 编译期对齐检查）。
    void directive(std::string_view line, int no) {
        for (const auto& [kw, h] : DIRECTIVE_HANDLERS)
            if (detail::is_directive(line, kw)) { (this->*h)(line, no); return; }
    }

    void directive_speed(std::string_view line, int no) {
        std::string_view unit = detail::trim(line.substr(5));
        const auto sp = unit.find_first_of(" \t");
        if (sp == std::string_view::npos)
            musicpp_seterror(i18n::tr("error.speed.missing_value", line), no);
        const std::string_view val = detail::trim(unit.substr(sp));
        unit = unit.substr(0, sp);
        if (unit == "bpm") {
            if (val.empty() || ((val[0] < '0' || val[0] > '9') && val[0] != '.')) {
                musicpp_seterror(timedeltas_.count(std::string(val))
                    ? i18n::tr("error.speed.bpm_needs_number", line)
                    : i18n::tr("error.speed.bad_number", line), no);
            }
            double v = 0;
            const auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
            if (ec != std::errc{} || ptr != val.data() + val.size())
                musicpp_seterror(i18n::tr("error.speed.bad_number", line), no);
            if (v < 1 || v > 2000)
                musicpp_seterror(i18n::tr("error.speed.bpm_range", v), no);
            set_tempo(60000.0 / v, no);
        } else if (unit == "milisec" || unit == "ms") {
            set_tempo(resolve_time(val, no), no);
        } else {
            musicpp_seterror(i18n::tr("error.speed.unknown_unit", unit), no);
        }
    }

    void directive_play(std::string_view line, int no) {
        std::string_view arg = detail::trim(line.substr(4));
        // 可选移调参数：play 名(+N) / 名(-N)，N = 半音数（嵌套 play 累加）
        int delta = 0;
        if (arg.ends_with(')')) {
            const auto lp = arg.find('(');
            if (lp == std::string_view::npos)
                musicpp_seterror(i18n::tr("error.play.transpose_format", line), no);
            std::string_view num = arg.substr(lp + 1, arg.size() - lp - 2);
            if (num.size() < 2 || (num.front() != '+' && num.front() != '-'))
                musicpp_seterror(i18n::tr("error.play.transpose_sign", line), no);
            const int sign = num.front() == '+' ? 1 : -1;
            num.remove_prefix(1);
            if (num.empty() || num.find_first_not_of("0123456789") != std::string_view::npos)
                musicpp_seterror(i18n::tr("error.play.transpose_sign", line), no);
            int v = 0;
            std::from_chars(num.data(), num.data() + num.size(), v);
            if (v > 36) musicpp_seterror(i18n::tr("error.play.transpose_range", line), no);
            delta = sign * v;
            arg = detail::trim(arg.substr(0, lp));
        }
        const std::string_view nv = detail::play_name(arg);
        if (nv.empty())
            musicpp_seterror(i18n::tr("error.play.format", line), no);
        const std::string name(nv);

        if (collecting_ && collecting_->name == name)
            musicpp_seterror(i18n::tr("error.section.not_ready", name), no);
        const auto it = sections_.find(name);
        if (it == sections_.end()) {
            std::vector<std::string_view> cands;
            cands.reserve(sections_.size());
            for (const auto& [k, v] : sections_) cands.push_back(k);
            musicpp_seterror(i18n::tr("error.section.undefined", name) + detail::did_you_mean(name, cands), no);
        }
        if (std::find(call_stack_.begin(), call_stack_.end(), name) != call_stack_.end()) {
            std::string cycle;
            for (const auto& s : call_stack_) cycle += s + " → ";
            musicpp_seterror(i18n::tr("error.section.cycle", cycle + name), no);
        }
        if (frames_.size() >= 64) musicpp_seterror(i18n::tr("error.section.too_deep"), no);

        log::debug("展开段落 <{}>{} (第 {} 行调用)", name,
                   delta ? std::format(" ({:+d})", delta) : "", no);
        const double saved_mpq = clock_.mpq;
        const int saved_program = program_;
        const bool saved_artic = artic_;
        transpose_ += delta;                                     // 移调参数：段内生效
        call_stack_.push_back(name);
        frames_.push_back({name, no});
        for (const auto& sl : it->second.lines)
            handle_content_line(sl);
        flush_pending();
        frames_.pop_back();
        call_stack_.pop_back();
        if (clock_.mpq != saved_mpq) set_tempo(saved_mpq, no);   // 状态恢复
        program_ = saved_program;                                // 音色同为段内局部
        artic_ = saved_artic;                                    // echo 状态同理
        transpose_ -= delta;                                     // 移调参数不外泄
    }

    // ensemble { play A / play B / ... }：成员同一 tick 起跑，块长取最长；
    // 按段落名分配声部，同一名字在不同合奏块中复用同一声部
    void exec_ensemble(const std::vector<SrcLine>& members, int no) {
        if (members.empty()) { log::debug("ensemble: 空 block"); return; }
        const int start = tick_;
        int end = start;
        log::debug("合奏开始: {} 声部 @ tick {}", members.size(), start);
        size_t next_free = score_.tracks.size();
        const size_t saved_base = track_base_;
        for (const auto& m : members) {
            const std::string_view ml = detail::trim(m.text);
            if (!detail::is_directive(ml, "play"))
                musicpp_seterror(i18n::tr("error.ensemble.member_play"), m.no);
            const std::string sec(detail::play_name(detail::trim(ml.substr(4))));
            const auto [it, inserted] = ensemble_tracks_.try_emplace(sec, next_free);
            (void)inserted;

            tick_ = start;
            track_base_ = it->second;
            directive_play(ml, m.no);
            next_free = std::max(next_free, score_.tracks.size());
            end = std::max(end, tick_);
        }
        track_base_ = saved_base;
        tick_ = end;
        log::debug("合奏结束: 时长 {} tick", end - start);
    }

    // import "路径"：加载模块——其段落注册为 <模块.名>，主流注册为 <模块>
    void directive_import(std::string_view line, int no) {
        if (!frames_.empty())
            musicpp_seterror(i18n::tr("error.import.top_level_only"), no);
        const auto q1 = line.find('"');
        const auto q2 = line.find('"', q1 == std::string_view::npos ? q1 : q1 + 1);
        if (q1 == std::string_view::npos || q2 == std::string_view::npos)
            musicpp_seterror(i18n::tr("error.import.format", line), no);
        const std::string rel(line.substr(q1 + 1, q2 - q1 - 1));

        namespace fs = std::filesystem;
        // 谱面内容与路径均为 UTF-8：统一走 u8 → 宽字符路径，Unicode 文件名安全
        const auto u8path = [](const std::string& s) {
            return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
        };
        const fs::path p = u8path(base_dir_) / u8path(rel);
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(p, ec);
        const std::string key = ec ? p.lexically_normal().string() : canon.string();
        if (std::find(import_stack_.begin(), import_stack_.end(), key) != import_stack_.end())
            musicpp_seterror(i18n::tr("error.import.cycle", rel), no);
        if (imported_.contains(key)) {
            log::debug("模块已加载，跳过: {}", rel);
            return;
        }
        std::ifstream in(p, std::ios::binary);
        if (!in) musicpp_seterror(i18n::tr("error.import.not_found", p.string()), no);

        imported_.insert(key);
        import_stack_.push_back(key);
        const std::string saved_base = base_dir_;
        base_dir_ = p.parent_path().string();

        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto [mod_lines, mod_sections] = detail::collect_sections(text);
        const std::string mod = [&] {
            const auto u8 = p.stem().u8string();   // 模块名保持 UTF-8
            return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        }();
        log::info("import \"{}\": 模块 <{}> ({} 段落)", rel, mod, mod_sections.size());

        // 被导入文件自己的 import 先行（顶层指令）
        std::vector<SrcLine> rest;
        for (auto& sl : mod_lines) {
            const std::string_view t = detail::trim(sl.text);
            if (detail::is_directive(t, "import")) directive_import(t, sl.no);
            else rest.push_back(std::move(sl));
        }
        for (auto& s : mod_sections) {
            auto qname = mod + "." + s.name;   // 先取限定名再 move（参数求值顺序不定）
            register_section(std::move(qname), std::move(s), no);
        }
        register_section(mod, Section{mod, no, std::move(rest)}, no);

        base_dir_ = saved_base;
        import_stack_.pop_back();
    }

    // instrument <名字|program号>：切换当前音色。作用于后续写入的声部；
    // 未知名字 warning 并回退标准音色（program 0 = 大钢琴）
    void directive_instrument(std::string_view line, int no) {
        const std::string_view arg = detail::trim(line.substr(10));
        if (arg.empty())
            musicpp_seterror(i18n::tr("error.instrument.missing", line), no);
        const auto prog = core::instrument_program(arg);
        if (!prog) {
            std::vector<std::string_view> cands;
            for (const auto& [k, v] : core::chinese_aliases()) cands.push_back(k);
            for (const auto g : core::GM_NAMES) cands.push_back(g);
            log::warn("{}", i18n::tr("warn.instrument.unknown", arg, no) + detail::did_you_mean(arg, cands));
            program_ = 0;
        } else {
            program_ = *prog;
            log::debug("音色切换: {} → program {} (第 {} 行)", arg, program_, no);
        }
    }

    // echo on|off：on = 连奏回音（默认，音符时值走完不松键，自由混响）；
    // off = 断奏，时值结束发 note-off，音与音干净分明
    void directive_echo(std::string_view line, int no) {
        std::string_view arg = detail::trim(line.substr(4));
        if (arg.empty() || arg == "on")       artic_ = false;
        else if (arg == "off")                artic_ = true;
        else musicpp_seterror(i18n::tr("error.echo.arg", line), no);
        log::debug("echo {} ({}模式)", artic_ ? "off" : "on",
                   artic_ ? "articulated" : "sustained");
    }

    // noteoff on|off：echo 的兼容别名（noteoff on ≡ echo off）
    void directive_noteoff(std::string_view line, int no) {
        std::string_view arg = detail::trim(line.substr(7));
        if (arg.empty() || arg == "on")       artic_ = true;
        else if (arg == "off")                artic_ = false;
        else musicpp_seterror(i18n::tr("error.noteoff.arg", line), no);
        log::debug("noteoff {} (兼容写法, 等价 echo {})",
                   artic_ ? "on" : "off", artic_ ? "off" : "on");
    }

    void directive_wait(std::string_view line, int no) {
        const std::string_view val = detail::trim(line.substr(4));
        if (val.empty()) musicpp_seterror(i18n::tr("error.wait.missing", line), no);
        const double ms = resolve_time(val, no);
        clock_.add_wait(tick_, ms);
        score_.waits.push_back({tick_, ms});
        log::trace("停顿: {:.0f} ms @ tick {}", ms, tick_);
    }

    void directive_timedelta(std::string_view line, int no) {
        if (!frames_.empty())
            musicpp_seterror(i18n::tr("error.timedelta.in_section"), no);
        std::string_view rest = detail::trim(line.substr(9));
        const auto sp = rest.find_first_of(" \t");
        if (sp == std::string_view::npos)
            musicpp_seterror(i18n::tr("error.timedelta.format", line), no);
        const std::string_view name = rest.substr(0, sp);
        rest = detail::trim(rest.substr(sp));
        if (!rest.starts_with('='))
            musicpp_seterror(i18n::tr("error.timedelta.missing_eq", line), no);
        const std::string_view val = detail::trim(rest.substr(1));
        if (name.empty() || (name[0] >= '0' && name[0] <= '9'))
            musicpp_seterror(i18n::tr("error.timedelta.bad_name", line), no);
        if (timedeltas_.contains(std::string(name)))
            musicpp_seterror(i18n::tr("error.timedelta.duplicate", name), no);
        const double ms = resolve_time(val, no);
        timedeltas_.emplace(name, ms);
        log::debug("timedelta {} = {:.0f} ms", name, ms);
    }

    // 时间值求值：字面量 + timedelta 变量表（实现见 parse/time.hpp 纯函数模块）
    double resolve_time(std::string_view tok, int no) {
        auto r = time::resolve(tok, timedeltas_);
        if (!r) musicpp_seterror(r.error(), no);
        return *r;
    }

    // 兼容：裸数字行 = speed milisec
    bool try_legacy_tempo(std::string_view line, int no) {
        std::string_view s = line;
        if (s.starts_with('+')) s.remove_prefix(1);
        if (s.empty() || s.find_first_not_of("0123456789") != std::string_view::npos)
            return false;
        double v = 0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || ptr != s.data() + s.size()) return false;
        log::debug("兼容模式: 裸数字速度行 {} ms (第 {} 行)", v, no);
        set_tempo(v, no);
        return true;
    }

    // ── 指令注册表（加新指令在这里登记）────────────────────────────────────
    // 实现细节；公开以便类外做编译期对齐检查、工具读取
public:
    using Handler = void (StreamBuilder::*)(std::string_view, int);
    static constexpr std::pair<std::string_view, Handler> DIRECTIVE_HANDLERS[] = {
        {"speed",      &StreamBuilder::directive_speed},
        {"play",       &StreamBuilder::directive_play},
        {"echo",       &StreamBuilder::directive_echo},
        {"noteoff",    &StreamBuilder::directive_noteoff},
        {"wait",       &StreamBuilder::directive_wait},
        {"timedelta",  &StreamBuilder::directive_timedelta},
        {"instrument", &StreamBuilder::directive_instrument},
        {"import",     &StreamBuilder::directive_import},
    }
    ;
private:
};

namespace detail {

// 编译期对齐检查：handler 表 ⊆ 关键字表；关键字表除 ensemble（块收录，无 handler）外
// 每个关键字都要有 handler
consteval bool directive_registry_aligned() {
    for (const auto& [kw, h] : StreamBuilder::DIRECTIVE_HANDLERS) {
        bool found = false;
        for (const std::string_view k : DIRECTIVE_KEYWORDS)
            if (k == kw) { found = true; break; }
        if (!found) return false;
    }
    for (const std::string_view k : DIRECTIVE_KEYWORDS) {
        if (k == "ensemble") continue;
        bool found = false;
        for (const auto& [kw, h] : StreamBuilder::DIRECTIVE_HANDLERS)
            if (kw == k) { found = true; break; }
        if (!found) return false;
    }
    return true;
}
static_assert(directive_registry_aligned(),
              "DIRECTIVE_KEYWORDS 与 DIRECTIVE_HANDLERS 不对齐（加新指令要两边都登记）");

} // namespace detail

// ════════════════════════════════════════════════════════════════════════════
// 入口：批处理 = 全部行喂给统一解析器，再取 Score IR
// ════════════════════════════════════════════════════════════════════════════

inline core::Score build_from_string(std::string_view text, int velocity = 100,
                                     std::string base_dir = ".") {
    StreamBuilder b(velocity, std::move(base_dir));
    for (size_t pos = 0; pos <= text.size();) {
        const size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) { b.feed_line(text.substr(pos)); break; }
        b.feed_line(text.substr(pos, eol - pos));
        pos = eol + 1;
    }
    b.finish();
    return b.take_score();
}

inline core::Score build_from_file(const std::string& path, int velocity = 100) {
    // path 为 UTF-8：经 filesystem::path 走宽字符 API 打开，Unicode 文件名安全
    const std::filesystem::path fsp(
        std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::ifstream in(fsp, std::ios::binary);
    if (!in) throw std::runtime_error(i18n::tr("error.file.open", path));
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    log::info("读取谱面: {} ({} 字节)", path, text.size());
    // import 的相对路径以谱面文件所在目录为基准
    std::string base = std::filesystem::path(path).parent_path().string();
    if (base.empty()) base = ".";
    return build_from_string(text, velocity, std::move(base));
}

} // namespace musicpp::parse
