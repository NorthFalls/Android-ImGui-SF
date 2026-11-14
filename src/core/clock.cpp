#include "core/clock.h"

#include <time.h>

namespace sf {

double nowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

uint64_t nowMillis() {
    return static_cast<uint64_t>(nowSeconds() * 1000.0);
}

void FrameCounter::tick(double now) {
    if (!primed_) {
        primed_ = true;
        windowStart_ = now;
        last_ = now;
        return;
    }
    ++frames_;
    ++windowFrames_;

    const double dt = now - last_;
    last_ = now;
    if (dt > 0.0) {
        const float instant = static_cast<float>(dt * 1000.0);
        frameMs_ = frameMs_ == 0.0f ? instant : frameMs_ * 0.9f + instant * 0.1f;
    }

    const double span = now - windowStart_;
    if (span >= 1.0) {
        fps_ = static_cast<float>(static_cast<double>(windowFrames_) / span);
        windowFrames_ = 0;
        windowStart_ = now;
    }
}

}  // namespace sf
