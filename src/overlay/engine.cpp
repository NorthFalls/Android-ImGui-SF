#include "overlay/engine.h"

#include <GLES3/gl3.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "backends/imgui_impl_opengl3.h"
#include "core/clock.h"
#include "core/crash_handler.h"
#include "core/log.h"
#include "core/trace.h"
#include "imgui.h"
#include "overlay/egl_core.h"
#include "sys/device_info.h"
#include "sys/telemetry.h"
#include "ui/OppoFont.h"
#include "ui/menu.h"
#include "ui/theme.h"

namespace sf {
namespace {

// One line per startup stage, printed on the terminal that started the host,
// appended to startup.log and remembered for the crash handler. A segfault inside
// a vendor graphics driver leaves no other breadcrumb, so the last stage line is
// the difference between "EGL never got a surface" and "the renderer died later".
void Stage(const char* text) {
    sf::setCrashStage(text);
    sf::trace("stage: %s", text);
    std::fprintf(stderr, "[sfimgui] %s\n", text);
    std::fflush(stderr);
}

struct TouchEvent {
    int action;
    float x;
    float y;
};

// The subset of the UI state that survives a restart. Kept as a value type so
// the render loop can detect "did anything change" with plain comparison.
struct SettingsSnapshot {
    int targetFps = 0;
    int theme = 0;
    float scale = 1.0f;
    float opacity = 1.0f;
    bool showHud = true;
    bool acceptInput = true;
    int directSurface = 0;

    bool operator==(const SettingsSnapshot& other) const {
        return targetFps == other.targetFps && theme == other.theme && scale == other.scale &&
               opacity == other.opacity && showHud == other.showHud &&
               acceptInput == other.acceptInput && directSurface == other.directSurface;
    }
    bool operator!=(const SettingsSnapshot& other) const { return !(*this == other); }
};

SettingsSnapshot snapshotSettings(const ui::MenuState& menu) {
    SettingsSnapshot settings;
    settings.targetFps = menu.targetFps;
    settings.theme = menu.theme == ui::ThemeMode::Dark ? 1 : 0;
    settings.scale = menu.scale;
    settings.opacity = menu.opacity;
    settings.showHud = menu.showHud;
    settings.acceptInput = menu.acceptInput;
    settings.directSurface = menu.directSurface ? 1 : 0;
    return settings;
}

bool loadSettings(const std::string& path, ui::MenuState& menu) {
    if (path.empty()) return false;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    char line[128];
    bool loaded = false;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        char key[32] = {};
        float value = 0.0f;
        if (std::sscanf(line, "%31[^=]=%f", key, &value) != 2) continue;
        if (std::strcmp(key, "fps") == 0) {
            menu.targetFps = std::clamp(static_cast<int>(value), 0, 120);
        } else if (std::strcmp(key, "scale") == 0) {
            menu.scale = std::clamp(value, 0.75f, 2.0f);
        } else if (std::strcmp(key, "opacity") == 0) {
            menu.opacity = std::clamp(value, 0.2f, 1.0f);
        } else if (std::strcmp(key, "hud") == 0) {
            menu.showHud = value != 0.0f;
        } else if (std::strcmp(key, "input") == 0) {
            // Older builds could persist "input=0" (touch disabled), which froze
            // the panel with no way to switch it back on. Input stays enabled.
            menu.acceptInput = true;
        } else if (std::strcmp(key, "theme") == 0) {
            menu.theme = value != 0.0f ? ui::ThemeMode::Dark : ui::ThemeMode::Light;
        } else if (std::strcmp(key, "sf") == 0) {
            // Direct SurfaceFlinger backend; android_main reads the same key
            // before the engine starts, so it only takes effect on the next run.
            menu.directSurface = value != 0.0f;
        } else {
            continue;
        }
        loaded = true;
    }
    std::fclose(file);
    return loaded;
}

void saveSettings(const std::string& path, const SettingsSnapshot& settings) {
    if (path.empty()) return;
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return;
    // sv=2 marks a settings file written by a build that ships the startup
    // recovery path: it lets the next launch tell an intentional direct-backend
    // choice from one inherited from a crash-looping older build.
    std::fprintf(file,
                 "version=1\nsv=2\nfps=%d\nscale=%.3f\nopacity=%.3f\nhud=%d\ninput=%d\ntheme=%d\nsf=%d\n",
                 settings.targetFps, settings.scale, settings.opacity, settings.showHud ? 1 : 0,
                 settings.acceptInput ? 1 : 0, settings.theme, settings.directSurface);
    std::fclose(file);
}

}  // namespace

