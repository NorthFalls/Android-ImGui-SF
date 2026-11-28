cmake_minimum_required(VERSION 3.20)
project(SfImGui VERSION 4.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT ANDROID)
    message(FATAL_ERROR "SfImGui targets Android. Configure with the NDK toolchain file.")
endif()

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_IMPL_OPENGL_ES3)
target_compile_options(imgui PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)

add_library(sfcore STATIC
    src/core/clock.cpp
    src/core/crash_handler.cpp
    src/core/trace.cpp
    src/sys/telemetry.cpp
    src/sys/device_info.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
    src/ui/menu.cpp
    src/overlay/egl_core.cpp
    src/overlay/engine.cpp
    src/sf/direct_surface.cpp
    src/sf/evdev.cpp
)
target_include_directories(sfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sfcore PUBLIC imgui android log EGL GLESv3 dl)
target_compile_options(sfcore PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Root host: the only supported deployment. The process asks SurfaceFlinger for
# its own layer through libgui.so instead of inheriting a WindowManager window,
# so the deliverable is one self-contained executable, not an APK.
add_executable(sfimgui src/app/main_bin.cpp)
target_link_libraries(sfimgui PRIVATE sfcore)
target_link_options(sfimgui PRIVATE "-Wl,--gc-sections")
target_compile_definitions(sfimgui PRIVATE SF_VERSION="${PROJECT_VERSION}")
set_target_properties(sfimgui PROPERTIES PREFIX "" SUFFIX "" OUTPUT_NAME "sf-imgui.sh")
if(ANDROID_ABI STREQUAL "arm64-v8a")
    target_link_options(sfimgui PRIVATE "-Wl,-z,max-page-size=16384")
endif()
