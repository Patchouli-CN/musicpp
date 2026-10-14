# musicpp 使用手册

[返回 README](README.md) · 版本 0.8.0+（含 main 分支新增：复合时间字面量、力度记号、报错国际化、did-you-mean）

---

## 1. 命令行

```
musicpp <file.music> [options]
  --noteoff, --no-echo   全局断奏（每个音时值结束松键）
  --trace                TRACE 级日志（看解析细节）
  --dump                 只解析不播放，打印 IR 统计
  --volume N             力度 0-127（默认 100）
  -h, --help             帮助
  -v, --version          版本与构建信息
```

谱面文件为 UTF-8，CRLF/LF 均可，中文文件名安全。`import` 的相对路径以谱面文件所在目录为基准。

---

## 2. 语言规范

### 2.1 音符

数字简谱，自然大调：**`1 2 3 4 5 6 7` = do re mi fa sol la si = C D E F G A B**。

| 记号 | 含义 | 示例 |
|------|------|------|
| `1`~`7` | 音级，中央八度 = C4~B4（`1` = 中央 C，MIDI 60） | `1` `5` |
| `^` | 升八度，可叠加 | `1^` = C5，`1^^` = C6 |
| `,` | 降八度，可叠加 | `5,` = G3，`1,,,` = C2 |
| `#` | 升半音（跟在八度记号后） | `1#` = C#4，`5#,,` |
| `0` | 休止符 | `0_` |
| `>1` | 重音：力度 ×1.25（封顶 127），作用于后一个音符/和弦 | `>1` `>[135]` |
| `1(80)` | 精确力度 0~127（0 = 无声），和弦写括号后 | `1(60)` `[135](100)` |

- 等音遵循乐理：`3#` = `4`（E#=F），`7#` = `1^`（B#=C）
- 写降号用等音：`b3` → `2#`，`b7` → `6#`，`b2` → `1#`，`b6` → `5#`
- 八度范围 lvl 0~6（MIDI 21=A0 ~ 108=C8），越界报错
- 记号间以空白分隔；`|` 仅作小节线排版，不发声
- 力度默认 100（CLI `--volume N` 全局改）；`>` 与 `(n)` 可叠加时值后缀：`>1(110)_`

### 2.2 时值

基本单位 = 一个四分音符，每个记号默认时值 1 单位。后缀作用于前一个音符/和弦，从左到右复合：

| 后缀 | 作用 | 示例 |
|------|------|------|
| `-` | +1 单位 | `1-` 二分（2 单位），`1---` 全音符（4 单位） |
| `_` | ÷2 | `1_` 八分，`1__` 十六分 |
| `*` | ÷3 | 三连音 |
| `%` | ÷5 | 五连音 |
| `&` | ÷7 | 七连音 |
| `.` | ×1.5（附点） | `1_.` 附点八分 |

例：`1-_` = (1+1)/2 = 1 单位（四分）。

### 2.3 和弦

`[135]` 或 `{135}`：括号内所有音同时发声，共享一个时值，后缀写在括号后：

```
[135]        大三和弦，1 单位
[35#1#^]_-   1.5 单位
```

括号不可嵌套、必须闭合、内部不能有空白。

### 2.4 双声部

连续的两行音符行（中间无空行/指令）**同时演奏**：第一行上声部，第二行下声部。空行是分组边界：

```
[1#35#1#^]_- [2#4#62#^]_ |     ← 旋律
1#,__ 2#,__ 3,__ 2#,__ |       ← 伴奏（与上行同时）
                               ← 空行：恢复单声部
1 2 3 |
```

三声部以上用 `ensemble`（见 2.9）。

### 2.5 speed — 速度

```
speed bpm 120        每分钟 120 个四分音符（1~2000）
speed milisec 500    每个四分音符 500 毫秒（等价 bpm 120）
speed ms 500         milisec 的简写别名
speed milisec beat   也接受 timedelta 变量（毫秒量纲；bpm 不接受变量）
```

可写在中途变速。兼容：单独一行的裸数字 = `speed milisec`。

