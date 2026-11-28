// Root host for AndroidSurfaceFlingerImgui.
//
// The APK host is a NativeActivity: WindowManager owns the window, so the UI can
// only ever live inside the Activity. This host does the opposite. Started by
// root it loads the platform gui library, asks SurfaceFlinger for a layer of its
// own and renders straight into that surface, which lets the panel float above
// every app without an Activity, a window token or a single line of Java.
//
//   su -c ./sfimgui --probe     print every step and exit
//   su -c ./sfimgui --symbols   dump the gui library's real symbol names
//   su -c ./sfimgui             transparent system-wide overlay
//   su -c ./sfimgui --opaque    opaque backdrop (screenshots, demos)
//   su -c ./sfimgui --duration 30
//
// Everything is printed on the terminal of the shell that started it, which is
// why this mode is far easier to diagnose than the APK.

#include <android/native_window.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "core/crash_handler.h"
#include "core/trace.h"
#include "overlay/engine.h"
#include "sf/direct_surface.h"
#include "sf/evdev.h"

#ifndef SF_VERSION
#define SF_VERSION "dev"
#endif

namespace {

const char* kDefaultBase = "/data/local/tmp/sf-imgui";

std::atomic<bool> gQuit{false};

void HandleSignal(int) { gQuit.store(true); }

// Names the current host step in three places at once: the crash record's stage
// field, the startup log, and the terminal that started the process.
void HostStage(const char* text) {
    sf::setCrashStage(text);
    sf::trace("host: %s", text);
    std::fprintf(stderr, "[sfimgui] %s\n", text);
    std::fflush(stderr);
}

struct Options {
    int width = 0;
    int height = 0;
    int fps = 0;
    int duration = 0;
    float scale = 1.0f;
    float opacity = 1.0f;
    bool probe = false;
    bool symbols = false;
    bool opaque = false;
    bool noInput = false;
    bool reset = false;
    bool help = false;
    std::string name = "AndroidSurfaceFlingerImgui";
    std::string base = kDefaultBase;
    std::string config;
};

void PrintUsage(const char* program) {
    std::printf(
        "AndroidSurfaceFlingerImgui root host %s\n"
        "\n"
        "用法: %s [参数]\n"
        "  --probe          逐步检查图形库、符号、屏幕尺寸与表面创建，然后退出\n"
        "  --symbols        转储 libgui.so 中与 SurfaceFlinger 相关的真实符号名\n"
        "  --opaque         使用不透明背景（默认透明，能看到下层应用）\n"
        "  --width N        覆盖屏幕宽度\n"
        "  --height N       覆盖屏幕高度\n"
        "  --fps N          帧率上限，0 表示跟随屏幕\n"
        "  --scale F        界面缩放，默认 1.0\n"
        "  --opacity F      界面不透明度，默认 1.0\n"
        "  --duration N     运行 N 秒后自动退出\n"
        "  --no-input       不读取触摸设备\n"
        "  --reset          忽略已保存的界面设置\n"
        "  --base DIR       运行目录，默认 %s\n"
        "  --config FILE    设置文件，默认 <base>/settings.cfg\n"
        "  --name NAME      SurfaceFlinger 图层名\n"
        "  -h, --help       显示本帮助\n",
        SF_VERSION, program, kDefaultBase);
}

bool ParseArgs(int argc, char** argv, Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index] != nullptr ? argv[index] : "";
        auto next = [&](const char* flag) -> const char* {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "[sfimgui] %s 缺少参数\n", flag);
                return nullptr;
            }
            return argv[++index];
        };
        if (arg == "--probe") {
            options->probe = true;
        } else if (arg == "--symbols") {
            options->symbols = true;
        } else if (arg == "--opaque") {
            options->opaque = true;
        } else if (arg == "--no-input") {
            options->noInput = true;
        } else if (arg == "--reset") {
            options->reset = true;
        } else if (arg == "-h" || arg == "--help") {
            options->help = true;
            PrintUsage(argv[0]);
            return true;
        } else if (arg == "--width") {
            const char* value = next("--width");
            if (value == nullptr) return false;
            options->width = std::atoi(value);
        } else if (arg == "--height") {
            const char* value = next("--height");
            if (value == nullptr) return false;
            options->height = std::atoi(value);
        } else if (arg == "--fps") {
            const char* value = next("--fps");
            if (value == nullptr) return false;
            options->fps = std::atoi(value);
        } else if (arg == "--duration") {
            const char* value = next("--duration");
            if (value == nullptr) return false;
            options->duration = std::atoi(value);
        } else if (arg == "--scale") {
            const char* value = next("--scale");
            if (value == nullptr) return false;
            options->scale = static_cast<float>(std::atof(value));
        } else if (arg == "--opacity") {
            const char* value = next("--opacity");
            if (value == nullptr) return false;
            options->opacity = static_cast<float>(std::atof(value));
        } else if (arg == "--base") {
            const char* value = next("--base");
            if (value == nullptr) return false;
            options->base = value;
        } else if (arg == "--config") {
            const char* value = next("--config");
            if (value == nullptr) return false;
            options->config = value;
        } else if (arg == "--name") {
            const char* value = next("--name");
            if (value == nullptr) return false;
            options->name = value;
        } else {
            std::fprintf(stderr, "[sfimgui] 未知参数: %s\n", arg.c_str());
            PrintUsage(argv[0]);
            return false;
        }
    }
    return true;
}

