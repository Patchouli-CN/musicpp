# musicpp

**一门会出声的音乐编程语言** —— 文本简谱 DSL + C++23 播放器。

同一份 `.music` 文件有三个身份：对人像乐谱（简谱排版、人工可读），对机器像代码（变量、段落、模块、合奏），跑起来直接是音乐。

```
speed bpm 100

主题 {
instrument 八音盒
1 1 5 5 | 6 6 5- |
}

伴奏 {
instrument 弦乐
[1,3,5,]- [1,4,6,]- |
}

ensemble {
play 主题
play 伴奏
}
play 主题(+12)      # 高八度再来一遍
```

## 特性

- **简谱记法**：`1~7` 数字谱，`^`/`,` 调八度，`#` 升半音，`[135]` 和弦，`- _ . * % &` 时值记号
- **双声部排版**：连续两行音符行同时演奏，和手写谱习惯一致
- **段落复用**：`名 { ... }` 定义，`play 名` 引用，支持移调参数 `play 名(+2)`
- **变量**：`timedelta 名 = 500ms`，时间字面量带单位（`ms/s/m/h/d`）
- **模块**：`import "drums.music"`，模块限定名 `drums.鼓点A`，防重防环
- **合奏**：`ensemble { play A / play B }` 多段落同时起跑，自动分轨分通道
- **GM 音色**：`instrument 小提琴`，中文别名 / 英文 GM 名 / program 号均可
- **回音控制**：`echo on/off` 连奏混响与断奏，分段生效
- **工程化**：行号 + 段落调用栈的错误报告、结构化日志、流式解析、协程播放、MIDI 后端抽象
- **安全**：Billion Laughs 防护（展开步数/事件数/总时长三重上限，`musicpp::config` 运行时可调）
- **全兼容**：最初的旧谱格式（裸数字速度行、`#noteoff`、双行声部）原样可跑

## 构建与运行

需要支持 C++23 的编译器（开发用的是 g++ 16，Windows/winmm 发声）：

```cmd
build.cmd                  :: 一键构建主程序 + 全部 demo
```

或手动：

```bash
g++ -std=c++23 -Wall -static -I. -o musicpp.exe main.cpp -lwinmm
```

运行：

```cmd
musicpp demo/demo.music            :: 播放
musicpp demo/intro.music --dump    :: 只解析，打印 IR 统计
musicpp demo/modes.music --no-echo :: 全局断奏
musicpp --version                  :: 版本与构建信息
```

## 文档与示例

- **[USAGE.md](USAGE.md)** —— 完整使用手册（CLI 参数、语言规范、编程 API）
- `skills/musicpp/SKILL.md` —— 给 AI 看的语言参考（可被 Kimi Code 等 agent 自动加载）
- 示例谱面（`demo/`）：`demo.music`（入门）、`modes.music`（中古调式教学）、`ensemble_demo.music`（合奏 + import）、`transpose_demo.music`（移调）、`instrument_demo.music`（音色）、`intro.music` / `intro_v2.music`（真实曲目：地灵殿 intro）
- `examples/` —— 编程调用示例源码（自定义后端 / 协程合奏 / 流式解析），构建产物在 `demo/`

## 架构

纯头文件库（`musicpp/`），namespace 分层：

```
musicpp::log       结构化日志     [tag] LEVEL / message
musicpp::config    运行时配置     解析上限（Billion Laughs 防护）
musicpp::core      音高与 IR      Score / NoteEvent / 通道分配 / GM 音色表
musicpp::parse     统一解析器     StreamBuilder（增量喂行，双产出 IR + TimedMsg 流）
musicpp::backend   MIDI 后端      MidiOut 接口 / winmm / Null
musicpp::engine    播放引擎       build_timeline / Engine::play / play_async（协程）
musicpp::async     迷你协程运行时 task / Scheduler / channel
```

新增平台后端（ALSA / CoreMIDI / MIDI 文件导出）只需实现 `MidiOut` 接口。

## 版本

当前 **0.8.0**（见 `musicpp/version.hpp`）。语义化版本：新增指令/语法升 minor，1.0 留给语言规范冻结。
