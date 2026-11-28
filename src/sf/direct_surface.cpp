// Direct SurfaceFlinger surface creation for the root host.
//
// The process is not an app: it is started by root, so it can dlopen the
// platform gui libraries, construct android::SurfaceComposerClient and ask
// SurfaceFlinger for a layer of its own. That layer is handed to
// eglCreateWindowSurface() as an ANativeWindow, which is why the rest of the
// engine (EGL, ImGui, menu, telemetry) never cares where the window came from.
//
// The ABI behind this is private and changes almost every Android release, so
// every entry point is resolved at runtime through dlsym() with a candidate
// list, and createSurface() itself is picked from a table ordered from the
// newest signature (Android 17) down to the oldest (Android 5). All of it is
// reported through ProbeResult so --probe and --symbols can tell exactly where
// a device stops.

#include "sf/direct_surface.h"

#include <dlfcn.h>
#include <elf.h>
#include <stdint.h>
#include <string.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/crash_handler.h"
#include "core/trace.h"

namespace sf {
namespace direct {
namespace {

constexpr int32_t kFormatRgba8888 = 1;
constexpr int32_t kLayerTop = 0x7fffffff;
constexpr uint32_t kOwnerUidSelf = 0xFFFFFFFFu;
constexpr size_t kInPlaceSize = 4096;
constexpr size_t kInPlaceGuard = 32;
constexpr unsigned char kGuardByte = 0xA5;

// A value returned by a function whose real return type is android::sp<T>.
// android::sp<T> is not trivially copyable, so such a function does not return in
// the normal result register: the caller passes the address of a result slot in
// the hidden x8 register and the callee constructs the sp there. Declaring the
// return type as this non-trivial struct makes the compiler pass that hidden
// pointer exactly like the real sp<T> return does, and the extra words after the
// object pointer are slack in case a future sp<T> grows.
// The destructor intentionally does not release the reference: the Surface has to
// outlive every reference we drop here.
struct ReturnedSpStorage {
    void* pointer = nullptr;
    void* refs = nullptr;
    void* reserved[6] = {};
    ~ReturnedSpStorage() {}
};

using SpPtr = ReturnedSpStorage;

// Storage an object is constructed into by the platform constructor itself, so
// the real layout stays on the device side.
struct Block {
    alignas(16) unsigned char bytes[kInPlaceSize];
};

struct Storage {
    Block client;
    Block transaction;
    Block metadata;
    alignas(16) unsigned char name[128];
};

Storage gStorage;

// createSurface() shapes. Each enum value is one call ABI, not one Android
// version: several versions share a shape.
enum class CreateKind {
    Plain,                // (client, name, w, h, format, flags)            -> sp
    WindowType,           // (..., flags, parent, windowType, ownerUid)      -> sp
    Metadata,             // (..., flags, parent, metadata)                  -> sp
    MetadataHint,         // (..., flags, parent, metadata, hint*)           -> sp
    CheckedWindowType,    // (..., out*, flags, parent, windowType, ownerUid) -> status
    CheckedMetadata,      // (..., out*, flags, parent, metadata)             -> status
    CheckedMetadataHint,  // (..., out*, flags, parent, metadata, hint*)      -> status
};

// How the parent handle is passed: most signatures take a const sp<IBinder>&,
// the newer out-parameter ones take a raw SurfaceControl*.
enum class ParentKind { None, Handle, Control };

struct CreateVariant {
    const char* symbol;
    CreateKind kind;
    ParentKind parent;
    int32_t windowType;
};

const CreateVariant kCreateVariants[] = {
    {"_ZN7android21SurfaceComposerClient20createSurfaceCheckedERKNS_7String8EjjiPNS_2spINS_14SurfaceControlEEEiRKNS4_INS_7IBinderEEERKNS_3gui13LayerMetadataEPj",
     CreateKind::CheckedMetadataHint, ParentKind::Handle, 0},
    {"_ZN7android21SurfaceComposerClient20createSurfaceCheckedERKNS_7String8EjjiPNS_2spINS_14SurfaceControlEEEiRKNS4_INS_7IBinderEEENS_3gui13LayerMetadataEPj",
     CreateKind::CheckedMetadataHint, ParentKind::Handle, 0},
    {"_ZN7android21SurfaceComposerClient20createSurfaceCheckedERKNS_7String8EjjiPNS_2spINS_14SurfaceControlEEEjRKNS4_INS_7IBinderEEENS_13LayerMetadataEPj",
     CreateKind::CheckedMetadataHint, ParentKind::Handle, 0},
    {"_ZN7android21SurfaceComposerClient20createSurfaceCheckedERKNS_7String8EjjiPNS_2spINS_14SurfaceControlEEEjPS5_NS_13LayerMetadataEPj",
     CreateKind::CheckedMetadataHint, ParentKind::Control, 0},
    {"_ZN7android21SurfaceComposerClient20createSurfaceCheckedERKNS_7String8EjjiPNS_2spINS_14SurfaceControlEEEjPS5_NS_13LayerMetadataE",
     CreateKind::CheckedMetadata, ParentKind::Control, 0},
    {"_ZN7android21SurfaceComposerClient20createSurfaceCheckedERKNS_7String8EjjiPNS_2spINS_14SurfaceControlEEEjPS5_ii",
     CreateKind::CheckedWindowType, ParentKind::Control, -1},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEERKNS_3gui13LayerMetadataEPj",
     CreateKind::MetadataHint, ParentKind::Handle, 0},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj",
     CreateKind::MetadataHint, ParentKind::Handle, 0},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_13LayerMetadataEPj",
     CreateKind::MetadataHint, ParentKind::Handle, 0},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataEPj",
     CreateKind::MetadataHint, ParentKind::Control, 0},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataE",
     CreateKind::Metadata, ParentKind::Control, 0},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEii",
     CreateKind::WindowType, ParentKind::Control, -1},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEjj",
     CreateKind::WindowType, ParentKind::Control, 0},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8Ejjij",
     CreateKind::Plain, ParentKind::None, 0},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEEij",
     CreateKind::WindowType, ParentKind::Handle, -1},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEEij",
     CreateKind::WindowType, ParentKind::Handle, -1},
    {"_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEEii",
     CreateKind::WindowType, ParentKind::Handle, -1},
};
constexpr size_t kCreateVariantCount = sizeof(kCreateVariants) / sizeof(kCreateVariants[0]);