std::string ReadCommand(const char* command) {
    std::string output;
    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) return output;
    char buffer[256] = {0};
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    pclose(pipe);
    return output;
}

bool ReadFirstLine(const std::string& path, std::string* line) {
    FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) return false;
    char buffer[128] = {0};
    const bool ok = std::fgets(buffer, sizeof(buffer), file) != nullptr;
    std::fclose(file);
    if (ok && line != nullptr) *line = buffer;
    return ok;
}

bool ParseSize(const std::string& text, int* width, int* height) {
    bool found = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') continue;
        std::size_t cursor = index;
        long w = 0;
        while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
            w = w * 10 + (text[cursor] - '0');
            ++cursor;
        }
        if (cursor >= text.size() || (text[cursor] != 'x' && text[cursor] != 'X')) {
            index = cursor;
            continue;
        }
        std::size_t digits = 0;
        long h = 0;
        std::size_t after = cursor + 1;
        while (after < text.size() && text[after] >= '0' && text[after] <= '9') {
            h = h * 10 + (text[after] - '0');
            ++after;
            ++digits;
        }
        if (digits > 0 && w >= 240 && h >= 240 && w <= 10000 && h <= 10000) {
            *width = static_cast<int>(w);
            *height = static_cast<int>(h);
            found = true;
        }
        index = after;
    }
    return found;
}

bool ReadDrmSize(int* width, int* height, std::string* how) {
    DIR* directory = opendir("/sys/class/drm");
    if (directory == nullptr) return false;
    bool found = false;
    while (struct dirent* entry = readdir(directory)) {
        if (entry->d_name[0] == '.') continue;
        const std::string modes = std::string("/sys/class/drm/") + entry->d_name + "/modes";
        std::string line;
        if (!ReadFirstLine(modes, &line)) continue;
        if (ParseSize(line, width, height)) {
            if (how != nullptr) *how = modes;
            found = true;
            break;
        }
    }
    closedir(directory);
    return found;
}

// The layer has to be created with a pixel size before anything can be drawn, so
// the display geometry is resolved before SurfaceFlinger is even asked: wm size
// knows about density overrides, fb0 and the DRM modes cover the rest.
void DetectDisplaySize(int* width, int* height, std::string* how) {
    const std::string wm = ReadCommand("/system/bin/wm size 2>/dev/null");
    if (ParseSize(wm, width, height)) {
        if (how != nullptr) *how = "wm size";
        return;
    }
    std::string line;
    if (ReadFirstLine("/sys/class/graphics/fb0/virtual_size", &line)) {
        std::string normalized;
        for (char c : line) normalized += (c == ',') ? 'x' : c;
        if (ParseSize(normalized, width, height)) {
            if (how != nullptr) *how = "fb0 virtual_size";
            return;
        }
    }
    if (ReadDrmSize(width, height, how)) return;
    *width = 1080;
    *height = 2400;
    if (how != nullptr) *how = "默认值（可用 --width/--height 覆盖）";
}

bool EnsureDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::string current;
    for (std::size_t index = 0; index <= path.size(); ++index) {
        if (index == path.size() || path[index] == '/') {
            if (!current.empty() && mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) {
                return false;
            }
        }
        if (index < path.size()) current += path[index];
    }
    return true;
}

std::string Join(const std::string& directory, const char* name) {
    if (directory.empty()) return name;
    return directory + "/" + name;
}

