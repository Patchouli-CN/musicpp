// musicpp — .music++ 谱面播放器
// 用法: musicpp <file.music> [--noteoff] [--trace] [--dump] [--volume N]

#include <charconv>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "musicpp/musicpp.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

// Windows 用 wmain 直接拿 UTF-16 命令行，转 UTF-8：
// 日韩文等非 GBK 文件名也安全（窄 argv 走 ANSI 会丢字符）
#ifdef _WIN32
std::string wide_to_utf8(std::wstring_view s) {
    if (s.empty()) return {};
    const int ulen = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string u(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), u.data(), ulen, nullptr, nullptr);
    return u;
}
#endif

void usage() {
    std::cout << musicpp::version_banner() << "\n"
        "usage: musicpp <file.music> [options]\n"
        "  --noteoff, --no-echo  articulated mode (send note-off at duration end)\n"
        "  --trace      verbose logging (TRACE level)\n"
        "  --dump       parse only, print IR stats, no playback\n"
        "  --volume N   velocity 0-127 (default 100)\n"
        "  -v, --version  print version and build info\n";
}

} // namespace

int real_main(int argc, const std::vector<std::string>& args) {
    // 报错国际化：语言文件在 <exe 目录>/lang/，MUSICPP_LANG 环境变量选择语言
    {
        std::string exe;
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        if (const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH); n > 0)
            exe = wide_to_utf8(std::wstring_view(buf, n));
#endif
        if (!exe.empty()) {
            const auto pos = exe.find_last_of("/\\");
            musicpp::i18n::init(pos == std::string::npos ? "." : exe.substr(0, pos));
        }
    }
    if (argc < 2) { usage(); return 1; }

    std::string path;
    bool noteoff = false, dump = false;
    int volume = 100;
    for (int i = 1; i < argc; ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "-v" || a == "--version") {
            std::cout << musicpp::version_banner() << "\n";
            return 0;
        }
        else if (a == "--noteoff" || a == "--no-echo") noteoff = true;
        else if (a == "--trace") musicpp::log::set_level(musicpp::log::Level::trace);
        else if (a == "--dump") dump = true;
        else if (a == "--volume" && i + 1 < argc) {
            // 力度写进 MIDI 数据字节（7 位），越界会污染相邻字段
            const std::string& v = args[++i];
            const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), volume);
            if (ec != std::errc{} || ptr != v.data() + v.size() || volume < 0 || volume > 127) {
                std::cerr << "invalid --volume: " << v << " (expected integer 0-127)\n";
                return 1;
            }
        }
        else if (a.starts_with('-') && a.size() > 1) {
            std::cerr << "unknown option: " << a << "\n";
            usage();
            return 1;
        }
        else path = a;
    }
    if (path.empty()) { usage(); return 1; }

    try {
        musicpp::log::app.info("{}", musicpp::version_banner());
        auto score = musicpp::parse::build_from_file(path, volume);
        if (noteoff) score.force_artic = true;

        if (dump) {
            for (size_t t = 0; t < score.tracks.size(); ++t) {
                std::cout << std::format("track {}: {} events\n", t, score.tracks[t].notes.size());
                const auto& notes = score.tracks[t].notes;
                for (size_t i = 0; i < notes.size() && i < 12; ++i) {
                    const auto& ev = notes[i];
                    std::cout << std::format("  tick {:>6}  midi {:>3}  vel {:>3}  dur {:>5}  ({:.0f} ms)\n",
                                             ev.tick, ev.midi, ev.velocity, ev.dur_ticks,
                                             musicpp::core::ticks_to_ms(score.tempo, score.waits, ev.tick));
                }
                if (notes.size() > 12) std::cout << "  ...\n";
            }
            for (const auto& p : score.tempo)
                std::cout << std::format("tempo @tick {:>6}: {:.1f} ms/quarter\n",
                                         p.tick, p.ms_per_quarter);
            for (const auto& w : score.waits)
                std::cout << std::format("wait  @tick {:>6}: {:.0f} ms\n", w.tick, w.ms);
            for (const auto& pc : score.programs)
                std::cout << std::format("instrument @tick {:>6}: track {} → program {}\n",
                                         pc.tick, pc.track, pc.program);
            return 0;
        }

        auto out = musicpp::backend::create_default();
        musicpp::engine::Engine engine(*out);
        engine.play(score);
    } catch (const musicpp::ScoreError& e) {
        musicpp::log::app.error("{}", e.what());
        return 2;
    } catch (const std::exception& e) {
        musicpp::log::app.fatal("{}", e.what());
        return 1;
    }
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args(argc);
    for (int i = 0; i < argc; ++i) args[i] = wide_to_utf8(wargv[i]);
    return real_main(argc, args);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    return real_main(argc, args);
}
#endif