using String8Ctor = void (*)(void*, const char*);
using String8Dtor = void (*)(void*);
using RefBaseOp = void (*)(void*, const void*);
using Ctor0 = void (*)(void*);
using GetDefaultFn = SpPtr (*)();
using InitCheckFn = int32_t (*)(void*);
using ValidateFn = int32_t (*)(void*);
using GetSurfaceFn = SpPtr (*)(void*);
using MetadataSetFn = void (*)(void*, int32_t, int32_t);
using SetLayerTxFn = void* (*)(void*, const void*, int32_t);
using ShowTxFn = void* (*)(void*, const void*);
using HideTxFn = void* (*)(void*, const void*);
using ApplyTxFn = int32_t (*)(void*, bool);
using ApplyTx2Fn = int32_t (*)(void*, bool, bool);
using OpenGlobalFn = void (*)();
using CloseGlobalFn = void (*)(bool);
using ControlOpFn = void (*)(void*);
using ControlSetLayerFn = void (*)(void*, int32_t);
using UnaryOpFn = void (*)(void*);

using PlainFn = SpPtr (*)(void*, void*, uint32_t, uint32_t, int32_t, uint32_t);
using WindowTypeFn = SpPtr (*)(void*, void*, uint32_t, uint32_t, int32_t, uint32_t, void*, int32_t, int32_t);
using MetadataFn = SpPtr (*)(void*, void*, uint32_t, uint32_t, int32_t, uint32_t, void*, void*);
using MetadataHintFn = SpPtr (*)(void*, void*, uint32_t, uint32_t, int32_t, uint32_t, void*, void*, uint32_t*);
using CheckedWindowTypeFn = int32_t (*)(void*, void*, uint32_t, uint32_t, int32_t, void**, uint32_t, void*, int32_t, int32_t);
using CheckedMetadataFn = int32_t (*)(void*, void*, uint32_t, uint32_t, int32_t, void**, uint32_t, void*, void*);
using CheckedMetadataHintFn = int32_t (*)(void*, void*, uint32_t, uint32_t, int32_t, void**, uint32_t, void*, void*, uint32_t*);

struct Symbols {
    void* utilsHandle = nullptr;
    void* guiHandle = nullptr;
    String8Ctor string8New = nullptr;
    String8Dtor string8Drop = nullptr;
    RefBaseOp retain = nullptr;
    RefBaseOp release = nullptr;
    GetDefaultFn clientDefault = nullptr;
    Ctor0 clientNew = nullptr;
    InitCheckFn clientCheck = nullptr;
    Ctor0 metadataNew = nullptr;
    MetadataSetFn metadataSet = nullptr;
    ValidateFn validate = nullptr;
    GetSurfaceFn getSurface = nullptr;
    UnaryOpFn disconnect = nullptr;
    UnaryOpFn destroyControl = nullptr;
    Ctor0 transactionNew = nullptr;
    Ctor0 transactionCopy = nullptr;
    SetLayerTxFn transactionSetLayer = nullptr;
    ShowTxFn transactionShow = nullptr;
    HideTxFn transactionHide = nullptr;
    ApplyTxFn transactionApply = nullptr;
    ApplyTx2Fn transactionApplyTwo = nullptr;
    ControlSetLayerFn globalSetLayer = nullptr;
    ControlOpFn globalShow = nullptr;
    ControlOpFn globalHide = nullptr;
    OpenGlobalFn globalOpen = nullptr;
    CloseGlobalFn globalClose = nullptr;
    bool hasTransaction = false;
    bool hasGlobal = false;
    std::vector<std::pair<const CreateVariant*, void*>> createVariants;
};

std::mutex gMutex;
bool gLoaded = false;
bool gAttempted = false;
std::string gDetail = "尚未解析";
std::string gLibrary;
std::string gSymbolReport = "未解析";
std::string gVariant;
std::string gLoadedPath;
int gApiLevel = 0;
Symbols gSymbols;
void* gClient = nullptr;

bool NeedsMetadata(CreateKind kind) {
    return kind == CreateKind::Metadata || kind == CreateKind::MetadataHint ||
           kind == CreateKind::CheckedMetadata || kind == CreateKind::CheckedMetadataHint;
}

const char* KindName(CreateKind kind) {
    switch (kind) {
        case CreateKind::Plain: return "Plain";
        case CreateKind::WindowType: return "WindowType";
        case CreateKind::Metadata: return "Metadata";
        case CreateKind::MetadataHint: return "MetadataHint";
        case CreateKind::CheckedWindowType: return "CheckedWindowType";
        case CreateKind::CheckedMetadata: return "CheckedMetadata";
        case CreateKind::CheckedMetadataHint: return "CheckedMetadataHint";
    }
    return "Unknown";
}

int ReadApiLevel() {
    char value[PROP_VALUE_MAX] = {0};
    const int length = __system_property_get("ro.build.version.sdk", value);
    if (length <= 0) return 0;
    return std::atoi(value);
}

std::string Join(const std::vector<std::string>& items, const char* separator) {
    std::string joined;
    for (size_t index = 0; index < items.size(); ++index) {
        if (index != 0) joined += separator;
        joined += items[index];
    }
    return joined;
}

