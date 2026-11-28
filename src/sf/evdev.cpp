#include "sf/evdev.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/trace.h"

namespace sf {
namespace evdev {
namespace {

constexpr int kMaxSlots = 16;
constexpr int kMaxReadsPerPoll = 256;

int MapAxis(int value, int minimum, int maximum, int target) {
    if (maximum <= minimum || target <= 0) return 0;
    const long long offset = static_cast<long long>(value) - minimum;
    long long mapped = offset * target / (maximum - minimum);
    if (mapped < 0) mapped = 0;
    if (mapped >= target) mapped = target - 1;
    return static_cast<int>(mapped);
}

bool HasBit(const std::uint8_t* bits, int index) {
    return (bits[index / 8] & (1u << (index % 8))) != 0;
}

struct Device {
    int fd = -1;
    std::string path;
    std::string name;
    bool multiTouch = false;
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int slot = 0;
    bool active[kMaxSlots] = {};
    int slotX[kMaxSlots] = {};
    int slotY[kMaxSlots] = {};
    bool touching = false;
    bool down = false;
    int lastX = 0;
    int lastY = 0;
};

void Emit(Touch* touches, int capacity, int* count, int action, int x, int y) {
    if (*count >= capacity) return;
    Touch& touch = touches[*count];
    touch.action = action;
    touch.x = static_cast<float>(x);
    touch.y = static_cast<float>(y);
    ++*count;
}

// Called on SYN_REPORT: turn the accumulated slot state into at most one touch
// event. The ImGui input queue takes a single pointer, so the lowest active slot
// is treated as the finger that drives the UI.
void Flush(Device& device, Touch* touches, int capacity, int* count) {
    int currentX = device.lastX;
    int currentY = device.lastY;
    bool pressed = false;
    if (device.multiTouch) {
        int primary = -1;
        for (int index = 0; index < kMaxSlots; ++index) {
            if (device.active[index]) {
                primary = index;
                break;
            }
        }
        if (primary >= 0) {
            pressed = true;
            currentX = device.slotX[primary];
            currentY = device.slotY[primary];
        }
    } else {
        pressed = device.touching;
        currentX = device.slotX[0];
        currentY = device.slotY[0];
    }
    if (pressed) {
        if (!device.down) {
            Emit(touches, capacity, count, 0, currentX, currentY);
            device.down = true;
        } else if (currentX != device.lastX || currentY != device.lastY) {
            Emit(touches, capacity, count, 1, currentX, currentY);
        }
        device.lastX = currentX;
        device.lastY = currentY;
    } else if (device.down) {
        Emit(touches, capacity, count, 2, device.lastX, device.lastY);
        device.down = false;
    }
}

}  // namespace

struct Reader::Impl {
    std::vector<Device> devices;
    int width = 0;
    int height = 0;
    std::string summary = "未打开";
};

Reader::Reader() : impl_(new Impl()) {}

Reader::~Reader() { Close(); }

bool Reader::Open(int surfaceWidth, int surfaceHeight, std::string* error) {
    Close();
    impl_.reset(new Impl());
    impl_->width = surfaceWidth;
    impl_->height = surfaceHeight;

    Device multi;
    Device single;
    bool haveMulti = false;
    bool haveSingle = false;

    // An event node is only usable when the kernel exposes absolute position
    // axes with a non-empty range; that filters out keyboards, sensors and the
    // many virtual nodes registered by vendor drivers.
    for (int index = 0; index < 40 && !haveMulti; ++index) {
        char path[64] = {0};
        std::snprintf(path, sizeof(path), "/dev/input/event%d", index);
        const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        std::uint8_t absBits[16] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
            close(fd);
            continue;
        }
        char name[80] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        Device device;
        device.fd = fd;
        device.path = path;
        device.name = name;
        struct input_absinfo info = {};
        if (HasBit(absBits, ABS_MT_POSITION_X) && HasBit(absBits, ABS_MT_POSITION_Y)) {
            device.multiTouch = true;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &info) == 0) {
                device.minX = info.minimum;
                device.maxX = info.maximum;
            }
            info = {};
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &info) == 0) {
                device.minY = info.minimum;
                device.maxY = info.maximum;
            }
        } else if (HasBit(absBits, ABS_X) && HasBit(absBits, ABS_Y)) {
            device.multiTouch = false;
            if (ioctl(fd, EVIOCGABS(ABS_X), &info) == 0) {
                device.minX = info.minimum;
                device.maxX = info.maximum;
            }
            info = {};
            if (ioctl(fd, EVIOCGABS(ABS_Y), &info) == 0) {
                device.minY = info.minimum;
                device.maxY = info.maximum;
            }
        }
        if (device.maxX > device.minX && device.maxY > device.minY) {
            if (device.multiTouch && !haveMulti) {
                if (haveSingle) {
                    close(single.fd);
                    haveSingle = false;
                }
                multi = device;
                haveMulti = true;
                if (haveMulti) break;
            }
            if (!device.multiTouch && !haveSingle) {
                single = device;
                haveSingle = true;
                continue;
            }
        }
        close(fd);
    }

    if (haveMulti) {
        impl_->devices.push_back(multi);
    } else if (haveSingle) {
        impl_->devices.push_back(single);
    } else {
        impl_->summary = "未找到触摸设备";
        if (error != nullptr) *error = "没有找到触摸设备（需要 root 读取 /dev/input/event*）";
        return false;
    }

    const Device& device = impl_->devices.front();
    char summary[320] = {0};
    std::snprintf(summary, sizeof(summary), "%s \"%s\" %s，轴范围 %d..%d x %d..%d -> %dx%d 像素",
                  device.path.c_str(), device.name.c_str(),
                  device.multiTouch ? "多点触控" : "单点触控", device.minX, device.maxX,
                  device.minY, device.maxY, surfaceWidth, surfaceHeight);
    impl_->summary = summary;
    sf::trace("input: %s", impl_->summary.c_str());
    return true;
}

