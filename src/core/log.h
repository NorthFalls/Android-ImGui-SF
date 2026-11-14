#pragma once

#include <android/log.h>

#define SF_TAG "SfImGui"

#define SF_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SF_TAG, __VA_ARGS__)
#define SF_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SF_TAG, __VA_ARGS__)
#define SF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SF_TAG, __VA_ARGS__)
