#include "overlay/engine.h"

#include <GLES3/gl3.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "backends/imgui_impl_opengl3.h"
#include "core/clock.h"
#include "core/log.h"
#include "imgui.h"
#include "overlay/egl_core.h"
#include "sys/device_info.h"
#include "sys/telemetry.h"
#include "ui/OppoFont.h"
#include "ui/menu.h"
#include "ui/theme.h"

namespace sf {
namespace {

struct TouchEvent {
    int action;
    float x;
    float y;
};

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
    bool renderFrame();
    void pumpTouches();
    void updateScale(int width, int height);
};

void Engine::Impl::updateScale(int width, int height) {
    (void)width;
    (void)height;
    // One layout unit is one physical pixel by default: predictable on every
    // density and never blown up. The menu slider can still change it.
    scale = 1.0f;
    menu.scale = scale;
    ui::ApplyTheme(scale);
}

bool Engine::Impl::setup(ANativeWindow* win) {
    const double started = nowSeconds();
    if (!egl.createWindowSurface(win)) {
        error = "eglCreateWindowSurface failed";
        return false;
    }

    device.load();
    telemetry.sample();

    int width = ANativeWindow_getWidth(win);
    int height = ANativeWindow_getHeight(win);
    for (int attempt = 0; attempt < 100 && (width <= 0 || height <= 0); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        width = ANativeWindow_getWidth(win);
        height = ANativeWindow_getHeight(win);
    }
    surfaceWidth = width;
    surfaceHeight = height;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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

    updateScale(surfaceWidth, surfaceHeight);

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        error = "ImGui OpenGL3 init failed";
        return false;
    }

    menu.telemetry = &telemetry;
    menu.device = &device;
    menu.artifact = options.artifactName;
    menu.glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    menu.glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    // Warm up the glyph atlas and its GL texture before the first visible frame:
    // otherwise the first frame pays for it while the user is already looking.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    initMs.store(static_cast<int>((nowSeconds() - started) * 1000.0));
    SF_LOGI("gl %s, %dx%d, init %dms", menu.glVersion.c_str(), surfaceWidth, surfaceHeight,
            initMs.load());
    return true;
}

void Engine::Impl::teardown() {
    ImGui_ImplOpenGL3_Shutdown();
    if (ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();
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

bool Engine::Impl::renderFrame() {
    if (window == nullptr) return false;

    const int width = ANativeWindow_getWidth(window);
    const int height = ANativeWindow_getHeight(window);
    if (width <= 0 || height <= 0) return false;
    if (width != surfaceWidth || height != surfaceHeight) {
        surfaceWidth = width;
        surfaceHeight = height;
        if (!egl.createWindowSurface(window)) return false;
    }

    OverlayOptions current;
    {
        std::lock_guard<std::mutex> lock(mutex);
        current = options;
    }

    pumpTouches();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));
    io.DeltaTime = counter.frameMs() > 0.0f ? counter.frameMs() / 1000.0f : 1.0f / 60.0f;

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

    ImGui::Render();
    glViewport(0, 0, surfaceWidth, surfaceHeight);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    egl.swapBuffers();
    return true;
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
            if (!renderFrame()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            counter.tick(frameStart);
            fps.store(counter.fps());
            frameMs.store(counter.frameMs());
            frameCount.store(counter.frames());

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