int Reader::Poll(Touch* touches, int capacity) {
    if (!impl_ || touches == nullptr || capacity <= 0) return 0;
    int count = 0;
    for (Device& device : impl_->devices) {
        for (int guard = 0; guard < kMaxReadsPerPoll && count < capacity; ++guard) {
            struct input_event event = {};
            const ssize_t got = read(device.fd, &event, sizeof(event));
            if (got != static_cast<ssize_t>(sizeof(event))) {
                if (got < 0 && errno == EINTR) continue;
                break;
            }
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                Flush(device, touches, capacity, &count);
                continue;
            }
            if (event.type == EV_ABS) {
                if (device.multiTouch) {
                    if (event.code == ABS_MT_SLOT) {
                        if (event.value >= 0 && event.value < kMaxSlots) {
                            device.slot = static_cast<int>(event.value);
                        }
                    } else if (event.code == ABS_MT_TRACKING_ID) {
                        device.active[device.slot] = event.value >= 0;
                        if (event.value >= 0) {
                            device.slotX[device.slot] = device.lastX;
                            device.slotY[device.slot] = device.lastY;
                        }
                    } else if (event.code == ABS_MT_POSITION_X) {
                        device.slotX[device.slot] =
                            MapAxis(event.value, device.minX, device.maxX, impl_->width);
                    } else if (event.code == ABS_MT_POSITION_Y) {
                        device.slotY[device.slot] =
                            MapAxis(event.value, device.minY, device.maxY, impl_->height);
                    }
                } else if (event.code == ABS_X) {
                    device.slotX[0] = MapAxis(event.value, device.minX, device.maxX, impl_->width);
                } else if (event.code == ABS_Y) {
                    device.slotY[0] = MapAxis(event.value, device.minY, device.maxY, impl_->height);
                }
            } else if (event.type == EV_KEY && event.code == BTN_TOUCH && !device.multiTouch) {
                device.touching = event.value != 0;
            }
        }
    }
    return count;
}

void Reader::Close() {
    if (!impl_) return;
    for (Device& device : impl_->devices) {
        if (device.fd >= 0) {
            close(device.fd);
            device.fd = -1;
        }
    }
    impl_->devices.clear();
}

std::string Reader::summary() const { return impl_ ? impl_->summary : "未打开"; }

bool Reader::ready() const { return impl_ && !impl_->devices.empty(); }

}  // namespace evdev
}  // namespace sf