### 2.6 wait / timedelta — 停顿与时间变量

```
wait 5s              停 5 秒（单位：ms / s / m / h / d，无单位按毫秒）
wait 1m30s           复合字面量：多段拼接，单位须从大到小（1h30m15.5s 合法，30s1m 报错）
timedelta 段间换气 = 500ms
wait 段间换气        变量要先声明后使用（顶层声明，常量不可变）
```

### 2.7 段落与 play —— 复用与参数化

```
副歌 {
instrument 小提琴
1 2 3 4 |
}

play 副歌          引用（先定义后引用）
play 副歌(+2)      移调参数：升 2 半音
play 副歌(-3)      降 3 半音（±36 以内）
play <副歌>        兼容写法
```

- 定义本身不出声；定义不能嵌套；引用禁止成环（报错打印调用链 `A → B → A`）
- 移调是**半音**移调（色彩性，非自然音阶）；嵌套 play 的参数累加
- 段内的 `speed` / `instrument` / `echo` 只影响段内，`play` 返回后自动恢复
- 段落体内可以写 `#` 注释（注释在任意位置都合法）

### 2.8 instrument — 音色

```
instrument 小提琴      中文别名（钢琴/小提琴/长笛/八音盒/古筝/太鼓……）
instrument flute     英文 GM 名（大小写/空格/连字符不敏感）
instrument 33        裸数字 = GM program 号（0~127）
```

- 作用于后续写入的声部；每条声部分配独立 MIDI 通道（自动跳过 GM 打击乐通道 10）
- 合奏时各段落各占各的通道，各用各的音色
- 未知名称：`[note] WARN` 并回退标准音色（钢琴，program 0）
- 完整音色表见 `musicpp/instrument.hpp`

### 2.9 ensemble — 合奏

```
ensemble {
play 旋律
play 伴奏
play drums.鼓点A
}
```

- 所有成员同一时刻起跑，块总时长取最长成员
- 成员**只能是 play 指令**（可带移调参数）
- 成员及其调用的段落内**不能用 `speed` / `wait`**：速度与停顿是全曲共享的，会波及其他声部（报错）；需要变速请写在 ensemble 块之外
- 按段落名分轨：同一名字在不同 ensemble 块中复用同一声部（和音色/通道）
- ensemble 块可以写在段落体内（`play` 该段落时展开）

### 2.10 echo — 回音模式

```
echo on      连奏（默认）：音符时值走完不松键，自由混响
echo off     断奏：时值结束发 note-off，音与音干净分明
```

分段生效（A 段断奏、B 段回音都可以）。兼容：`noteoff on` ≡ `echo off`，`#noteoff` ≡ `echo off`。CLI 全局强制：`--no-echo`。

### 2.11 import — 模块

```
import "drums.music"
play drums.鼓点A      模块内段落 → 模块名.段落名
play drums            模块主流（段落外的内容）→ 模块名
```

- 只能用于顶层；路径相对当前谱面文件
- 自动防重复导入、防循环导入

### 2.12 注释

`#` 开头的行是注释，任意位置均可（含段落体内）。**不支持行尾注释**。

### 2.13 旧格式兼容

最初踩音器时代的谱面原样可跑：裸数字速度行、`#noteoff`、CRLF、双行声部配对。

---

## 3. 错误报告

所有错误带行号；段落内错误附带调用栈：

```
[app] ERROR / 谱面错误: 段落引用形成环: A → B → A (第 12 行)
[app] ERROR / 谱面错误: 移调 +12 后音高超出 MIDI 范围（110） (第 3 行)
[note] WARN  / 未知乐器 'xxx'（第 33 行），回退标准音色 [钢琴]
```

**did-you-mean 建议**：段落名/乐器名打错时，按编辑距离（UTF-8 码点级，≤2）给出最接近的候选：

```
[app] ERROR / 谱面错误: 未定义（或尚未解析到）的段落 <副哥>，你的意思是 <副歌> 吗？ (第 4 行)
[note] WARN  / 未知乐器 '小提亲'（第 1 行），回退标准音色 [钢琴]，你的意思是 <小提琴> 吗？
```