int RunProbe(const Options& options) {
    std::printf("[sfimgui] probe\n");
    const long long uid = static_cast<long long>(geteuid());
    std::printf("[sfimgui] uid=%lld %s\n", uid,
                uid == 0 ? "(root)" : "(非 root，需要 root 才能加载系统图形库)");
    const sf::direct::ProbeResult probe = sf::direct::Probe();
    std::printf("[sfimgui] 图形库: %s\n", probe.library.empty() ? "(未加载)" : probe.library.c_str());
    std::printf("[sfimgui] 符号: %s\n", probe.symbolReport.c_str());
    std::printf("[sfimgui] 系统: API %d\n", probe.apiLevel);
    std::printf("[sfimgui] 变体: %s\n", probe.variant.empty() ? "(未选中)" : probe.variant.c_str());
    std::printf("[sfimgui] 元数据: %s\n", probe.metadata.c_str());
    std::printf("[sfimgui] 说明: %s\n", probe.detail.c_str());

    int width = options.width;
    int height = options.height;
    std::string how = "命令行参数";
    if (width <= 0 || height <= 0) DetectDisplaySize(&width, &height, &how);
    std::printf("[sfimgui] 屏幕: %dx%d (%s)\n", width, height, how.c_str());

    sf::evdev::Reader input;
    std::string inputError;
    if (input.Open(width, height, &inputError)) {
        std::printf("[sfimgui] 触摸: %s\n", input.summary().c_str());
    } else {
        std::printf("[sfimgui] 触摸: %s\n", inputError.c_str());
    }

    sf::direct::SurfaceHandle handle;
    std::string error;
    const bool created = sf::direct::Create(width, height, options.name.c_str(), &handle, &error);
    std::printf("[sfimgui] 表面: %s\n", created ? "创建成功" : error.c_str());
    if (created) {
        std::printf("[sfimgui] window=%p 尺寸 %dx%d\n", static_cast<void*>(handle.window),
                    handle.width, handle.height);
    }
    sf::direct::Destroy(&handle);
    std::printf("[sfimgui] 探测完成\n");
    return created ? 0 : 2;
}

int RunSymbols() {
    std::printf("[sfimgui] 符号转储\n");
    const sf::direct::ProbeResult probe = sf::direct::Probe();
    std::printf("[sfimgui] 图形库: %s\n", probe.library.empty() ? "(未加载)" : probe.library.c_str());
    std::printf("[sfimgui] 符号: %s\n", probe.symbolReport.c_str());
    std::printf("[sfimgui] 说明: %s\n", probe.detail.c_str());
    const std::string dump = sf::direct::DumpSymbols();
    std::printf("%s", dump.c_str());
    return probe.libraryLoaded ? 0 : 2;
}

// The previous crash log is printed before anything else and rotated out of the
// way, so the crash handler always appends to a file that holds one crash and the
// terminal shows the record without pulling anything off the device.
void ShowPreviousCrash(const std::string& base) {
    const std::string path = base + "/native_crash.txt";
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return;
    std::string data;
    char chunk[4096];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) data.append(chunk, got);
    std::fclose(file);
    if (data.empty()) {
        std::remove(path.c_str());
        return;
    }
    const std::size_t limit = 2000;
    const std::string tail = data.size() > limit ? data.substr(data.size() - limit) : data;
    std::fprintf(stderr, "[sfimgui] 上次运行崩溃记录（%s）:\n%s\n", path.c_str(), tail.c_str());
    std::fflush(stderr);
    std::rename(path.c_str(), (base + "/native_crash_prev.txt").c_str());
}

