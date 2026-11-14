#pragma once

#include "imgui.h"

namespace sf {
namespace ui {

class RingBuffer {
public:
    static constexpr int kCapacity = 180;

    void push(float value) {
        if (count_ < kCapacity) {
            data_[count_++] = value;
        } else {
            for (int i = 1; i < kCapacity; ++i) data_[i - 1] = data_[i];
            data_[kCapacity - 1] = value;
        }
    }

    void clear() { count_ = 0; }
    int size() const { return count_; }
    const float* data() const { return data_; }

private:
    float data_[kCapacity] = {};
    int count_ = 0;
};

void FrameGraph(const char* id, const RingBuffer& buffer, float height, float maxValue, ImU32 color);
void MeterRow(const char* label, float fraction, const char* valueText, ImU32 color);
void KeyValue(const char* key, const char* value);
void KeyValuef(const char* key, const char* fmt, ...);
void SectionHeader(const char* text);
void Badge(const char* text, ImU32 textColor, ImU32 bgColor);

}  // namespace ui
}  // namespace sf
