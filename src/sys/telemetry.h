#pragma once

#include <cstdint>

namespace sf {

struct CpuStats {
    double usage = 0.0;
    double cores[16] = {};
    int coreCount = 0;
};

struct MemStats {
    uint64_t totalKb = 0;
    uint64_t availableKb = 0;
    double usedFraction = 0.0;
};

struct ThermalStats {
    float socCelsius = 0.0f;
    float batteryCelsius = 0.0f;
    int batteryPercent = -1;
    bool hasSoc = false;
    bool hasBattery = false;
};

// Reads the phone's own /proc and /sys interfaces. Nothing here touches another
// process: this is the device health shown in the HUD.
class Telemetry {
public:
    void sample();
    void setSampleIntervalSeconds(double seconds) { interval_ = seconds; }
    bool due(double now) const { return lastSample_ == 0.0 || now - lastSample_ >= interval_; }

    const CpuStats& cpu() const { return cpu_; }
    const MemStats& mem() const { return mem_; }
    const ThermalStats& thermal() const { return thermal_; }
    double uptimeSeconds() const { return uptime_; }

private:
    void sampleCpu();
    void sampleMem();
    void sampleThermal();

    CpuStats cpu_{};
    MemStats mem_{};
    ThermalStats thermal_{};
    double uptime_ = 0.0;
    double interval_ = 0.5;
    double lastSample_ = 0.0;
    uint64_t prevTotal_ = 0;
    uint64_t prevIdle_ = 0;
    uint64_t prevCoreTotal_[16] = {};
    uint64_t prevCoreIdle_[16] = {};
    bool havePrev_ = false;
};

}  // namespace sf
