#include "sys/device_info.h"

#include <android/api-level.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdlib>

namespace sf {
namespace {

std::string prop(const char* key) {
    char value[PROP_VALUE_MAX] = {};
    const int len = __system_property_get(key, value);
    if (len <= 0) return {};
    return std::string(value, static_cast<size_t>(len));
}

}  // namespace

void DeviceInfo::load() {
    model = prop("ro.product.model");
    manufacturer = prop("ro.product.manufacturer");
    soc = prop("ro.soc.model");
    if (soc.empty()) soc = prop("ro.board.platform");
    fingerprint = prop("ro.build.fingerprint");
    sdkInt = android_get_device_api_level();
    pageSize = static_cast<int>(sysconf(_SC_PAGESIZE));

    std::string density = prop("ro.sf.lcd_density");
    if (density.empty()) density = prop("qemu.sf.lcd_density");
    if (!density.empty()) {
        const int dpi = std::atoi(density.c_str());
        if (dpi >= 120 && dpi <= 1000) densityDpi = dpi;
    }

#if defined(__aarch64__)
    abi = "arm64-v8a";
#elif defined(__arm__)
    abi = "armeabi-v7a";
#elif defined(__x86_64__)
    abi = "x86_64";
#elif defined(__i386__)
    abi = "x86";
#else
    abi = "unknown";
#endif

    utsname uts{};
    if (uname(&uts) == 0) {
        kernel = std::string(uts.sysname) + " " + uts.release + " " + uts.machine;
    }
}

}  // namespace sf
