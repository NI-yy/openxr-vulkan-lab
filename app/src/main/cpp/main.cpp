#include <android/log.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstring>

namespace {
constexpr char kLogTag[] = "OpenXRVulkanLab";

void OnAppCommand(android_app*, int32_t command) {
    if (command == APP_CMD_START) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "activity started");
    } else if (command == APP_CMD_STOP) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "activity stopped");
    } else if (command == APP_CMD_DESTROY) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "activity destroyed");
    }
}

int32_t OnInputEvent(android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY &&
        AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
        AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "back pressed; finishing activity");
        ANativeActivity_finish(app->activity);
        return 1;
    }
    return 0;
}

bool InitializeOpenXR(android_app* app, XrInstance* instance) {
    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    XrResult result = xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader));
    if (XR_FAILED(result) || initializeLoader == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "xrInitializeLoaderKHR unavailable: %d", result);
        return false;
    }

    XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInfo.applicationVM = app->activity->vm;
    loaderInfo.applicationContext = app->activity->clazz;
    result = initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo));
    if (XR_FAILED(result)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "xrInitializeLoaderKHR failed: %d", result);
        return false;
    }

    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = app->activity->vm;
    androidInfo.applicationActivity = app->activity->clazz;

    const char* extensions[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
    };
    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = &androidInfo;
    std::strncpy(createInfo.applicationInfo.applicationName, "OpenXR Vulkan Lab",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount = 2;
    createInfo.enabledExtensionNames = extensions;
    result = xrCreateInstance(&createInfo, instance);
    if (XR_FAILED(result)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "xrCreateInstance failed: %d", result);
        return false;
    }

    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    result = xrGetInstanceProperties(*instance, &properties);
    if (XR_SUCCEEDED(result)) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "OpenXR runtime: %s", properties.runtimeName);
    }
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    result = xrGetSystem(*instance, &systemInfo, &systemId);
    __android_log_print(XR_SUCCEEDED(result) ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                        kLogTag, "xrGetSystem: %d", result);
    return true;
}
}  // namespace

void android_main(android_app* app) {
    app->onAppCmd = OnAppCommand;
    app->onInputEvent = OnInputEvent;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "native app started");

    uint32_t vulkanVersion = VK_API_VERSION_1_0;
    VkResult vkResult = vkEnumerateInstanceVersion(&vulkanVersion);
    __android_log_print(vkResult == VK_SUCCESS ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
                        kLogTag, "Vulkan loader: result=%d API=%u.%u.%u", vkResult,
                        VK_VERSION_MAJOR(vulkanVersion), VK_VERSION_MINOR(vulkanVersion),
                        VK_VERSION_PATCH(vulkanVersion));

    XrInstance instance = XR_NULL_HANDLE;
    InitializeOpenXR(app, &instance);

    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0 &&
            source != nullptr) {
            source->process(app, source);
        }
    }

    if (instance != XR_NULL_HANDLE) {
        xrDestroyInstance(instance);
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "native app exited");
}
