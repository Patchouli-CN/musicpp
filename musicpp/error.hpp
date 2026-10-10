#pragma once
// ════════════════════════════════════════════════════════════════════════════
// musicpp — 谱面错误：带行号与段落调用栈的编译器级诊断
//
// 错误形如:
//   谱面错误: 八度超出范围 '1^^^^' (第 12 行)
//     在段落 <副歌> (第 87 行 'play <副歌>' 调用)
//     在段落 <终曲> (第 120 行 'play <终曲>' 调用)
// ════════════════════════════════════════════════════════════════════════════

#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace musicpp {

// 段落调用帧
struct CallFrame {
    std::string section;   // 段落名
    int invoke_line = 0;   // 调用它的行号
};

class ScoreError : public std::runtime_error {
public:
    ScoreError(const std::string& what, int line, std::vector<CallFrame> stack = {})
        : std::runtime_error(build(what, line, stack))
        , line_(line)
        , stack_(std::move(stack)) {}

    int line() const noexcept { return line_; }
    const std::vector<CallFrame>& stack() const noexcept { return stack_; }

private:
    static std::string build(const std::string& what, int line,
                             const std::vector<CallFrame>& stack) {
        std::string msg = std::format("谱面错误: {} (第 {} 行)", what, line);
        // 栈底在前：最外层调用排在最前面
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            msg += std::format("\n  在段落 <{}> (第 {} 行 'play <{}>' 调用)",
                               it->section, it->invoke_line, it->section);
        }
        return msg;
    }

    int line_;
    std::vector<CallFrame> stack_;
};

} // namespace musicpp
