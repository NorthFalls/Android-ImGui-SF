#include "sys/telemetry.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

#include "core/clock.h"

namespace sf {
namespace {

bool readWhole(const char* path, char* out, size_t cap) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    const size_t n = std::fread(out, 1, cap - 1, f);
    std::fclose(f);
    if (n == 0) return false;
    out[n] = 0;
    return true;
}

bool readLong(const char* path, long* value) {
    char buf[64];
    if (!readWhole(path, buf, sizeof(buf))) return false;
    return std::sscanf(buf, "%ld", value) == 1;
}

}  // namespace

void Telemetry::sample() {
    sampleCpu();
    sampleMem();
    sampleThermal();

    char buf[64];
    if (readWhole("/proc/uptime", buf, sizeof(buf))) {
        double up = 0.0;
        if (std::sscanf(buf, "%lf", &up) == 1) uptime_ = up;
    }
    lastSample_ = nowSeconds();
}

void Telemetry::sampleCpu() {
    FILE* f = std::fopen("/proc/stat", "r");
    if (!f) return;

    char line[512];
    uint64_t total = 0;
    uint64_t idle = 0;
    int core = 0;

    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "cpu", 3) != 0) break;

        unsigned long long user = 0, nice = 0, system = 0, idleAll = 0, iowait = 0,
                            irq = 0, softirq = 0, steal = 0;
        const bool aggregate = (line[3] == ' ');
        const int fields =
            std::sscanf(line + 3, "%llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice,
                        &system, &idleAll, &iowait, &irq, &softirq, &steal);
        if (fields < 4) continue;

        const uint64_t busy = user + nice + system + irq + softirq + steal;
        const uint64_t idleAllJiffies = idleAll + iowait;

        if (aggregate) {
            total = busy + idleAllJiffies;
            idle = idleAllJiffies;
        } else if (core < 16) {
            const uint64_t coreTotal = busy + idleAllJiffies;
            if (havePrev_ && coreTotal > prevCoreTotal_[core]) {
                const uint64_t dt = coreTotal - prevCoreTotal_[core];
                const uint64_t di = idleAllJiffies - prevCoreIdle_[core];
                cpu_.cores[core] =
                    dt ? static_cast<double>(dt - di) / static_cast<double>(dt) : 0.0;
            }
            prevCoreTotal_[core] = coreTotal;
            prevCoreIdle_[core] = idleAllJiffies;
            ++core;
        }
    }
    std::fclose(f);
    cpu_.coreCount = core;

    if (total == 0) return;
    if (havePrev_ && total > prevTotal_) {
        const uint64_t dt = total - prevTotal_;
        const uint64_t di = idle - prevIdle_;
        cpu_.usage = dt ? static_cast<double>(dt - di) / static_cast<double>(dt) : 0.0;
    }
    prevTotal_ = total;
    prevIdle_ = idle;
    havePrev_ = true;
}

void Telemetry::sampleMem() {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return;

    char line[256];
    uint64_t total = 0;
    uint64_t available = 0;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long kb = 0;
        if (std::sscanf(line, "MemTotal: %llu kB", &kb) == 1) total = kb;
        if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1) available = kb;
    }
    std::fclose(f);

    mem_.totalKb = total;
    mem_.availableKb = available;
    mem_.usedFraction =
        total ? static_cast<double>(total - (available ? available : 0)) / static_cast<double>(total)
              : 0.0;
}

void Telemetry::sampleThermal() {
    DIR* dir = opendir("/sys/class/thermal");
    thermal_.hasSoc = false;
    thermal_.socCelsius = 0.0f;
    if (dir) {
        float hottest = -1000.0f;
        while (dirent* entry = readdir(dir)) {
            if (std::strncmp(entry->d_name, "thermal_zone", 12) != 0) continue;
            char path[256];
            std::snprintf(path, sizeof(path), "/sys/class/thermal/%s/type", entry->d_name);
            char type[128] = {};
            if (!readWhole(path, type, sizeof(type))) continue;
            if (std::strstr(type, "battery") || std::strstr(type, "charger")) continue;

            std::snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", entry->d_name);
            long millis = 0;
            if (!readLong(path, &millis)) continue;
            const float celsius = static_cast<float>(millis) / 1000.0f;
            if (celsius <= -40.0f || celsius >= 200.0f) continue;
            if (celsius > hottest) hottest = celsius;
        }
        closedir(dir);
        if (hottest > -1000.0f) {
            thermal_.socCelsius = hottest;
            thermal_.hasSoc = true;
        }
    }

    long capacity = -1;
    if (readLong("/sys/class/power_supply/battery/capacity", &capacity)) {
        thermal_.batteryPercent = static_cast<int>(capacity);
        thermal_.hasBattery = true;
    }
    long temp = 0;
    thermal_.batteryCelsius = 0.0f;
    if (readLong("/sys/class/power_supply/battery/temp", &temp)) {
        thermal_.batteryCelsius = static_cast<float>(temp) / 10.0f;
        thermal_.hasBattery = true;
    }
}

}  // namespace sf
