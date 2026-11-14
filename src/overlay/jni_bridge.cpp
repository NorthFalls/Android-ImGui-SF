#include <jni.h>

#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>

#include <string>

#include "core/crash_handler.h"
#include "core/log.h"
#include "imgui.h"
#include "overlay/engine.h"

namespace {

sf::Engine gEngine;

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    SF_LOGI("SfImGui native library loaded");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sfimgui_Overlay_nativeStart(JNIEnv* env, jclass /*clazz*/, jobject surface, jint fps) {
    if (!surface) {
        SF_LOGE("nativeStart: null Surface");
        return JNI_FALSE;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        SF_LOGE("nativeStart: ANativeWindow_fromSurface failed");
        return JNI_FALSE;
    }

    sf::OverlayOptions options;
    options.targetFps = fps > 0 ? fps : 0;  // 0 = follow the display
    options.artifactName = "sf";
    const bool ok = gEngine.start(window, options);
    ANativeWindow_release(window);

    if (!ok) {
        SF_LOGE("nativeStart: engine failed to start");
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_sfimgui_Overlay_nativeSetSurface(JNIEnv* env, jclass /*clazz*/, jobject surface) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    gEngine.updateWindow(window);
    if (window) ANativeWindow_release(window);
}

extern "C" JNIEXPORT void JNICALL Java_com_sfimgui_Overlay_nativeStop(JNIEnv*, jclass) {
    gEngine.stop();
}

extern "C" JNIEXPORT void JNICALL
Java_com_sfimgui_Overlay_nativeInstallCrashHandler(JNIEnv* env, jclass, jstring path) {
    if (!path) return;
    const char* chars = env->GetStringUTFChars(path, nullptr);
    if (chars) {
        sf::installCrashHandler(chars);
        env->ReleaseStringUTFChars(path, chars);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sfimgui_Overlay_nativeSetAssetManager(JNIEnv* env, jclass, jobject manager) {
    AAssetManager* nativeManager = manager ? AAssetManager_fromJava(env, manager) : nullptr;
    gEngine.setAssetManager(nativeManager);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sfimgui_Overlay_nativeSetFontPath(JNIEnv* env, jclass, jstring path) {
    if (!path) return;
    const char* chars = env->GetStringUTFChars(path, nullptr);
    if (chars) {
        gEngine.setFontPath(chars);
        env->ReleaseStringUTFChars(path, chars);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sfimgui_Overlay_nativeTouch(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    gEngine.touch(action, x, y);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sfimgui_Overlay_nativeSetFrameCap(JNIEnv*, jclass, jint fps) {
    sf::OverlayOptions options = gEngine.options();
    options.targetFps = fps;
    gEngine.setOptions(options);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sfimgui_Overlay_nativeSetAcceptInput(JNIEnv*, jclass, jboolean accept) {
    sf::OverlayOptions options = gEngine.options();
    options.acceptInput = accept != JNI_FALSE;
    gEngine.setOptions(options);
    if (!options.acceptInput) gEngine.resetTouch();
}

extern "C" JNIEXPORT jfloatArray JNICALL Java_com_sfimgui_Overlay_nativeStats(JNIEnv* env, jclass) {
    const sf::FrameStats stats = gEngine.stats();
    jfloat values[3] = {stats.fps, stats.frameMs, static_cast<jfloat>(stats.frames)};
    jfloatArray array = env->NewFloatArray(3);
    if (array) env->SetFloatArrayRegion(array, 0, 3, values);
    return array;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_sfimgui_Overlay_nativeStatus(JNIEnv* env, jclass) {
    const sf::FrameStats stats = gEngine.stats();
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "running=%d fps=%.0f frames=%llu init=%dms font=%s err=%s",
                  gEngine.running() ? 1 : 0, stats.fps,
                  static_cast<unsigned long long>(stats.frames), gEngine.initMillis(),
                  gEngine.fontInfo().c_str(),
                  gEngine.lastError().empty() ? "-" : gEngine.lastError().c_str());
    return env->NewStringUTF(buffer);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_sfimgui_Overlay_nativeBuildInfo(JNIEnv* env, jclass) {
    std::string info = std::string("SfImGui / ImGui ") + IMGUI_VERSION;
    return env->NewStringUTF(info.c_str());
}
