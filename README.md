AndroidSurfaceFlingerImgui
==========================

Android 原生 ImGui 界面框架 · 纯 C/C++ · SurfaceFlinger 直连 · 不经过 WindowManager · 无 Java 与 dex

（框架代号 SfImGui，源码命名空间 sf。）

简介
----

AndroidSurfaceFlingerImgui（框架代号 SfImGui）提供一个可以直接运行在 Android 设备上的原生 ImGui 界面层。
它不创建应用窗口，也不继承 Activity：进程启动后动态解析系统图形库 `libgui.so` 的 SurfaceComposerClient 接口，
直接创建 Surface 并提交给 SurfaceFlinger 合成器，用 EGL + OpenGL ES3 渲染，触摸事件从 `/dev/input/event*`
的 evdev 节点直接读取。最终产物是单个 arm64-v8a 可执行文件 `sf-imgui.sh`，以 root 运行即成为覆盖在所有应用
之上的系统级悬浮界面。

界面是一套完整的 Liquid Glass 设计系统：半透明玻璃面板、可展开的灵动岛 HUD、实时 CPU/内存/温度/电池遥测、
明暗主题与缩放控制，全部由 Dear ImGui 即时模式绘制，不依赖 Java 组件、dex 或任何第三方运行时。

特性
----

| 能力 | 说明 |
|------|------|
| 直连合成器 | dlopen `libgui.so`，解析 SurfaceComposerClient，创建 Surface 直接提交给 SurfaceFlinger |
| 无窗口管理 | 不经过 WindowManager，没有 Activity、窗口令牌或输入法通道 |
| 单文件交付 | 产物是一个自包含可执行文件，`adb push` 到设备即可运行 |
| 系统级悬浮 | 图层以最高 Z 序显示，透明背景叠加在任意应用之上 |
| 原生触摸 | 直接读取 `/dev/input/event*` 的 evdev 多点触控，不依赖 InputFlinger |
| 实时遥测 | 只读 `/proc` 与 `/sys`，采集 CPU、内存、温度、电池与充电状态 |
| 明暗主题 | 同一套 Liquid Glass 设计语言的浅色与深色两套配色 |
| 启动跟踪 | 每一步写入 `startup.log`，原生崩溃生成 `native_crash.txt` 回溯 |
| 云端构建 | GitHub Actions 只安装 NDK 即可产出可执行文件与 SHA256SUMS |

架构
----