**国际化**：报错消息为键值对目录（内置简体中文）。语言文件在 `<exe 目录>/lang/<语言>.txt`，
每行 `key = value`（`#` 注释，UTF-8），逐键覆盖内置目录；环境变量 `MUSICPP_LANG` 选择语言
（如 `en`，仓库自带 `lang/en.txt`），文件缺失时 WARN 并回退内置中文。
带 `{}` 占位符的消息中，花括号字面量需转义为 `{{ }}`。

常见错误：先 play 后定义、段落成环、和弦未闭合、非法字符、八度/移调越界、ensemble 成员不是 play、裸数字速度行以外的未知内容。

---

## 4. 编程调用（C++ API）

纯头文件库，`#include "musicpp/musicpp.hpp"`，编译 `-I. -std=c++23 -lwinmm`：

```cpp
using namespace musicpp;

// ① 批处理解析 → Score IR（可任意探查/改写）
auto score = parse::build_from_file("demo/demo.music", /*velocity=*/100);

// ② 同步播放
auto out = backend::create_default();
engine::Engine eng(*out);
eng.play(score);                       // eng.stop_requested = true 可随时中断

// ③ 协程播放（多个 play_async 派生进同一 Scheduler 即合奏）
async::Scheduler loop;
loop.spawn(engine::play_async(loop, *out, score));
loop.run();

// ④ 流式解析：边喂行边出声（AI 生成/网络流/实时弹奏）
parse::StreamBuilder sb(100, ".");
sb.set_sink([](std::vector<core::TimedMsg> msgs) { /* t 毫秒时刻发 raw MIDI */ });
sb.feed_line("1 2 3 |");
sb.finish();

// ⑤ 自定义后端：实现 MidiOut 即可接入任何东西
class MyOut final : public backend::MidiOut {
    void note_on(int ch, int midi, int vel) override { /* ... */ }
    void note_off(int ch, int midi) override { /* ... */ }
    void program_change(int ch, int program) override { /* ... */ }
};

// ⑥ 解析上限（Billion Laughs 防护）：play 是宏展开语义，
//    DAG 式段落引用可让几十行谱面指数膨胀。默认值对正常谱面足够宽，
//    跑可信的大型谱面可放宽，跑不可信谱面可收紧
musicpp::config::max_expansion_steps = 10'000'000;  // 展开处理的内容行总数
musicpp::config::max_events = 10'000'000;           // 音符事件总数（≈200MB）
musicpp::config::max_ticks = 100'000'000;           // 总时长（≈4 小时 @bpm120）
```

完整示例见 `examples/`（构建产物在 `demo/`）：

| 示例 | 演示内容 |
|------|---------|
| `demo/api_demo.exe` | IR 探查、自定义后端、BGM 后台播放与中断 |
| `demo/async_demo.exe` | 协程双任务错开 1.5s（卡农效果） |
| `demo/stream_demo.exe <file>` | 流式解析 + 协程播放（边喂边响） |

---

## 5. 示例谱面

全部位于 `demo/`（`ensemble_demo.music` 依赖 `demo/lib/drums.music`，import 相对谱面所在目录解析）：

| 文件 | 内容 |
|------|------|
| `demo/demo.music` | 入门：段落 + 双声部 |
| `demo/wait_demo.music` | wait / timedelta |
| `demo/ensemble_demo.music` | 合奏 + import |
| `demo/instrument_demo.music` | 中文/英文/program 号音色、未知回退 |
| `demo/echo_demo.music` | 分段回音/断奏 |
| `demo/velocity_demo.music` | 重音记号、精确力度、渐强渐弱 |
| `demo/transpose_demo.music` | 移调参数、嵌套累加 |
| `demo/modes.music` | 中古调式教学（同主音对比 + C 大调轮转） |
| `demo/intro.music` / `demo/intro_v2.music` | 真实曲目：地灵殿 intro（旧格式与新格式，IR 完全一致） |
