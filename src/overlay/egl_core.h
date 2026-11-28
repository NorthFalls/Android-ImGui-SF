#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>

namespace sf {

class EglCore {
public:
    EglCore() = default;
    ~EglCore();
    EglCore(const EglCore&) = delete;
    EglCore& operator=(const EglCore&) = delete;

    bool createWindowSurface(ANativeWindow* window);
    bool makeCurrent();
    // Returns false when the frame was rejected (surface lost, invalid window):
    // the caller must not count that frame as presented.
    bool swapBuffers();
    void releaseSurface();
    void shutdown();
    bool valid() const { return context_ != EGL_NO_CONTEXT && surface_ != EGL_NO_SURFACE; }

private:
    bool initDisplay();

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
};

}  // namespace sf