struct Engine::Impl {
    std::mutex mutex;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};

    ANativeWindow* window = nullptr;
    bool windowDirty = false;
    OverlayOptions options;

    EglCore egl;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    int resizeRetries = 0;
    int swapFailures = 0;

    FrameCounter counter;
    std::atomic<float> fps{0.0f};
    std::atomic<float> frameMs{0.0f};
    std::atomic<uint64_t> frameCount{0};
    std::atomic<int> initMs{0};

    std::vector<TouchEvent> touches;
    Telemetry telemetry;
    DeviceInfo device;
    ui::MenuState menu;
    std::string error;
    std::string fontPath;
    std::string fontInfo = "none";
    std::string configPath;
    SettingsSnapshot savedSettings;
    bool settingsLoaded = false;
    double lastSaveTime = 0.0;
    float scale = 1.0f;

    ~Impl() {
        if (window != nullptr) {
            ANativeWindow_release(window);
            window = nullptr;
        }
    }

    void run();
    bool setup(ANativeWindow* win);
    void teardown();
    bool renderFrame(ANativeWindow* target);
    void pumpTouches();
    void updateScale(int width, int height);
    void saveSettingsIfChanged();
};

void Engine::Impl::updateScale(int width, int height) {
    (void)width;
    (void)height;
    // One layout unit is one physical pixel by default: predictable on every
    // density and never blown up. The menu slider (or the saved settings) can
    // still change it, and the theme variant rides along with the scale.
    menu.scale = std::clamp(menu.scale, 0.75f, 2.0f);
    scale = menu.scale;
    ui::ApplyTheme(scale, menu.theme);
    menu.themeDirty = false;
}

