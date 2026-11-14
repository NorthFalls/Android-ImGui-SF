cmake_minimum_required(VERSION 3.20)
project(SfImGui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT ANDROID)
    message(FATAL_ERROR "SfImGui targets Android. Configure with the NDK toolchain file.")
endif()

set(SF_ARTIFACT_NAME "" CACHE STRING "Exact output file name of the overlay library")
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_IMPL_OPENGL_ES3)
target_compile_options(imgui PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)

add_library(sfcore STATIC
    src/core/clock.cpp
    src/core/crash_handler.cpp
    src/sys/telemetry.cpp
    src/sys/device_info.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
    src/ui/menu.cpp
    src/overlay/egl_core.cpp
    src/overlay/engine.cpp
)
target_include_directories(sfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sfcore PUBLIC imgui android log EGL GLESv3)
target_compile_options(sfcore PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Pure C/C++ application: android.app.NativeActivity loads this library and hands
# us the window through android_native_app_glue. No Java, no dex.
add_library(sfapp SHARED
    src/app/android_main.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(sfapp PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
target_link_libraries(sfapp PRIVATE sfcore)
target_link_options(sfapp PRIVATE "-u" "ANativeActivity_onCreate" "-Wl,--gc-sections")

if(SF_ARTIFACT_NAME)
    set_target_properties(sfapp PROPERTIES PREFIX "" SUFFIX "" OUTPUT_NAME "${SF_ARTIFACT_NAME}")
    target_link_options(sfapp PRIVATE "-Wl,-soname,${SF_ARTIFACT_NAME}")
endif()

if(ANDROID_ABI STREQUAL "arm64-v8a")
    target_link_options(sfapp PRIVATE "-Wl,-z,max-page-size=16384")
endif()
