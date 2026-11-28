#pragma once

#include <memory>
#include <string>

namespace sf {
namespace evdev {

// One touch sample mapped into surface pixels.
struct Touch {
    int action = 0;  // 0 = down, 1 = move, 2 = up
    float x = 0.0f;
    float y = 0.0f;
};

// Reads Linux input events straight from /dev/input/event*. The root host uses
// this instead of InputFlinger: there is no Activity and no window token, so the
// only available source of touches is the kernel device node itself.
class Reader {
public:
    Reader();
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    // Opens the first touchscreen that exposes absolute multitouch axes (or a
    // single-touch device when that is all the kernel offers) and maps its raw
    // axis range onto surfaceWidth x surfaceHeight pixels.
    bool Open(int surfaceWidth, int surfaceHeight, std::string* error);
    // Non-blocking drain of pending events; returns how many Touch entries were
    // written. Whatever does not fit stays in the kernel queue.
    int Poll(Touch* touches, int capacity);
    void Close();

    std::string summary() const;
    bool ready() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace evdev
}  // namespace sf