bool Engine::Impl::setup(ANativeWindow* win) {
    const double started = nowSeconds();
    Stage("创建 EGL 表面");
    bool resetRequested = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        configPath = options.configPath;
        resetRequested = options.resetSettings;
    }
    if (!egl.createWindowSurface(win)) {
        char message[192] = {0};
        std::snprintf(message, sizeof(message), "eglCreateWindowSurface 失败 (eglGetError=0x%04x)",
                      static_cast<unsigned>(eglGetError()));
        error = message;
        sf::trace("engine: %s", error.c_str());
        return false;
    }
    Stage("EGL 表面就绪");

    Stage("读取设备与遥测信息");
    device.load();
    telemetry.sample();

    int width = ANativeWindow_getWidth(win);
    int height = ANativeWindow_getHeight(win);
    for (int attempt = 0; attempt < 100 && (width <= 0 || height <= 0); ++attempt) {
        if (stopRequested.load()) {
            sf::trace("engine: setup aborted while waiting for the surface size");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        width = ANativeWindow_getWidth(win);
        height = ANativeWindow_getHeight(win);
    }
    surfaceWidth = width;
    surfaceHeight = height;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    Stage("生成 ImGui 上下文并载入字体");
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImFont* font = nullptr;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned int*>(OPPOSans_H),
                                          static_cast<int>(OPPOSans_H_size), 18.0f, &config);
    fontInfo = font != nullptr ? "embedded:OPPOSans" : "none";
    if (font == nullptr) font = io.Fonts->AddFontDefault();

    // Restore the previous session's UI state before the first frame so the
    // panel never flashes at the defaults and then jumps.
    // loadSettings() mutates the menu in place; a failed load is not fatal and
    // must not disable persistence. Persistence is on whenever a path exists.
    settingsLoaded = !configPath.empty();
    if (resetRequested) {
        // The previous launch ended before the first frames: the stored file is
        // not trusted. A persisted experimental backend could otherwise crash the
        // process on every start, with no way back from inside the UI.
        menu.targetFps = 0;
        menu.scale = 1.0f;
        menu.opacity = 1.0f;
        menu.showHud = true;
        menu.acceptInput = true;
        menu.theme = ui::ThemeMode::Light;
        menu.themeDirty = true;
        menu.directSurface = false;
        sf::trace("engine: startup recovery, stored settings replaced with defaults");
        // Persist right away instead of waiting for the debounce or a clean
        // shutdown: a second crash must not be able to restore the old file.
        saveSettings(configPath, snapshotSettings(menu));
    } else {
        (void)loadSettings(configPath, menu);
    }
    // Input can never be disabled from the UI: a stored 0 used to make the panel
    // untouchable across restarts, with no control left to re-enable it.
    menu.acceptInput = true;
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Publish the restored state through the options as well: the loop copies
        // options back into the menu every frame, so without this the saved
        // opacity and frame cap would be replaced by the host defaults on frame one.
        options.targetFps = menu.targetFps;
        options.opacity = menu.opacity;
        options.acceptInput = menu.acceptInput;
        options.uiScale = menu.scale;
    }
    sf::trace("engine: settings %s theme=%d", settingsLoaded ? "on" : "off",
              static_cast<int>(menu.theme));
    savedSettings = snapshotSettings(menu);
    lastSaveTime = nowSeconds();

    updateScale(surfaceWidth, surfaceHeight);

    Stage("初始化 OpenGL3 后端");
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        error = "ImGui OpenGL3 init failed";
        sf::trace("engine: %s", error.c_str());
        return false;
    }
    Stage("GL 后端就绪，预热首帧");

    menu.telemetry = &telemetry;
    menu.device = &device;
    menu.artifact = options.artifactName;
    menu.backend = options.backend;
    menu.backendDetail = options.backendDetail;
    menu.crashReport = options.crashReport;
    menu.startupNote = options.startupNote;
    menu.transparent = options.transparentBackground;
    // glGetString() returns nullptr when no context is current; constructing a
    // std::string from that pointer is undefined behaviour and aborted startup.
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    menu.glRenderer = renderer != nullptr ? renderer : "unknown";
    menu.glVersion = version != nullptr ? version : "unknown";

    // Warm up the glyph atlas and its GL texture before the first visible frame:
    // otherwise the first frame pays for it while the user is already looking.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    initMs.store(static_cast<int>((nowSeconds() - started) * 1000.0));
    menu.initMs = initMs.load();
    // A retry that finally succeeds must not keep reporting the previous failure.
    error.clear();
    Stage("引擎初始化完成");
    sf::trace("engine: setup done in %dms", initMs.load());
    SF_LOGI("gl %s, %dx%d, init %dms, theme %s", menu.glVersion.c_str(), surfaceWidth,
            surfaceHeight, initMs.load(), menu.theme == ui::ThemeMode::Dark ? "dark" : "light");
    return true;
}

void Engine::Impl::teardown() {
    Stage("停止渲染并保存设置");
    // Persist the current UI state on every shutdown path (window detach, app
    // exit, surface loss) so the next launch continues where this one stopped.
    if (settingsLoaded) {
        savedSettings = snapshotSettings(menu);
        saveSettings(configPath, savedSettings);
        settingsLoaded = false;
    }
    // setup() can fail before the renderer backend exists (no EGL surface, no
    // context, bad font). Tearing down unconditionally would call into an
    // uninitialised backend, so only unwind what was actually created.
    if (ImGui::GetCurrentContext() != nullptr) {
        if (ImGui::GetIO().BackendRendererUserData != nullptr) ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }
    egl.shutdown();
}

void Engine::Impl::pumpTouches() {
    std::vector<TouchEvent> batch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        batch.swap(touches);
    }
    ImGuiIO& io = ImGui::GetIO();
    for (const TouchEvent& event : batch) {
        const bool inside = event.x >= 0.0f && event.y >= 0.0f && event.x < io.DisplaySize.x &&
                            event.y < io.DisplaySize.y;
        switch (event.action) {
            case 0:
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(event.x, event.y);
                io.AddMouseButtonEvent(0, true);
                break;
            case 1:
                if (inside) io.AddMousePosEvent(event.x, event.y);
                break;
            case 2:
            case 3:
                io.AddMousePosEvent(event.x, event.y);
                io.AddMouseButtonEvent(0, false);
                break;
            default:
                break;
        }
    }
}