| 路径 | 职责 |
|------|------|
| src/app/main_bin.cpp | root 宿主入口：参数解析、屏幕探测、表面创建、事件循环 |
| src/sf/direct_surface.* | 动态解析 libgui.so，创建与销毁 SurfaceComposerClient 图层 |
| src/sf/evdev.* | 直接读取 /dev/input/event* 的多点触控 |
| src/overlay/engine.* | 渲染线程、EGL 生命周期、触摸队列、设置持久化 |
| src/overlay/egl_core.* | EGL 显示、配置、上下文与表面管理 |
| src/ui/* | Liquid Glass 主题、面板、灵动岛、图表与控件 |
| src/sys/* | CPU、内存、温度、电池遥测与设备信息 |
| src/core/* | 时钟、日志、启动跟踪、崩溃处理 |
| third_party/imgui | Dear ImGui 与 OpenGL ES3 后端 |

工作原理
--------

  1. 宿主以 root 启动，检查 uid、创建运行目录，初始化启动跟踪与崩溃处理。
  2. 探测屏幕尺寸：依次尝试 `wm size`、`/sys/class/graphics/fb0/virtual_size`、`/sys/class/drm/*/modes`。
  3. `dlopen` 系统图形库，按 Itanium ABI 逐个解析 String8、SurfaceComposerClient、createSurface、
     Transaction 等入口；同一入口保留多个候选，兼容不同 Android 版本的签名变化。
  4. 构造 SurfaceComposerClient，调用 createSurface 申请 RGBA_8888 图层，取回 Surface 指针。
  5. Surface 本身即 `ANativeWindow*`，直接交给 `eglCreateWindowSurface`，复用引擎的 EGL 渲染线程。
  6. 通过 Transaction 设置图层 Z 序、位置并显示，图层开始被 SurfaceFlinger 合成。
  7. 主线程轮询 evdev 触摸事件，映射到 ImGui 输入队列；透明清屏让下层应用保持可见。

构建
----

  export ANDROID_NDK_HOME=/path/to/android-ndk-r26d
  ./scripts/build.sh arm64-v8a 28

  # 产物
  dist/sf-imgui.sh

也可以在仓库 Actions 页面运行 `build` 工作流：云端只安装 NDK r26d，执行同一套 CMake 配置，
产出 `dist/sf-imgui.sh` 与 `SHA256SUMS`；填写 tag 与发布时间后自动创建 Release，构建机不留本地文件。

使用
----

  adb push sf-imgui.sh /data/local/tmp/sf-imgui.sh
  adb shell su -c "chmod 755 /data/local/tmp/sf-imgui.sh"

  # 逐步诊断：uid、图形库、符号、屏幕、触摸、表面创建
  adb shell su -c "/data/local/tmp/sf-imgui.sh --probe"

  # 启动悬浮界面
  adb shell su -c "/data/local/tmp/sf-imgui.sh"

  # 退出
  adb shell su -c "killall sf-imgui.sh"

文件名带 `.sh` 只是命名约定，实际是静态链接 C++ 运行时的 ELF 可执行文件，直接执行即可，不需要 `sh` 解释器。

| 参数 | 说明 |
|------|------|
| --probe | 打印 uid、图形库加载、符号解析、屏幕尺寸、触摸设备与表面创建结果后退出 |
| --opaque | 换成不透明背景，便于截图（默认透明，能看到下层应用） |
| --width / --height | 覆盖自动探测的屏幕尺寸 |
| --fps / --scale / --opacity | 帧率上限、界面缩放、界面不透明度 |
| --duration | 运行指定秒数后自动退出，便于验证 |
| --no-input | 不读取触摸设备 |
| --reset | 忽略已保存的界面设置 |
| --base / --config | 运行目录与设置文件，默认 /data/local/tmp/sf-imgui |
| --name | SurfaceFlinger 图层名 |
| -h / --help | 显示帮助 |

运行文件
--------

运行目录默认为 `/data/local/tmp/sf-imgui`，可用 `--base` 修改：

| 文件 | 内容 |
|------|------|
| settings.cfg | 帧率上限、缩放、不透明度、主题、灵动岛开关 |
| startup.log | 启动跟踪，每一步一行，崩溃前最后一行即断点 |
| native_crash.txt | SIGSEGV/SIGABRT 等信号的寄存器与调用栈回溯 |

所有输出同时打印在启动它的终端上，这是该架构相对 APK 的最大优势：失败可读、可定位。

兼容性
------

  * 目标 ABI 为 arm64-v8a，主要验证 Android 10 及以上；符号候选覆盖 Android 10 到 15。
  * 需要 root（uid 0），并且 SELinux 允许进程 dlopen `/system/lib64/libgui.so`。
  * 屏幕尺寸三级自动探测，探测不到时回退 1080x2400，可用参数覆盖。
  * 透明合成依赖 RGBA_8888 图层；硬件合成器不支持时由 GPU 合成，帧率略有下降。

限制
----

  * SurfaceComposerClient 是私有 ABI，不同 Android 版本与厂商 ROM 的符号和结构体布局存在差异；
    失败时用 `--probe` 定位具体缺哪一步，必要时补充符号候选。
  * 图层按屏幕自然方向创建，横屏场景需要显式指定尺寸并在启动后保持方向。
  * 不独占输入：未抓取设备时触摸事件同时下发给下层应用。
  * 没有 Activity 生命周期：旋转、熄屏、分辨率变化后需要重新启动进程。
  * 仅面向 root 设备，不提供应用商店分发。

更新
----

4.1.3
-----
- 修复真正的根因：`android::Surface` 不是 `ANativeWindow` 本体，`ANativeWindow` 子对象位于对象起始地址 +16 字节（`RefBase` 的 vptr 与 `weakref_impl*`），旧版本把 Surface 地址直接当窗口交给 EGL，第一次 `ANativeWindow_*` 调用就 SIGSEGV（0x0 / 0x1）。
- 新增运行时布局探测：按候选偏移校验 `android_native_base_t::magic`（`'_wnd'`），命中后才 acquire，并打印命中偏移、窗口地址和 EGL 报告的尺寸。
- 恢复 `ANativeWindow_setBuffersGeometry(RGBA_8888)`，并把返回值写进终端与启动日志。
- 宿主进程增加阶段打印：构造渲染引擎 / 启动渲染线程 / 渲染线程已启动，崩溃记录的 stage 字段因此能精确到步骤。
- `Destroy()` 释放取到的 `ANativeWindow` 引用，图层销毁后不再残留窗口引用。
- 版本号 4.1.3。

4.1.2
  * 修复 sp<T> 返回值存储不足：SurfaceFlinger 返回的 sp 是两个字，八字节的临时对象会被写穿，可能破坏调用方栈帧

- 修复 root 宿主在创建 Surface 之后立刻 SIGSEGV（signal 11 at 0x1）的问题：不再调用 ANativeWindow_setBuffersGeometry()，图层的尺寸与 RGBA_8888 格式在 createSurface 时已经确定，重复设置会进入厂商窗口实现。
- ProcessState::self() 现在按非平凡返回值（sp<ProcessState>）解析，之前按普通指针调用会把 sp 写进未初始化的隐藏结果寄存器。
- binder 线程池改为在 Surface 创建成功之后、启动渲染线程之前单独启动，创建 Surface 的路径与已知可用的版本保持一致。
- 直连路径每一步（连接 SurfaceFlinger、构造图层名、每个 createSurface 变体、校验、取 Surface、显示事务）都写入崩溃阶段标记、startup.log 与终端，后续任何崩溃都能定位到具体调用。
- 版本号 4.1.2。

4.1.1
  * 修复 sp<T> 返回值存储不足：SurfaceFlinger 返回的 sp 是两个字，八字节的临时对象会被写穿，可能破坏调用方栈帧
  * 启动前建立 binder 线程池（libbinder_ndk，回退 libbinder），Surface 的缓冲区队列回调不再依赖调用线程
  * 创建图层后显式设置缓冲区几何（RGBA_8888 + 表面尺寸），避免部分驱动读到 0x0 几何后首次分配出错
  * 引擎初始化改为逐阶段输出：终端、startup.log 与崩溃记录共用同一阶段名，闪退时能直接看到停在哪一步
  * 崩溃处理安装独立信号栈，signal、地址、阶段与回溯同时写到 stderr 和 native_crash.txt
  * 启动时先打印上次崩溃记录并轮转为 native_crash_prev.txt，不必把文件从设备拷出来
  * 触摸设备改在渲染线程之前打开，终端输出顺序与真实初始化顺序一致
  * EGL 建面失败时打印 eglGetError 具体错误码
  * 渲染线程异常退出时返回码改为 4，不再伪装成正常结束
  * 版本号 4.1.1

4.1.0
  * 重写 libgui.so 动态解析：createSurface/createSurfaceChecked 变体表覆盖 Android 5 至 Android 17，含 gui::LayerMetadata 与旧版 LayerMetadata 两种命名空间
  * SurfaceComposerClient 优先 getDefault，其次构造函数，并调用 initCheck 校验与 SurfaceFlinger 的连接
  * String8 与 RefBase 引用计数改从 libutils.so 解析，Transaction 与全局事务两条显示路径都可用
  * 建面时按变体顺序逐个尝试，成功后才提交显示事务；失败时终端列出每个变体的 status
  * 新增 --symbols：直接在设备上转储 libgui.so 中与 SurfaceFlinger 相关的动态符号，便于定位被改写的 ABI
  * --probe 增加 API 级别、选中的 createSurface 变体与 LayerMetadata 可用性
  * 版本号 4.1.0

4.0.0
  * 项目只保留二进制可执行文件架构，移除 APK/NativeActivity 宿主、AndroidManifest、签名与 build-tools 打包链
  * 产物重命名为 sf-imgui.sh（ELF 可执行文件），CI 只安装 NDK，构建时间与发布附件大幅精简
  * Release 附件只剩 sf-imgui.sh 与 SHA256SUMS
  * README 重写为二进制架构说明：libgui.so 动态解析、SurfaceComposerClient 建面、EGL 渲染、evdev 触摸
  * 显示页移除实验性直连开关，改为常驻后端说明与 libgui.so 重新探测按钮
  * 运行目录默认改为 /data/local/tmp/sf-imgui
  * 版本号 4.0.0

3.0.0
  * 首次提供 root 二进制宿主，实现不经过 WindowManager 的 SurfaceFlinger 直连
  * 复用同一套引擎、菜单、主题与遥测，evdev 多点触控，透明图层
  * 版本号 54 / 3.0.0

致谢
----

  * AOSP 的 SurfaceFlinger、SurfaceComposerClient 与 libgui 实现
  * Dear ImGui 及其 OpenGL ES3 后端
  * OPPO Sans 字体

许可
----

MIT，见 LICENSE。
