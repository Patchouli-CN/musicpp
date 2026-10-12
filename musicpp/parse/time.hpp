#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp::parse::time — 时间字面量与时间值求值
//
//   纯函数模块：不依赖 StreamBuilder 状态，错误以 expected 返回，
//   由调用方包装成行号错误。可独立单元测试。
//
//   字面量 = 一段或多段 数字[.数字]单位 拼接，单位 ms/s/m/h/d
//   必须按从大到小出现：
//     500ms  90s  1m30s  1h30m15.5s  2m100ms；无单位按 ms
// ════════════════════════════════════════════════════════════════════════════

#include "../i18n.hpp"

#include <cctype>
#include <charconv>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace musicpp::parse::time {

// 纯字面量解析：1m30s → 90000.0（毫秒）
inline std::expected<double, std::string> parse_literal(std::string_view tok) {
    if (tok.empty() || !(std::isdigit((unsigned char)tok[0]) || tok[0] == '.'))
        return std::unexpected(i18n::tr("error.time.missing_number", tok));
    static constexpr std::pair<std::string_view, double> UNITS[] = {
        {"ms", 1}, {"s", 1000}, {"m", 60000}, {"h", 3600000}, {"d", 86400000}};
    double total = 0.0;
    int last_rank = 5;   // 单位必须按从大到小出现：d=4 h=3 m=2 s=1 ms=0
    size_t i = 0;
    while (i < tok.size()) {
        size_t j = i;
        while (j < tok.size() && (std::isdigit((unsigned char)tok[j]) || tok[j] == '.')) ++j;
        if (j == i)
            return std::unexpected(i18n::tr("error.time.missing_number", tok));
        double v = 0;
        const auto [ptr, ec] = std::from_chars(tok.data() + i, tok.data() + j, v);
        if (ec != std::errc{} || ptr != tok.data() + j)
            return std::unexpected(i18n::tr("error.time.bad_number", tok));
        size_t k = j;
        while (k < tok.size() && std::isalpha((unsigned char)tok[k])) ++k;
        const std::string_view unit = tok.substr(j, k - j);
        double mult = 1;
        int rank = 0;
        if (!unit.empty()) {
            bool found = false;
            for (int r = 0; r < 5; ++r)
                if (UNITS[r].first == unit) { mult = UNITS[r].second; rank = r; found = true; break; }
            if (!found)
                return std::unexpected(i18n::tr("error.time.unknown_unit", unit, tok));
        }
        if (rank >= last_rank)
            return std::unexpected(i18n::tr("error.time.unit_order", tok));
        last_rank = rank;
        total += v * mult;
        i = k;
    }
    if (total <= 0 || total > 86400000.0)
        return std::unexpected(i18n::tr("error.time.range", tok));
    return total;
}

// 时间值求值：字面量走 parse_literal，否则查 timedelta 变量表
inline std::expected<double, std::string>
resolve(std::string_view tok, const std::unordered_map<std::string, double>& vars) {
    if (tok.empty() || !(std::isdigit((unsigned char)tok[0]) || tok[0] == '.')) {
        const auto it = vars.find(std::string(tok));
        if (it == vars.end())
            return std::unexpected(i18n::tr("error.time.undefined_var", tok));
        return it->second;
    }
    return parse_literal(tok);
}

} // namespace musicpp::parse::time