// Called with the render thread's own reference to the window: reading the
// mutable Impl::window pointer here raced with updateWindow() on the glue thread.
bool Engine::Impl::renderFrame(ANativeWindow* target) {
    sf::setCrashStage("渲染帧");
    if (target == nullptr) return false;

    const int width = ANativeWindow_getWidth(target);
    const int height = ANativeWindow_getHeight(target);
    if (width <= 0 || height <= 0) return false;
    if (width != surfaceWidth || height != surfaceHeight) {
        // The new size is only committed once the surface rebuild succeeded.
        // Storing it first made a failed eglCreateWindowSurface invisible to the
        // size check, so the rebuild was never retried and every later frame
        // swapped into a dead surface (frozen panel until the next rotation).
        sf::trace("engine: surface resize to %dx%d", width, height);
        if (!egl.createWindowSurface(target)) {
            if (resizeRetries == 0 || resizeRetries % 50 == 0) {
                sf::trace("engine: surface rebuild failed (%d)", resizeRetries);
            }
            ++resizeRetries;
            return false;
        }
        resizeRetries = 0;
        surfaceWidth = width;
        surfaceHeight = height;
    }

    OverlayOptions current;
    {
        std::lock_guard<std::mutex> lock(mutex);
        current = options;
    }

    ImGuiIO& io = ImGui::GetIO();
    // DisplaySize must be current before the queued touches are applied: on the
    // first frame it used to be (0, 0), so the first tap was clipped as out of bounds.
    io.DisplaySize = ImVec2(static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));
    io.DeltaTime = counter.frameMs() > 0.0f ? counter.frameMs() / 1000.0f : 1.0f / 60.0f;

    pumpTouches();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    menu.fps = fps.load();
    menu.frameMs = frameMs.load();
    menu.frames = frameCount.load();
    menu.width = surfaceWidth;
    menu.height = surfaceHeight;
    menu.acceptInput = current.acceptInput;
    menu.opacity = current.opacity;
    if (telemetry.due(nowSeconds())) telemetry.sample();
    ui::Draw(menu);

    {
        std::lock_guard<std::mutex> lock(mutex);
        options.targetFps = menu.targetFps;
        options.opacity = menu.opacity;
        options.acceptInput = menu.acceptInput;
        options.uiScale = menu.scale;
    }
    saveSettingsIfChanged();

    ImGui::Render();
    glViewport(0, 0, surfaceWidth, surfaceHeight);
    if (menu.transparent) {
        // Root host: keep the layer transparent so the apps underneath stay
        // visible; the glass panels draw their own translucent wash.
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // A rejected swap means the frame never reached the compositor: counting it
    // would report a healthy fps on a surface that shows nothing.
    if (!egl.swapBuffers()) {
        if (swapFailures == 0 || swapFailures % 100 == 0) {
            sf::trace("engine: swap failed (%d)", swapFailures);
        }
        ++swapFailures;
        return false;
    }
    swapFailures = 0;
    if (frameCount.load() == 0) Stage("首帧已提交，界面开始渲染");
    return true;
}

void Engine::Impl::saveSettingsIfChanged() {
    if (!settingsLoaded) return;
    const SettingsSnapshot current = snapshotSettings(menu);
    if (current == savedSettings) return;
    // Debounce: dragging a slider would otherwise write the file every frame.
    const double now = nowSeconds();
    if (now - lastSaveTime < 0.75) return;
    lastSaveTime = now;
    savedSettings = current;
    saveSettings(configPath, current);
}

