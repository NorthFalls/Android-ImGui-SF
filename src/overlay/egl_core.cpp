#include "overlay/egl_core.h"

#include "core/log.h"

namespace sf {
namespace {
constexpr EGLint kOpenGlEs3Bit = 0x0040;  // EGL_OPENGL_ES3_BIT_KHR
}

EglCore::~EglCore() { shutdown(); }

bool EglCore::initDisplay() {
    if (display_ != EGL_NO_DISPLAY) return true;
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) return false;
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display_, &major, &minor)) {
        display_ = EGL_NO_DISPLAY;
        return false;
    }
    const EGLint attribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, kOpenGlEs3Bit,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                              EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display_, attribs, &config_, 1, &count) || count == 0) return false;
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
    return context_ != EGL_NO_CONTEXT;
}

bool EglCore::createWindowSurface(ANativeWindow* window) {
    if (window == nullptr || !initDisplay()) return false;
    releaseSurface();
    surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        SF_LOGE("eglCreateWindowSurface failed");
        return false;
    }
    if (!makeCurrent()) return false;
    // One swap per vsync: presentation is paced by the display, not a sleep loop.
    eglSwapInterval(display_, 1);
    return true;
}

bool EglCore::makeCurrent() {
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE) return false;
    return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
}

bool EglCore::swapBuffers() {
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE) return false;
    return eglSwapBuffers(display_, surface_) == EGL_TRUE;
}

void EglCore::releaseSurface() {
    if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display_, surface_);
    }
    surface_ = EGL_NO_SURFACE;
}

void EglCore::shutdown() {
    releaseSurface();
    if (display_ != EGL_NO_DISPLAY) {
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglTerminate(display_);
    }
    context_ = EGL_NO_CONTEXT;
    display_ = EGL_NO_DISPLAY;
    config_ = nullptr;
}

}  // namespace sf
