LOCAL_PATH := $(call my-dir)
IMGUI := $(LOCAL_PATH)/third_party/imgui

include $(CLEAR_VARS)
LOCAL_MODULE := imgui
LOCAL_SRC_FILES := third_party/imgui/imgui.cpp \
                   third_party/imgui/imgui_draw.cpp \
                   third_party/imgui/imgui_tables.cpp \
                   third_party/imgui/imgui_widgets.cpp \
                   third_party/imgui/backends/imgui_impl_vulkan.cpp \
                   third_party/imgui/backends/imgui_impl_android.cpp
LOCAL_C_INCLUDES := $(IMGUI) $(IMGUI)/backends
LOCAL_CPPFLAGS := -std=c++17 -DIMGUI_DEFINE_MATH_OPERATORS -DVK_USE_PLATFORM_ANDROID_KHR -fno-exceptions
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := sfcore
LOCAL_SRC_FILES := src/core/clock.cpp \
                   src/core/crash_handler.cpp \
                   src/sys/telemetry.cpp \
                   src/sys/device_info.cpp \
                   src/ui/theme.cpp \
                   src/ui/widgets.cpp \
                   src/ui/menu.cpp \
                   src/overlay/vulkan_core.cpp \
                   src/overlay/engine.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/src $(IMGUI)
LOCAL_CPPFLAGS := -std=c++17 -DIMGUI_DEFINE_MATH_OPERATORS -DVK_USE_PLATFORM_ANDROID_KHR -fno-exceptions -Wall
LOCAL_STATIC_LIBRARIES := imgui
LOCAL_LDLIBS := -llog -landroid -lvulkan
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := sfimgui
LOCAL_SRC_FILES := src/overlay/jni_bridge.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/src $(IMGUI)
LOCAL_CPPFLAGS := -std=c++17 -fno-exceptions
LOCAL_STATIC_LIBRARIES := sfcore imgui
LOCAL_LDLIBS := -llog -landroid -lvulkan
include $(BUILD_SHARED_LIBRARY)
