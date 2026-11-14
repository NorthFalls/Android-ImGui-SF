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