int RunHost(const Options& options) {
    int width = options.width;
    int height = options.height;
    std::string how = "命令行参数";
    if (width <= 0 || height <= 0) DetectDisplaySize(&width, &height, &how);
    std::printf("[sfimgui] 屏幕 %dx%d (%s)\n", width, height, how.c_str());

    sf::direct::SurfaceHandle handle;
    std::string error;
    if (!sf::direct::Create(width, height, options.name.c_str(), &handle, &error)) {
        std::fprintf(stderr, "[sfimgui] 创建表面失败: %s\n", error.c_str());
        std::fprintf(stderr,
                     "[sfimgui] 需要 root，并且进程要能 dlopen /system/lib64/libgui.so；"
                     "先用 --probe 查看每一步。\n");
        return 2;
    }
    std::string binderDetail;
    const bool binderReady = sf::direct::PrepareBinderPool(&binderDetail);
    std::printf("[sfimgui] 表面就绪（%s），启动渲染线程\n", binderDetail.c_str());
    if (!binderReady) {
        std::fprintf(stderr, "[sfimgui] 警告: %s，继续运行\n", binderDetail.c_str());
    }
    sf::trace("host: surface %dx%d ready", width, height);

    // Input devices first, renderer second: the terminal order then matches the
    // real initialisation order, and a crash in the render thread always comes
    // after a readable input summary.
    sf::evdev::Reader input;
    std::string inputError;
    if (options.noInput) {
        std::printf("[sfimgui] 触摸: 已禁用\n");
    } else if (input.Open(width, height, &inputError)) {
        std::printf("[sfimgui] 触摸: %s\n", input.summary().c_str());
    } else {
        std::fprintf(stderr, "[sfimgui] 触摸不可用: %s（继续运行，界面无法交互）\n", inputError.c_str());
    }
    std::fflush(stdout);

    HostStage("构造渲染引擎");
    sf::Engine engine;
    sf::OverlayOptions overlay;
    overlay.targetFps = options.fps;
    overlay.uiScale = options.scale;
    overlay.opacity = options.opacity;
    overlay.acceptInput = true;
    overlay.transparentBackground = !options.opaque;
    overlay.artifactName = std::string("sfimgui ") + SF_VERSION;
    overlay.backend = "SurfaceFlinger 直连";
    overlay.backendDetail = "root 宿主：不经过 WindowManager，图层直接提交给 SurfaceFlinger 合成器。";
    overlay.configPath = options.config;
    overlay.resetSettings = options.reset;
    std::printf("[sfimgui] 渲染窗口 %p（%dx%d）\n", static_cast<void*>(handle.window), width, height);
    std::fflush(stdout);
    HostStage("启动渲染线程");
    if (!engine.start(handle.window, overlay)) {
        std::fprintf(stderr, "[sfimgui] 渲染线程启动失败: %s\n", engine.lastError().c_str());
        sf::direct::Destroy(&handle);
        return 3;
    }
    HostStage("渲染线程已启动");

    const auto startedAt = std::chrono::steady_clock::now();
    bool rendererDied = false;
    sf::evdev::Touch touches[32];
    while (!gQuit.load()) {
        const int count = input.Poll(touches, static_cast<int>(sizeof(touches) / sizeof(touches[0])));
        for (int index = 0; index < count; ++index) {
            engine.touch(touches[index].action, touches[index].x, touches[index].y);
        }
        if (!engine.running()) {
            std::fprintf(stderr, "[sfimgui] 渲染线程已退出: %s\n", engine.lastError().c_str());
            rendererDied = true;
            break;
        }
        if (options.duration > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::steady_clock::now() - startedAt).count();
            if (elapsed >= options.duration) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }
    std::printf("[sfimgui] 停止\n");
    engine.stop();
    sf::direct::Destroy(&handle);
    if (rendererDied) {
        // A dead render thread means no UI at all: report it as a failure instead
        // of exiting 0 as if the session had been a success.
        std::printf("[sfimgui] 退出（渲染线程异常结束）\n");
        return 4;
    }
    std::printf("[sfimgui] 退出\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, &options)) return 1;
    if (options.help) return 0;

    if (options.symbols) {
        EnsureDirectory(options.base);
        sf::traceInit((options.base + "/startup.log").c_str());
        sf::trace("symbols: uid=%lld version=%s", static_cast<long long>(geteuid()), SF_VERSION);
        return RunSymbols();
    }

    if (options.probe) {
        EnsureDirectory(options.base);
        sf::traceInit((options.base + "/startup.log").c_str());
        sf::trace("probe: uid=%lld version=%s", static_cast<long long>(geteuid()), SF_VERSION);
        return RunProbe(options);
    }

    if (!EnsureDirectory(options.base)) {
        std::fprintf(stderr, "[sfimgui] 无法创建运行目录 %s\n", options.base.c_str());
    }
    if (options.config.empty()) options.config = Join(options.base, "settings.cfg");
    sf::traceInit(Join(options.base, "startup.log").c_str());
    sf::installCrashHandler(Join(options.base, "native_crash.txt").c_str());
    ShowPreviousCrash(options.base);

    std::printf("[sfimgui] AndroidSurfaceFlingerImgui root host %s\n", SF_VERSION);
    std::printf("[sfimgui] 运行目录 %s\n", options.base.c_str());
    sf::trace("host: start uid=%lld version=%s", static_cast<long long>(geteuid()), SF_VERSION);
    if (geteuid() != 0) {
        std::fprintf(stderr,
                     "[sfimgui] 警告：当前不是 root，系统图形库与 SurfaceFlinger 通常不可访问\n");
    }
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    std::signal(SIGQUIT, HandleSignal);
    return RunHost(options);
}