void* FindFirst(void* handle, std::initializer_list<const char*> names) {
    if (handle == nullptr) return nullptr;
    for (const char* name : names) {
        if (void* address = dlsym(handle, name)) return address;
    }
    return nullptr;
}

std::string LastDlError() {
    const char* error = dlerror();
    return error != nullptr ? std::string(error) : std::string("未知错误");
}

std::string Trim(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == '\n' || text[begin] == '\r' || text[begin] == ' ')) ++begin;
    size_t end = text.size();
    while (end > begin && (text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == ' ')) --end;
    return text.substr(begin, end - begin);
}

// Records one step of the direct path three times: as the crash stage label that
// the signal handler prints, in startup.log, and on stderr. The label has to
// outlive this call because the handler reads the same pointer later, hence the
// static buffer.
__attribute__((format(printf, 1, 2))) void Note(const char* format, ...) {
    static char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    sf::setCrashStage(text);
    sf::trace("direct: %s", text);
    std::fprintf(stderr, "[sfimgui] direct: %s\n", text);
    std::fflush(stderr);
}

// Constructs a platform object with the platform's own constructor inside a
// guard checked, zeroed allocation: the object layout is private, so only the
// device knows its real size.
void* ConstructInPlace(const char* what, void* ctorAddress, std::string* error) {
    if (ctorAddress == nullptr) {
        if (error != nullptr) *error = std::string(what) + " 构造入口不可用";
        return nullptr;
    }
    unsigned char* memory = nullptr;
    if (posix_memalign(reinterpret_cast<void**>(&memory), 16, kInPlaceSize + kInPlaceGuard) != 0) {
        if (error != nullptr) *error = std::string(what) + " 内存分配失败";
        return nullptr;
    }
    std::memset(memory, 0, kInPlaceSize + kInPlaceGuard);
    unsigned char* guard = memory + kInPlaceSize;
    std::memset(guard, kGuardByte, kInPlaceGuard);
    const Ctor0 ctor = reinterpret_cast<Ctor0>(ctorAddress);
    ctor(memory);
    for (size_t index = 0; index < kInPlaceGuard; ++index) {
        if (guard[index] != kGuardByte) {
            if (error != nullptr) *error = std::string(what) + " 构造时越界写入，ABI 不匹配";
            std::free(memory);
            return nullptr;
        }
    }
    return memory;
}

// Result slot for the Checked* createSurface signatures, which fill an
// sp<SurfaceControl> through a pointer. One word of slack absorbs a change in the
// size of sp<T> without touching the caller's stack frame.
struct SpSlot {
    void* pointer = nullptr;
    void* extra = nullptr;
};

struct CreateOutcome {
    void* control = nullptr;
    int32_t status = 0;
};

CreateOutcome TryVariant(const CreateVariant& variant, void* createAddress, void* client, void* title,
                         uint32_t width, uint32_t height, uint32_t flags, void* metadata) {
    CreateOutcome outcome;
    if (createAddress == nullptr) {
        outcome.status = -1;
        return outcome;
    }
    const int32_t format = kFormatRgba8888;
    void* noHandle = nullptr;
    void* parent = (variant.parent == ParentKind::Control) ? nullptr : static_cast<void*>(&noHandle);
    const int32_t windowType = variant.windowType;
    SpSlot out;
    void** outControl = &out.pointer;
    uint32_t* hint = nullptr;

    switch (variant.kind) {
        case CreateKind::Plain:
            outcome.control = reinterpret_cast<PlainFn>(createAddress)(client, title, width, height, format,
                                                                      flags).pointer;
            break;
        case CreateKind::WindowType:
            outcome.control = reinterpret_cast<WindowTypeFn>(createAddress)(
                                  client, title, width, height, format, flags, parent, windowType,
                                  static_cast<int32_t>(kOwnerUidSelf)).pointer;
            break;
        case CreateKind::Metadata:
            outcome.control = reinterpret_cast<MetadataFn>(createAddress)(
                                  client, title, width, height, format, flags, parent, metadata).pointer;
            break;
        case CreateKind::MetadataHint:
            outcome.control = reinterpret_cast<MetadataHintFn>(createAddress)(
                                  client, title, width, height, format, flags, parent, metadata, hint).pointer;
            break;
        case CreateKind::CheckedWindowType:
            outcome.status = reinterpret_cast<CheckedWindowTypeFn>(createAddress)(
                                 client, title, width, height, format, outControl, flags, parent, windowType,
                                 static_cast<int32_t>(kOwnerUidSelf));
            outcome.control = out.pointer;
            break;
        case CreateKind::CheckedMetadata:
            outcome.status = reinterpret_cast<CheckedMetadataFn>(createAddress)(
                                 client, title, width, height, format, outControl, flags, parent, metadata);
            outcome.control = out.pointer;
            break;
        case CreateKind::CheckedMetadataHint:
            outcome.status = reinterpret_cast<CheckedMetadataHintFn>(createAddress)(
                                 client, title, width, height, format, outControl, flags, parent, metadata, hint);
            outcome.control = out.pointer;
            break;
    }
    return outcome;
}

bool EnsureLoadedLocked(std::string* error);

