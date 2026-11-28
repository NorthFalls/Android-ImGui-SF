#pragma once

#include <string>

namespace sf {

struct DeviceInfo {
    std::string model;
    std::string manufacturer;
    std::string soc;
    std::string abi;
    std::string kernel;
    std::string fingerprint;
    int sdkInt = 0;
    int pageSize = 4096;
    int densityDpi = 160;

    void load();
};

}  // namespace sf