void Engine::Impl::run() {
    while (!stopRequested.load()) {
        ANativeWindow* win = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            win = window;
            if (win != nullptr) ANativeWindow_acquire(win);
            windowDirty = false;
        }
        if (win == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (!setup(win)) {
            sf::trace("engine: setup failed: %s", error.c_str());
            teardown();
            ANativeWindow_release(win);
            running = false;
            return;
        }

        double nextDeadline = nowSeconds();
        while (!stopRequested.load()) {
            OverlayOptions current;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (windowDirty) break;
                current = options;
            }
            int effectiveFps = current.targetFps;
            if (effectiveFps <= 0) {
                // Nothing is moving and nothing is visible in detail: half the
                // refresh rate keeps the island counter alive at half the power.
                const bool idle = !menu.visible && !menu.hudExpanded && menu.hudAnim <= 0.0f;
                effectiveFps = idle ? 30 : 0;
            }
            const double frameBudget = effectiveFps > 0 ? 1.0 / effectiveFps : 0.0;

            const double frameStart = nowSeconds();
            if (renderFrame(win)) {
                // Only presented frames count: a failed swap used to inflate both
                // the frame counter and the fps figure.
                counter.tick(frameStart);
                fps.store(counter.fps());
                frameMs.store(counter.frameMs());
                frameCount.store(counter.frames());
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            if (frameBudget > 0.0) {
                nextDeadline += frameBudget;
                const double now = nowSeconds();
                if (nextDeadline > now) {
                    std::this_thread::sleep_for(
                        std::chrono::duration<double>(std::min(nextDeadline - now, frameBudget)));
                } else if (now - nextDeadline > frameBudget * 4.0) {
                    nextDeadline = now;
                }
            }
        }

        teardown();
        ANativeWindow_release(win);
    }
    sf::trace("engine: render thread exit");
    running = false;
}

Engine::Engine() : impl_(new Impl()) {}
Engine::~Engine() { stop(); }

bool Engine::start(ANativeWindow* window, const OverlayOptions& options) {
    if (window == nullptr) return false;
    if (impl_->running) return true;
    if (impl_->thread.joinable()) {
        // A previous run() already returned (setup failed). Joining it first is
        // mandatory: assigning to a joinable std::thread calls std::terminate.
        impl_->stopRequested = true;
        impl_->thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->running) return true;
        if (impl_->window != nullptr) ANativeWindow_release(impl_->window);
        impl_->window = window;
        ANativeWindow_acquire(window);
        impl_->options = options;
        impl_->windowDirty = true;
        impl_->stopRequested = false;
    }
    impl_->running = true;
    impl_->thread = std::thread([this] { impl_->run(); });
    return true;
}

void Engine::stop() {
    if (!impl_->thread.joinable()) {
        impl_->running = false;
        return;
    }
    impl_->stopRequested = true;
    impl_->thread.join();
    impl_->running = false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->window != nullptr) {
        ANativeWindow_release(impl_->window);
        impl_->window = nullptr;
    }
}

bool Engine::running() const { return impl_->running.load(); }

void Engine::updateWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // Same target: renderFrame() notices the new size and rebuilds the EGL
    // surface in place. Restarting the thread here would destroy the ImGui
    // context and rebuild the 6.9 MB glyph atlas on every rotation.
    if (impl_->window == window) return;
    if (impl_->window != nullptr) ANativeWindow_release(impl_->window);
    impl_->window = window;
    if (window != nullptr) ANativeWindow_acquire(window);
    impl_->windowDirty = true;
}

void Engine::setFontPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->fontPath = path;
}

void Engine::setAssetManager(AAssetManager* manager) { (void)manager; }

std::string Engine::fontInfo() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->fontInfo;
}

int Engine::initMillis() const { return impl_->initMs.load(); }

void Engine::setOptions(const OverlayOptions& options) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->options = options;
}

OverlayOptions Engine::options() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->options;
}

void Engine::touch(int action, float x, float y) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->options.acceptInput) return;
    impl_->touches.push_back({action, x, y});
    if (impl_->touches.size() > 256) impl_->touches.erase(impl_->touches.begin());
}

void Engine::resetTouch() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->touches.clear();
}

FrameStats Engine::stats() const {
    FrameStats stats;
    stats.fps = impl_->fps.load();
    stats.frameMs = impl_->frameMs.load();
    stats.frames = impl_->frameCount.load();
    return stats;
}

std::string Engine::lastError() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

}  // namespace sf