void* ConnectLocked(std::string* error) {
    if (gClient != nullptr) return gClient;
    Note("连接 SurfaceFlinger");
    if (gSymbols.clientDefault != nullptr) {
        SpPtr client = gSymbols.clientDefault();
        gClient = client.pointer;
    }
    if (gClient == nullptr && gSymbols.clientNew != nullptr) {
        std::string localError;
        void* object = ConstructInPlace("SurfaceComposerClient", reinterpret_cast<void*>(gSymbols.clientNew),
                                        &localError);
        if (object != nullptr) {
            if (gSymbols.retain != nullptr) gSymbols.retain(object, object);
            gClient = object;
        } else if (error != nullptr) {
            *error = localError;
        }
    }
    if (gClient == nullptr) {
        if (error != nullptr) *error = "无法创建 SurfaceComposerClient：getDefault 与构造函数都不可用";
        return nullptr;
    }
    Note("SurfaceComposerClient=%p", gClient);
    if (gSymbols.clientCheck != nullptr) {
        const int32_t status = gSymbols.clientCheck(gClient);
        if (status != 0) {
            char message[128] = {0};
            std::snprintf(message, sizeof(message), "与 SurfaceFlinger 建立连接失败 status=%d", status);
            if (error != nullptr) *error = message;
            gClient = nullptr;
            return nullptr;
        }
    }
    Note("SurfaceFlinger 连接校验通过");
    return gClient;
}

// Surface buffers travel over binder and the buffer queue calls back into this
// process, so the pool has to exist before EGL is allowed to touch the surface.
// libbinder_ndk is the public NDK entry point; libbinder is the fallback for
// builds that only expose the old ProcessState API.
bool StartBinderThreads() {
    static std::mutex binderMutex;
    static bool started = false;
    std::lock_guard<std::mutex> lock(binderMutex);
    if (started) return true;
    if (void* ndk = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL)) {
        using StartPoolFn = void (*)();
        StartPoolFn start = reinterpret_cast<StartPoolFn>(dlsym(ndk, "ABinderProcess_startThreadPool"));
        if (start != nullptr) {
            start();
            started = true;
            return true;
        }
    }
    if (void* binder = dlopen("libbinder.so", RTLD_NOW | RTLD_LOCAL)) {
        // ProcessState::self() returns sp<ProcessState>, so it comes back through
        // the hidden result pointer like every other non-trivial return here.
        // Declaring it as returning a plain pointer made the callee write the sp
        // through an uninitialised x8 register, which is a fault at address 0x1.
        using SelfFn = SpPtr (*)();
        using StartThreadPoolFn = void (*)(void*);
        void* selfAddress = dlsym(binder, "_ZN7android12ProcessState4selfEv");
        StartThreadPoolFn startPool = reinterpret_cast<StartThreadPoolFn>(
            dlsym(binder, "_ZN7android12ProcessState13startThreadPoolEv"));
        if (selfAddress != nullptr && startPool != nullptr) {
            SpPtr state = reinterpret_cast<SelfFn>(selfAddress)();
            if (state.pointer != nullptr) {
                startPool(state.pointer);
                started = true;
                return true;
            }
        }
    }
    return false;
}

bool PrepareBinderPoolImpl(std::string* detail) {
    Note("启动 binder 线程池");
    const bool started = StartBinderThreads();
    if (started) {
        Note("binder 线程池已启动");
        if (detail != nullptr) *detail = "binder 线程池已启动";
    } else {
        Note("binder 线程池不可用，继续运行");
        if (detail != nullptr) *detail = "binder 线程池不可用";
    }
    return started;
}

