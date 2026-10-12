#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::parse — 段落切分
//
//   section_start    段落定义头判定（"名 {" / "<名> {"）
//   collect_sections 模块文件的段落提取（import 专用）：纯文本切分为
//                    段落表 + 主流行，不做任何指令求值
// ════════════════════════════════════════════════════════════════════════════

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common.hpp"

namespace musicpp::parse::detail {

// 段落定义头: "<名> {"（兼容形式）或 "名 {"（裸名形式）。
// 返回段落名；不是段落头返回 nullopt。ensemble { 等关键字由调用方先判。
inline std::optional<std::string> section_start(std::string_view line) {
    if (!line.ends_with('{')) return std::nullopt;
    const std::string_view head = trim(line.substr(0, line.size() - 1));
    if (head.empty()) return std::nullopt;                       // "{135}" 和弦行
    if (head.front() == '<') {                                   // <名> { 兼容形式
        if (head.back() != '>' || head.size() < 3) return std::nullopt;
        return std::string(head.substr(1, head.size() - 2));
    }
    if (head.find_first_of(" \t") != std::string_view::npos) return std::nullopt;
    const char c = head.front();
    if ((c >= '0' && c <= '7') || c == '[' || c == '{' || c == '|')
        return std::nullopt;                                     // 音符行
    if (is_any_directive(head)) return std::nullopt;             // 关键字另有其职
    return std::string(head);
}

// 模块文件的段落提取（import 专用）：把文件切分为段落表 + 主流行。
// 不做任何指令求值——主流的执行发生在 play <模块> 时。
inline std::pair<std::vector<SrcLine>, std::vector<Section>>
collect_sections(std::string_view text) {
    std::vector<SrcLine> main_lines;
    std::vector<Section> sections;
    Section* cur = nullptr;
    bool in_ensemble = false;   // ensemble block 内（含段落体内的）
    int no = 0;

    auto on_line = [&](std::string_view raw) {
        ++no;
        const std::string_view line = trim(raw);

        if (cur) {
            if (line == "}" && !in_ensemble) {
                sections.push_back(std::move(*cur));
                delete cur;
                cur = nullptr;
            } else {
                if (is_directive(line, "ensemble") && line.ends_with('{'))
                    in_ensemble = true;
                else if (line == "}" && in_ensemble)
                    in_ensemble = false;
                cur->lines.push_back({std::string(line), no});
            }
            return;
        }

        if (in_ensemble) {
            main_lines.push_back({std::string(line), no});
            if (line == "}") in_ensemble = false;
            return;
        }

        if (line.empty()) { main_lines.push_back({}); return; }
        if (line.starts_with('#')) {
            if (line == "#noteoff") main_lines.push_back({"noteoff on", no});
            return;
        }
        if (auto sec = section_start(line)) {
            cur = new Section{std::move(*sec), no, {}};
            return;
        }
        if (is_directive(line, "ensemble") && line.ends_with('{')) {
            in_ensemble = true;
            main_lines.push_back({std::string(line), no});
            return;
        }
        if (line == "}") musicpp_seterror(i18n::tr("error.brace.orphan"), no, {});
        main_lines.push_back({std::string(line), no});
    };

    for (size_t pos = 0; pos <= text.size();) {
        const size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) { on_line(text.substr(pos)); break; }
        on_line(text.substr(pos, eol - pos));
        pos = eol + 1;
    }
    if (cur) {
        const int def = cur->def_line;
        const std::string name = cur->name;
        delete cur;
        musicpp_seterror(i18n::tr("error.section.unclosed", name), def, {});
    }
    if (in_ensemble) musicpp_seterror(i18n::tr("error.ensemble.unclosed"), no, {});
    return {std::move(main_lines), std::move(sections)};
}

} // namespace musicpp::parse::detail
