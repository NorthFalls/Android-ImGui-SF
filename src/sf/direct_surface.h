#pragma once

#include <android/native_window.h>

#include <string>

namespace sf {
namespace direct {

// Runtime probe: can this process load a gui library and resolve the
// SurfaceComposerClient entry points? Used by --probe to explain exactly what
// the direct path needs on this device.
struct ProbeResult {
    bool privileged = false;
    bool libraryLoaded = false;
    bool symbolsResolved = false;
    std::string library;
    std::string detail;
    // Per entry point resolution report, printed by the root host's --probe mode.
    std::string symbolReport;
    // The createSurface() variant that was picked, the device API level, and
    // whether gui::LayerMetadata can be constructed.
    std::string variant;
    int apiLevel = 0;
    std::string metadata;
};

ProbeResult Probe();

// Dumps the dynamic symbols of the loaded gui library that belong to the
// SurfaceFlinger client surface, so a ROM with a rewritten ABI can be inspected
// on the device itself without readelf.
std::string DumpSymbols();

// Starts the binder thread pool that the surface's buffer queue calls back on.
// Kept out of Create() so the surface creation path stays exactly the path that
// is known to work on devices where the direct layer already succeeded.
bool PrepareBinderPool(std::string* detail);

// A Surface created straight on SurfaceFlinger, wrapped as the ANativeWindow
// that eglCreateWindowSurface() needs. The handle owns the SurfaceControl and
// Surface references until Destroy() is called.
struct SurfaceHandle {
    ANativeWindow* window = nullptr;
    int width = 0;
    int height = 0;
    void* control = nullptr;
    void* surface = nullptr;
    void* storage = nullptr;
    // Byte offset of the ANativeWindow subobject inside android::Surface; recorded
    // because the layout differs between releases and --probe reports it.
    int windowOffset = 0;
};

// Creates the surface and shows it through a SurfaceFlinger transaction.
// On failure the handle stays empty and error receives a Chinese description.
bool Create(int width, int height, const char* name, SurfaceHandle* handle, std::string* error);

// Tears the surface down. The caller must have detached EGL from handle->window
// (Engine::stop()) before calling this.
void Destroy(SurfaceHandle* handle);

}  // namespace direct
}  // namespace sf
