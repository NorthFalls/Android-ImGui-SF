#include "core/trace.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace sf {
namespace {

std::mutex gMutex;
std::string gPath;
bool gHaveStart = false;
timespec gStart = {};

long long nowMillis() {
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!gHaveStart) {
        gStart = ts;
        gHaveStart = true;
    }
    return static_cast<long long>(ts.tv_sec - gStart.tv_sec) * 1000LL +
           (ts.tv_nsec - gStart.tv_nsec) / 1000000LL;
}

}  // namespace

void traceInit(const char* path) {
    std::lock_guard<std::mutex> lock(gMutex);
    gPath = path != nullptr ? path : "";
    gHaveStart = false;
    gStart = {};
    (void)nowMillis();
    if (gPath.empty()) return;
    FILE* file = std::fopen(gPath.c_str(), "wb");
    if (file == nullptr) {
        gPath.clear();
        return;
    }
    std::fprintf(file, "SfImGui startup trace\n");
    std::fclose(file);
}

void trace(const char* format, ...) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gPath.empty()) return;
    FILE* file = std::fopen(gPath.c_str(), "ab");
    if (file == nullptr) return;
    std::fprintf(file, "[%6lld ms] ", nowMillis());
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
}

}  // namespace sf
