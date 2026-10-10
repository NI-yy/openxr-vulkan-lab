#pragma once

#include <android/log.h>
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

constexpr char kTag[] = "OpenXRVulkanLab";
constexpr XrViewConfigurationType kView = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
constexpr uint32_t kEyeCount = 2;
constexpr uint32_t kWarmupFrames = 300;
constexpr uint32_t kMeasuredFrames = 1800;

enum class Foveation { Off, Low, High };
inline const char* FoveationName(Foveation level) {
    switch (level) {
        case Foveation::Low: return "low";
        case Foveation::High: return "high";
        default: return "off";
    }
}

inline bool XrOk(const char* name, XrResult result) {
    if (XR_SUCCEEDED(result)) return true;
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s failed: XrResult=%d", name, result);
    return false;
}

inline bool VkOk(const char* name, VkResult result) {
    if (result == VK_SUCCESS) return true;
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s failed: VkResult=%d", name, result);
    return false;
}

template <typename Function>
inline bool Load(XrInstance instance, const char* name, Function* function) {
    const auto result = xrGetInstanceProcAddr(
        instance, name, reinterpret_cast<PFN_xrVoidFunction*>(function));
    if (!XrOk(name, result) || *function == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "%s unavailable", name);
        return false;
    }
    return true;
}