bool EnsureLoadedLocked(std::string* error) {
    if (gLoaded) return true;
    if (gAttempted) {
        if (error != nullptr) *error = gDetail;
        return false;
    }
    gAttempted = true;
    gApiLevel = ReadApiLevel();

    std::string utilsError;
    gSymbols.utilsHandle = dlopen("libutils.so", RTLD_NOW);
    if (gSymbols.utilsHandle == nullptr) gSymbols.utilsHandle = dlopen("/system/lib64/libutils.so", RTLD_NOW);
    if (gSymbols.utilsHandle == nullptr) utilsError = LastDlError();

    static const char* const kLibraries[] = {"libgui.so", "/system/lib64/libgui.so", "/system_ext/lib64/libgui.so"};
    std::string libraryError;
    for (const char* candidate : kLibraries) {
        dlerror();
        void* handle = dlopen(candidate, RTLD_NOW);
        if (handle == nullptr) {
            libraryError += std::string(candidate) + ": " + Trim(LastDlError()) + "\n";
            continue;
        }
        void* probe = dlsym(handle, "_ZN7android21SurfaceComposerClient10getDefaultEv");
        if (probe == nullptr) probe = dlsym(handle, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8Ejjij");
        if (probe == nullptr) {
            libraryError += std::string(candidate) + ": 已加载但缺少 SurfaceComposerClient 入口\n";
            continue;
        }
        gSymbols.guiHandle = handle;
        gLibrary = candidate;
        Dl_info info;
        std::memset(&info, 0, sizeof(info));
        if (dladdr(probe, &info) != 0 && info.dli_fname != nullptr) gLoadedPath = info.dli_fname;
        break;
    }
    if (gSymbols.guiHandle == nullptr) {
        gDetail = "无法加载 libgui.so：" + libraryError;
        if (gSymbols.utilsHandle == nullptr) gDetail += "libutils.so 同样不可用: " + utilsError;
        gSymbolReport = "失败";
        if (error != nullptr) *error = gDetail;
        return false;
    }
    if (gLoadedPath.empty()) gLoadedPath = gLibrary;

    std::vector<std::string> picks;
    void* utils = gSymbols.utilsHandle != nullptr ? gSymbols.utilsHandle : gSymbols.guiHandle;
    gSymbols.string8New = reinterpret_cast<String8Ctor>(
        FindFirst(utils, {"_ZN7android7String8C1EPKc", "_ZN7android7String8C2EPKc"}));
    gSymbols.string8Drop = reinterpret_cast<String8Dtor>(
        FindFirst(utils, {"_ZN7android7String8D1Ev", "_ZN7android7String8D2Ev"}));
    gSymbols.retain = reinterpret_cast<RefBaseOp>(FindFirst(utils, {"_ZNK7android7RefBase9incStrongEPKv"}));
    gSymbols.release = reinterpret_cast<RefBaseOp>(FindFirst(utils, {"_ZNK7android7RefBase9decStrongEPKv"}));
    if (gSymbols.string8New == nullptr) {
        gDetail = "libutils.so 中没有 String8 构造函数，无法构造图层名";
        gSymbolReport = "String8=缺失";
        if (error != nullptr) *error = gDetail;
        return false;
    }
    picks.push_back(gSymbols.utilsHandle != nullptr ? "String8=libutils" : "String8=libgui");
    picks.push_back(gSymbols.retain != nullptr ? "RefBase=ok" : "RefBase=缺失");
    picks.push_back("binder=稍后启动");

    gSymbols.clientDefault = reinterpret_cast<GetDefaultFn>(
        FindFirst(gSymbols.guiHandle, {"_ZN7android21SurfaceComposerClient10getDefaultEv"}));
    gSymbols.clientNew = reinterpret_cast<Ctor0>(FindFirst(
        gSymbols.guiHandle, {"_ZN7android21SurfaceComposerClientC1Ev", "_ZN7android21SurfaceComposerClientC2Ev"}));
    gSymbols.clientCheck = reinterpret_cast<InitCheckFn>(
        FindFirst(gSymbols.guiHandle, {"_ZNK7android21SurfaceComposerClient9initCheckEv"}));
    if (gSymbols.clientDefault != nullptr) {
        picks.push_back("SurfaceComposerClient=getDefault");
    } else if (gSymbols.clientNew != nullptr) {
        picks.push_back("SurfaceComposerClient=构造");
    } else {
        gDetail = "libgui.so 已加载，但没有 getDefault 也没有 SurfaceComposerClient 构造函数";
        gSymbolReport = Join(picks, " ") + " SurfaceComposerClient=缺失";
        if (error != nullptr) *error = gDetail;
        return false;
    }

    for (size_t index = 0; index < kCreateVariantCount; ++index) {
        const CreateVariant& variant = kCreateVariants[index];
        void* address = dlsym(gSymbols.guiHandle, variant.symbol);
        if (address != nullptr) gSymbols.createVariants.emplace_back(&variant, address);
    }
    if (gSymbols.createVariants.empty()) {
        char message[224] = {0};
        std::snprintf(message, sizeof(message),
                      "libgui.so 已加载，但 Android 5-17 的 %zu 个 createSurface 变体一个都不存在，"
                      "ROM 可能改写了 SurfaceComposerClient 接口；用 --symbols 查看真实符号名",
                      kCreateVariantCount);
        gDetail = message;
        gSymbolReport = Join(picks, " ") + " createSurface=缺失";
        if (error != nullptr) *error = gDetail;
        return false;
    }
    {
        char message[256] = {0};
        std::snprintf(message, sizeof(message), "createSurface=%zu个可用", gSymbols.createVariants.size());
        picks.push_back(message);
    }
    gVariant = gSymbols.createVariants.front().first->symbol;

    gSymbols.metadataNew = reinterpret_cast<Ctor0>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android3gui13LayerMetadataC1Ev", "_ZN7android3gui13LayerMetadataC2Ev",
         "_ZN7android13LayerMetadataC1Ev", "_ZN7android13LayerMetadataC2Ev"}));
    gSymbols.metadataSet = reinterpret_cast<MetadataSetFn>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android3gui13LayerMetadata8setInt32Eji", "_ZN7android13LayerMetadata8setInt32Eji"}));
    picks.push_back(gSymbols.metadataNew != nullptr ? "LayerMetadata=ok" : "LayerMetadata=缺失");

    gSymbols.validate = reinterpret_cast<ValidateFn>(
        FindFirst(gSymbols.guiHandle, {"_ZNK7android14SurfaceControl8validateEv"}));
    gSymbols.getSurface = reinterpret_cast<GetSurfaceFn>(FindFirst(
        gSymbols.guiHandle, {"_ZN7android14SurfaceControl10getSurfaceEv", "_ZNK7android14SurfaceControl10getSurfaceEv"}));
    gSymbols.disconnect = reinterpret_cast<UnaryOpFn>(
        FindFirst(gSymbols.guiHandle, {"_ZN7android14SurfaceControl10disconnectEv"}));
    gSymbols.destroyControl = reinterpret_cast<UnaryOpFn>(
        FindFirst(gSymbols.guiHandle, {"_ZN7android14SurfaceControl7destroyEv"}));
    if (gSymbols.getSurface == nullptr) {
        gDetail = "libgui.so 中没有 SurfaceControl::getSurface，无法把图层交给 EGL";
        gSymbolReport = Join(picks, " ") + " getSurface=缺失";
        if (error != nullptr) *error = gDetail;
        return false;
    }
    picks.push_back("getSurface=ok");

    gSymbols.transactionNew = reinterpret_cast<Ctor0>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android21SurfaceComposerClient11TransactionC1Ev", "_ZN7android21SurfaceComposerClient11TransactionC2Ev"}));
    gSymbols.transactionCopy = reinterpret_cast<Ctor0>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android21SurfaceComposerClient11TransactionC1ERKS1_",
         "_ZN7android21SurfaceComposerClient11TransactionC2ERKS1_"}));
    gSymbols.transactionSetLayer = reinterpret_cast<SetLayerTxFn>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi"}));
    gSymbols.transactionShow = reinterpret_cast<ShowTxFn>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE"}));
    gSymbols.transactionHide = reinterpret_cast<HideTxFn>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android21SurfaceComposerClient11Transaction4hideERKNS_2spINS_14SurfaceControlEEE"}));
    gSymbols.transactionApplyTwo = reinterpret_cast<ApplyTx2Fn>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android21SurfaceComposerClient11Transaction5applyEbb"}));
    gSymbols.transactionApply = reinterpret_cast<ApplyTxFn>(FindFirst(gSymbols.guiHandle,
        {"_ZN7android21SurfaceComposerClient11Transaction5applyEb"}));
    gSymbols.hasTransaction = gSymbols.transactionSetLayer != nullptr && gSymbols.transactionShow != nullptr &&
                              (gSymbols.transactionNew != nullptr || gSymbols.transactionCopy != nullptr) &&
                              (gSymbols.transactionApply != nullptr || gSymbols.transactionApplyTwo != nullptr);
    if (gSymbols.hasTransaction) {
        picks.push_back(gSymbols.transactionApplyTwo != nullptr ? "Transaction=ok(applyEbb)" : "Transaction=ok(applyEb)");
    } else {
        gSymbols.globalOpen = reinterpret_cast<OpenGlobalFn>(
            FindFirst(gSymbols.guiHandle, {"_ZN7android21SurfaceComposerClient21openGlobalTransactionEv"}));
        gSymbols.globalClose = reinterpret_cast<CloseGlobalFn>(
            FindFirst(gSymbols.guiHandle, {"_ZN7android21SurfaceComposerClient22closeGlobalTransactionEb"}));
        gSymbols.globalSetLayer = reinterpret_cast<ControlSetLayerFn>(FindFirst(
            gSymbols.guiHandle, {"_ZN7android14SurfaceControl8setLayerEi", "_ZN7android14SurfaceControl8setLayerEj"}));
        gSymbols.globalShow = reinterpret_cast<ControlOpFn>(
            FindFirst(gSymbols.guiHandle, {"_ZN7android14SurfaceControl4showEv"}));
        gSymbols.globalHide = reinterpret_cast<ControlOpFn>(
            FindFirst(gSymbols.guiHandle, {"_ZN7android14SurfaceControl4hideEv"}));
        gSymbols.hasGlobal = gSymbols.globalOpen != nullptr && gSymbols.globalClose != nullptr &&
                             gSymbols.globalSetLayer != nullptr && gSymbols.globalShow != nullptr;
        picks.push_back(gSymbols.hasGlobal ? "Transaction=全局事务回退" : "Transaction=缺失");
    }
    {
        char message[64] = {0};
        std::snprintf(message, sizeof(message), "API=%d", gApiLevel);
        picks.push_back(message);
    }
    gSymbolReport = Join(picks, " ");
    if (!gSymbols.hasTransaction && !gSymbols.hasGlobal) {
        gDetail = "SurfaceComposerClient 可解析，但没有任何可用的图层显示事务接口";
        if (error != nullptr) *error = gDetail;
        return false;
    }
    gDetail = "SurfaceComposerClient 已解析（" + gLibrary + "，" + gSymbolReport + "）";
    gLoaded = true;
    sf::trace("direct: loaded %s (%s)", gLoadedPath.c_str(), gSymbolReport.c_str());
    return true;
}

