#include <android/input.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <chrono>
#include <string>

#include "core/crash_handler.h"
#include "core/log.h"
#include "overlay/engine.h"

namespace {

sf::Engine gEngine;
ANativeWindow* gWindow = nullptr;

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void startRenderer() {
    if (gWindow == nullptr) return;
    sf::OverlayOptions options;
    options.targetFps = 0;  // follow the display
    options.artifactName = "embedded";
    gEngine.start(gWindow, options);
}

void onAppCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                gWindow = app->window;
                startRenderer();
                SF_LOGI("window %dx%d attached", ANativeWindow_getWidth(gWindow),
                        ANativeWindow_getHeight(gWindow));
            }
            break;
        case APP_CMD_TERM_WINDOW:
            SF_LOGI("window detached");
            gEngine.stop();
            gWindow = nullptr;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (app->window != nullptr) gEngine.updateWindow(app->window);
            break;
        default:
            break;
    }
}

int32_t onInput(android_app* /*app*/, AInputEvent* event) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    int mapped = -1;
    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN: mapped = 0; break;
        case AMOTION_EVENT_ACTION_MOVE: mapped = 1; break;
        case AMOTION_EVENT_ACTION_UP: mapped = 2; break;
        case AMOTION_EVENT_ACTION_CANCEL: mapped = 3; break;
        default: return 0;
    }
    gEngine.touch(mapped, AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
    return 1;
}

}  // namespace

void android_main(android_app* app) {
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInput;

    if (app->activity != nullptr) {
        const char* base = app->activity->externalDataPath != nullptr
                               ? app->activity->externalDataPath
                               : app->activity->internalDataPath;
        if (base != nullptr) {
            const std::string crash = std::string(base) + "/native_crash.txt";
            sf::installCrashHandler(crash.c_str());
            SF_LOGI("crash log: %s", crash.c_str());
        }
    }

    double lastAttempt = 0.0;
    int attempts = 0;

    while (!app->destroyRequested) {
        android_poll_source* source = nullptr;
        int events = 0;
        const int timeout = gWindow != nullptr ? 16 : -1;
        if (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
        }

        // The first window the system hands over is sometimes not ready yet.
        if (gWindow != nullptr && !gEngine.running()) {
            const double now = nowSeconds();
            if (attempts < 120 && now - lastAttempt > 0.5) {
                lastAttempt = now;
                ++attempts;
                SF_LOGW("retrying render start (%d)", attempts);
                startRenderer();
            }
        }
    }

    gEngine.stop();
}
