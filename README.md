SfImGui
=======

Android 上的即时 UI 框架，基于 Dear ImGui 与 OpenGL ES3。
纯 C/C++ 实现，运行于 android.app.NativeActivity，不包含 Java 源码，
APK 内没有 dex，也不依赖任何第三方运行时。

![platform](https://img.shields.io/badge/platform-Android%209%2B-3DDC84?style=flat-square&logo=android&logoColor=white)
![abi](https://img.shields.io/badge/ABI-arm64--v8a-blue?style=flat-square)
![cxx](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![license](https://img.shields.io/badge/license-MIT-2ea44f?style=flat-square)

简介
----

SfImGui 提供一个可以直接嵌入 Android 原生程序的 ImGui 界面层：由系统把
ANativeWindow 交给 native 代码，EGL 建立 GLES3 上下文，Engine 在独立线程里完成
ImGui 的初始化、布局与提交。宿主只需提供窗口句柄和触摸事件，其余生命周期由框架
自行管理。

项目内置一套完整的设置面板（帧率、缩放、透明度、系统与设备信息）以及一个可展开的
浮动状态条，可直接作为独立应用运行，也可以只取 framework 部分接入已有工程。

特性
----

| 能力 | 说明 |
| --- | --- |
| 渲染 | EGL + OpenGL ES3，eglSwapInterval 垂直同步，双缓冲提交 |
| 宿主 | NativeActivity + android_native_app_glue，窗口由系统直接下发 |
| 输入 | AInputEvent 映射为 ImGui 触摸事件，支持点击、拖动、滑块 |
| 字体 | TrueType 内嵌，按需构建字形图集，无外部资源 |
| 界面 | Liquid Glass 风格，中文菜单，五个功能页签 |
| 监控 | 只读 /proc 与 /sys，采集 CPU、内存、温度 |
| 稳定性 | 渲染线程隔离，窗口重建、启动失败重试、信号级崩溃处理 |
| 体积 | 单 .so，arm64-v8a，APK 约 2.1 MB |

架构
----

| 模块 | 职责 |
| --- | --- |
| src/app/android_main.cpp | native 入口、窗口与输入事件循环 |
| src/overlay/engine.* | 渲染线程、ImGui 生命周期、触摸队列、帧调度 |
| src/overlay/egl_core.* | EGL display / context / surface 封装 |
| src/ui/menu.* | 面板、页签、浮动状态条 |
| src/ui/theme.* | 字号、圆角与配色 |
| src/ui/widgets.* | 进度条、环形图、键值行 |
| src/sys/telemetry.* | CPU、内存、温度采样 |
| src/sys/device_info.* | SoC、ABI、屏幕信息 |
| src/core/* | 时钟、日志、崩溃处理 |

构建
----

  export ANDROID_NDK_HOME=/path/to/android-ndk-r26d
  ./scripts/build.sh arm64-v8a 28

  export ANDROID_SDK_ROOT=/path/to/android-sdk
  export JAVA_HOME=/path/to/jdk-17
  ./scripts/build_apk.sh

产物输出到 dist/。生成的 .so 名称包含构建时间，并写入 DT_SONAME，同时作为
NativeActivity 的 android.app.lib_name；打包脚本会自动完成清单替换。

集成
----

  #include "overlay/engine.h"

  sf::Engine engine;
  sf::OverlayOptions options;
  options.targetFps = 0;        // 0 = follow the display refresh rate
  options.acceptInput = true;

  engine.start(app->window, options);
  engine.touch(0, x, y);        // 0 down, 1 move, 2 up

  const FrameStats stats = engine.stats();
  engine.stop();

Engine 在 start() 后自管渲染线程；updateWindow() 用于切换渲染目标，
stop() 会等待线程退出并释放窗口引用。初始化失败时 running() 返回 false，
宿主可按需重试。

定制
----

| 内容 | 位置 |
| --- | --- |
| 字体 | src/ui/OppoFont.h，需为 TrueType（CFF/TTC 不受支持） |
| 配色与圆角 | src/ui/theme.cpp 的 ApplyTheme() |
| 面板与页签 | src/ui/menu.cpp |
| 默认缩放 | src/overlay/engine.cpp 的 updateScale() |

兼容性
------

  * minSdk 28（Android 9），targetSdk 34
  * arm64-v8a，16 KB 页对齐
  * 依赖系统 EGL / GLES v3，不需要 Vulkan

限制
----

  * 面板绘制在应用自身窗口内，不提供跨应用悬浮
  * ImGui 无实时模糊，Liquid Glass 由渐变底与半透明层近似
  * 字体仅支持 TrueType

致谢
----

Dear ImGui by Omar Cornut，OPPO Sans 字体作者。

许可
----

MIT