bool PresentLocked(void* control, std::string* error) {
    if (gSymbols.hasTransaction) {
        std::string constructError;
        void* ctor = gSymbols.transactionNew != nullptr ? reinterpret_cast<void*>(gSymbols.transactionNew)
                                                        : reinterpret_cast<void*>(gSymbols.transactionCopy);
        Note("构造显示事务");
        void* transaction = ConstructInPlace("SurfaceComposerClient::Transaction", ctor, &constructError);
        if (transaction == nullptr) {
            if (error != nullptr) *error = constructError;
            return false;
        }
        Note("设置图层层级与可见性");
        gSymbols.transactionSetLayer(transaction, &control, kLayerTop);
        gSymbols.transactionShow(transaction, &control);
        Note("提交显示事务");
        const int32_t status = gSymbols.transactionApplyTwo != nullptr
                                   ? gSymbols.transactionApplyTwo(transaction, true, false)
                                   : gSymbols.transactionApply(transaction, true);
        if (status != 0) {
            char message[128] = {0};
            std::snprintf(message, sizeof(message), "SurfaceFlinger 拒绝了图层显示事务 status=%d", status);
            if (error != nullptr) *error = message;
            return false;
        }
        Note("显示事务已提交");
        return true;
    }
    if (gSymbols.hasGlobal) {
        Note("提交全局显示事务");
        gSymbols.globalOpen();
        gSymbols.globalSetLayer(control, kLayerTop);
        gSymbols.globalShow(control);
        gSymbols.globalClose(true);
        return true;
    }
    if (error != nullptr) *error = "没有可用的图层显示事务接口";
    return false;
}

// android_native_base_t::magic, the first field of every ANativeWindow ('_wnd').
constexpr uint32_t kNativeWindowMagic = 0x5f776e64U;

bool FindNativeWindow(void* surface, ANativeWindow** outWindow, intptr_t* outOffset) {
    if (surface == nullptr) return false;
    const intptr_t candidates[] = {
        2 * static_cast<intptr_t>(sizeof(void*)),  // RefBase first: modern AOSP layout
        static_cast<intptr_t>(sizeof(void*)),     // single-word base
        0,                                        // ANativeWindow first: old layout
        3 * static_cast<intptr_t>(sizeof(void*)), // defensive
    };
    for (const intptr_t offset : candidates) {
        auto* candidate = reinterpret_cast<ANativeWindow*>(static_cast<char*>(surface) + offset);
        uint32_t magic = 0;
        std::memcpy(&magic, candidate, sizeof(magic));
        if (magic == kNativeWindowMagic) {
            *outWindow = candidate;
            *outOffset = offset;
            return true;
        }
    }
    return false;
}
}  // namespace

bool PrepareBinderPool(std::string* detail) {
    std::lock_guard<std::mutex> lock(gMutex);
    return PrepareBinderPoolImpl(detail);
}

