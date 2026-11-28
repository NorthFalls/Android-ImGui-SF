#pragma once

#include <cstdint>

namespace sf {

double nowSeconds();
uint64_t nowMillis();

class FrameCounter {
public:
    void tick(double now);
    float fps() const { return fps_; }
    float frameMs() const { return frameMs_; }
    uint64_t frames() const { return frames_; }

private:
    uint64_t frames_ = 0;
    uint64_t windowFrames_ = 0;
    double windowStart_ = 0.0;
    double last_ = 0.0;
    bool primed_ = false;
    float fps_ = 0.0f;
    float frameMs_ = 0.0f;
};

}  // namespace sf
