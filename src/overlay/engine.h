#pragma once

#include <android/asset_manager.h>
#include <android/native_window.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace sf {

struct OverlayOptions {
    int targetFps = 60;
    float uiScale = 1.0f;
    float opacity = 1.0f;
    bool acceptInput = true;
    std::string artifactName;
    // Optional path for the persisted UI settings (scale, theme, opacity, ...).
    // An empty path disables persistence and keeps every launch at defaults.
    std::string configPath;
    // Name and detail of the surface backend the host selected, shown in the
    // menu so the direct SurfaceFlinger path is visible when it is active.
    std::string backend;
    std::string backendDetail;
    // Set when the previous launch ended before the first frames were presented.
    // Its stored settings cannot be trusted then (a persisted backend choice may
    // be exactly what killed the process), so the engine starts from defaults and
    // writes them back immediately.
    bool resetSettings = false;
    // Tail of the previous native crash log, shown in the 关于 tab so a device
    // that keeps dying can be diagnosed from a screenshot.
    std::string crashReport;
    // One-line explanation of a startup recovery, shown at the top of 显示.
    std::string startupNote;
    // The root host draws over other apps: clear to a transparent frame and skip
    // the full-screen pastel wash so the app underneath stays visible.
    bool transparentBackground = false;
};

struct FrameStats {
    float fps = 0.0f;
    float frameMs = 0.0f;
    uint64_t frames = 0;
};

// Owns the render thread. Every public method is safe to call from any thread.
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool start(ANativeWindow* window, const OverlayOptions& options);
    void stop();
    bool running() const;

    void updateWindow(ANativeWindow* window);
    void setFontPath(const std::string& path);
    void setAssetManager(AAssetManager* manager);
    std::string fontInfo() const;
    int initMillis() const;
    void setOptions(const OverlayOptions& options);
    OverlayOptions options() const;

    // action: 0 = down, 1 = move, 2 = up, 3 = cancel (pixels, top-left origin)
    void touch(int action, float x, float y);
    void resetTouch();

    FrameStats stats() const;
    std::string lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sf