ProbeResult Probe() {
    ProbeResult result;
    result.privileged = geteuid() == 0;
    std::lock_guard<std::mutex> lock(gMutex);
    std::string error;
    result.symbolsResolved = EnsureLoadedLocked(&error);
    result.libraryLoaded = !gLibrary.empty();
    result.library = gLibrary;
    result.detail = gLoaded ? gDetail : (error.empty() ? gDetail : error);
    result.symbolReport = gSymbolReport;
    result.variant = gVariant;
    result.apiLevel = gApiLevel;
    result.metadata = gSymbols.metadataNew != nullptr ? "LayerMetadata 构造可用" : "LayerMetadata 缺失（零值构造）";
    return result;
}

bool Create(int width, int height, const char* name, SurfaceHandle* handle, std::string* error) {
    if (handle == nullptr) return false;
    *handle = SurfaceHandle();
    std::lock_guard<std::mutex> lock(gMutex);
    std::string localError;
    if (!EnsureLoadedLocked(&localError)) {
        if (error != nullptr) *error = localError.empty() ? gDetail : localError;
        return false;
    }
    void* client = ConnectLocked(&localError);
    if (client == nullptr) {
        if (error != nullptr) *error = localError;
        return false;
    }
    if (width <= 0 || height <= 0) {
        if (error != nullptr) *error = "表面尺寸无效";
        return false;
    }

    Note("构造图层名 %s", (name != nullptr && name[0] != '\0') ? name : "AndroidSurfaceFlingerImgui");
    std::memset(gStorage.name, 0, sizeof(gStorage.name));
    gSymbols.string8New(gStorage.name, (name != nullptr && name[0] != '\0') ? name : "AndroidSurfaceFlingerImgui");
    std::memset(gStorage.metadata.bytes, 0, sizeof(gStorage.metadata.bytes));
    if (gSymbols.metadataNew != nullptr) gSymbols.metadataNew(gStorage.metadata.bytes);

    const uint32_t flags = 0;
    void* control = nullptr;
    const CreateVariant* used = nullptr;
    std::string attempts;
    int32_t lastStatus = 0;
    Note("创建图层 %dx%d，可用变体 %zu", width, height, gSymbols.createVariants.size());
    size_t attemptIndex = 0;
    for (const auto& entry : gSymbols.createVariants) {
        const CreateVariant& variant = *entry.first;
        ++attemptIndex;
        Note("尝试变体 %zu/%zu：%s", attemptIndex, gSymbols.createVariants.size(), KindName(variant.kind));
        if (NeedsMetadata(variant.kind) && gSymbols.metadataNew == nullptr) {
            attempts += "(跳过:无LayerMetadata) ";
            continue;
        }
        void* meta = (variant.kind == CreateKind::Plain || variant.kind == CreateKind::WindowType)
                         ? nullptr
                         : static_cast<void*>(gStorage.metadata.bytes);
        const CreateOutcome outcome = TryVariant(variant, entry.second, client, gStorage.name,
                                                 static_cast<uint32_t>(width), static_cast<uint32_t>(height), flags,
                                                 meta);
        lastStatus = outcome.status;
        if (outcome.control != nullptr) {
            control = outcome.control;
            used = &variant;
            break;
        }
        attempts += "(变体status=" + std::to_string(outcome.status) + ") ";
    }
    if (control == nullptr) {
        char message[320] = {0};
        std::snprintf(message, sizeof(message),
                      "SurfaceFlinger 拒绝创建 %dx%d 图层，最后状态 %d；尝试记录: %s", width, height, lastStatus,
                      attempts.c_str());
        if (error != nullptr) *error = message;
        sf::trace("direct: create failed %s", message);
        return false;
    }
    gVariant = used->symbol;
    Note("图层已创建 %s", gVariant.c_str());

    if (gSymbols.validate != nullptr) {
        Note("校验图层");
        const int32_t valid = gSymbols.validate(control);
        if (valid != 0) {
            char message[128] = {0};
            std::snprintf(message, sizeof(message), "创建出的 SurfaceControl 未通过校验 status=%d", valid);
            if (error != nullptr) *error = message;
            return false;
        }
    }

    Note("取 Surface");
    SpPtr surface = gSymbols.getSurface(control);
    if (surface.pointer == nullptr) {
        if (error != nullptr) *error = "SurfaceControl::getSurface 返回空表面";
        return false;
    }
    if (!PresentLocked(control, &localError)) {
        if (error != nullptr) *error = localError;
        return false;
    }

    // android::Surface is NOT an ANativeWindow: it derives from
    // ANativeObjectBase<ANativeWindow, Surface, RefBase>, and RefBase (a vptr plus
    // weakref_impl*) occupies the leading 16 bytes, so the ANativeWindow subobject
    // starts at +2*sizeof(void*). Handing EGL the surface base address instead makes
    // every ANativeWindow_* call dispatch through garbage, which is exactly the
    // SIGSEGV at 0x0 / 0x1 earlier builds died with. The base order has moved around
    // across releases, so the subobject is located by checking
    // android_native_base_t::magic ('_wnd') at the known candidate offsets.
    ANativeWindow* window = nullptr;
    intptr_t windowOffset = 0;
    if (!FindNativeWindow(surface.pointer, &window, &windowOffset)) {
        if (error != nullptr) *error = "android::Surface 的 ANativeWindow 布局校验失败（候选偏移都没有 '_wnd' 魔数）";
        return false;
    }
    ANativeWindow_acquire(window);
    Note("Surface=%p ANativeWindow=%p 偏移 %ld 魔数 _wnd", surface.pointer, static_cast<void*>(window),
         static_cast<long>(windowOffset));

    // The layer is created with this size and format already, but the buffer geometry
    // is set explicitly so the vendor window implementation and EGL agree on it.
    const int32_t geometry = ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888);
    Note("setBuffersGeometry(%dx%d, RGBA_8888) = %d", width, height, geometry);

    handle->window = window;
    handle->width = width;
    handle->height = height;
    handle->control = control;
    handle->surface = surface.pointer;
    handle->storage = &gStorage;
    handle->windowOffset = static_cast<int>(windowOffset);
    Note("图层就绪 %dx%d window=%p 报告尺寸 %dx%d variant=%s", width, height, static_cast<void*>(window),
         ANativeWindow_getWidth(window), ANativeWindow_getHeight(window), gVariant.c_str());
    return true;
}

void Destroy(SurfaceHandle* handle) {
    if (handle == nullptr) return;
    std::lock_guard<std::mutex> lock(gMutex);
    if (handle->window != nullptr) ANativeWindow_release(handle->window);
    if (handle->control != nullptr) {
        if (gSymbols.disconnect != nullptr) {
            gSymbols.disconnect(handle->control);
        } else if (gSymbols.destroyControl != nullptr) {
            gSymbols.destroyControl(handle->control);
        }
    }
    *handle = SurfaceHandle();
}

std::string DumpSymbols() {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gLoaded) {
        std::string error;
        EnsureLoadedLocked(&error);
    }
    const std::string path = gLoadedPath.empty() ? std::string("/system/lib64/libgui.so") : gLoadedPath;

    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return "无法读取 " + path + "（" + LastDlError() + "）";
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    if (size <= 0 || size > (64L * 1024L * 1024L)) {
        std::fclose(file);
        return "文件大小异常: " + path;
    }
    std::fseek(file, 0, SEEK_SET);
    std::vector<unsigned char> data(static_cast<size_t>(size));
    const bool read = std::fread(data.data(), 1, data.size(), file) == data.size();
    std::fclose(file);
    if (!read) return "读取失败: " + path;

    Elf64_Ehdr header;
    if (data.size() < sizeof(header)) return "文件过小";
    std::memcpy(&header, data.data(), sizeof(header));
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0) return "不是 ELF 文件: " + path;
    if (header.e_ident[EI_CLASS] != ELFCLASS64) return "不是 64 位 ELF: " + path;
    if (header.e_shoff == 0 || header.e_shnum == 0 || header.e_shentsize < sizeof(Elf64_Shdr)) {
        return "该库没有节头表（可能被精简），请在设备上执行:\n  grep -a -o 'createSurface[A-Za-z0-9_]*' " + path +
               " | sort -u";
    }

    std::set<std::string> names;
    const size_t sectionCount = header.e_shnum;
    for (size_t index = 0; index < sectionCount; ++index) {
        Elf64_Shdr section;
        const size_t offset = static_cast<size_t>(header.e_shoff) + index * header.e_shentsize;
        if (offset + sizeof(section) > data.size()) break;
        std::memcpy(&section, data.data() + offset, sizeof(section));
        if (section.sh_type != SHT_DYNSYM) continue;
        if (section.sh_entsize < sizeof(Elf64_Sym)) continue;
        if (section.sh_link >= sectionCount) continue;
        Elf64_Shdr strings;
        const size_t stringsOffset = static_cast<size_t>(header.e_shoff) + section.sh_link * header.e_shentsize;
        if (stringsOffset + sizeof(strings) > data.size()) continue;
        std::memcpy(&strings, data.data() + stringsOffset, sizeof(strings));
        if (strings.sh_offset + strings.sh_size > data.size()) continue;
        const char* strtab = reinterpret_cast<const char*>(data.data() + strings.sh_offset);
        const size_t strtabSize = static_cast<size_t>(strings.sh_size);
        const size_t symbolCount = static_cast<size_t>(section.sh_size) / section.sh_entsize;
        for (size_t symbolIndex = 0; symbolIndex < symbolCount; ++symbolIndex) {
            Elf64_Sym symbol;
            const size_t symbolOffset = static_cast<size_t>(section.sh_offset) + symbolIndex * section.sh_entsize;
            if (symbolOffset + sizeof(symbol) > data.size()) break;
            std::memcpy(&symbol, data.data() + symbolOffset, sizeof(symbol));
            if (symbol.st_name == 0 || symbol.st_name >= strtabSize) continue;
            const unsigned int type = ELF64_ST_TYPE(symbol.st_info);
            if (type != STT_FUNC && type != STT_OBJECT && type != STT_NOTYPE) continue;
            const char* symbolName = strtab + symbol.st_name;
            const size_t maxLength = strtabSize - symbol.st_name;
            const size_t length = strnlen(symbolName, maxLength);
            if (length == 0 || length >= maxLength) continue;
            const std::string name(symbolName, length);
            static const char* const kNeedles[] = {"SurfaceComposer", "SurfaceControl", "LayerMetadata",
                                                   "Transaction",      "createSurface",   "getSurface",
                                                   "String8",          "RefBase",         "LayerState"};
            bool wanted = false;
            for (const char* needle : kNeedles) {
                if (name.find(needle) != std::string::npos) {
                    wanted = true;
                    break;
                }
            }
            if (wanted) names.insert(name);
        }
    }
    if (names.empty()) return "库 " + path + " 中没有找到相关符号（节头表可能不完整）";
    std::string output = "库 " + path + " 中与 SurfaceFlinger 相关的动态符号（" + std::to_string(names.size()) + " 个）:\n";
    size_t printed = 0;
    for (const std::string& name : names) {
        if (printed >= 400) {
            output += "  ...（仅显示前 400 个）\n";
            break;
        }
        output += "  " + name + "\n";
        ++printed;
    }
    return output;
}

}  // namespace direct
}  // namespace sf
