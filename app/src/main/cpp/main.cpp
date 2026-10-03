#include <android/log.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstring>
#include <vector>

namespace {
constexpr char kTag[] = "OpenXRVulkanLab";
constexpr XrViewConfigurationType kView = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

struct State {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    VkInstance vkInstance = VK_NULL_HANDLE;
    VkDevice vkDevice = VK_NULL_HANDLE;
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool running = false;
    bool exiting = false;
    uint64_t frames = 0;
};

bool XrOk(const char* name, XrResult result) {
    if (XR_SUCCEEDED(result)) return true;
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s failed: XrResult=%d", name, result);
    return false;
}

bool VkOk(const char* name, VkResult result) {
    if (result == VK_SUCCESS) return true;
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s failed: VkResult=%d", name, result);
    return false;
}

template <typename Function>
bool Load(XrInstance instance, const char* name, Function* function) {
    const auto result = xrGetInstanceProcAddr(
        instance, name, reinterpret_cast<PFN_xrVoidFunction*>(function));
    if (!XrOk(name, result) || *function == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "%s unavailable", name);
        return false;
    }
    return true;
}

void OnCommand(android_app*, int32_t command) {
    if (command == APP_CMD_START || command == APP_CMD_RESUME || command == APP_CMD_PAUSE ||
        command == APP_CMD_STOP || command == APP_CMD_DESTROY) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "Android activity command: %d", command);
    }
}

int32_t OnInput(android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY &&
        AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
        AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "back pressed; finishing activity");
        ANativeActivity_finish(app->activity);
        return 1;
    }
    return 0;
}

bool ExtensionsAvailable() {
    uint32_t count = 0;
    if (!XrOk("xrEnumerateInstanceExtensionProperties(count)",
              xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr))) return false;
    std::vector<XrExtensionProperties> available(count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (!XrOk("xrEnumerateInstanceExtensionProperties(list)",
              xrEnumerateInstanceExtensionProperties(nullptr, count, &count, available.data()))) return false;
    bool complete = true;
    for (const char* required : {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                                 XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME}) {
        bool found = false;
        for (const auto& entry : available) {
            if (std::strcmp(entry.extensionName, required) == 0) {
                found = true;
                break;
            }
        }
        __android_log_print(found ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kTag,
                            "OpenXR extension %s: %s", required, found ? "available" : "missing");
        complete &= found;
    }
    return complete;
}

bool InitInstance(android_app* app, State& state) {
    PFN_xrInitializeLoaderKHR initialize = nullptr;
    if (!Load(XR_NULL_HANDLE, "xrInitializeLoaderKHR", &initialize)) return false;
    XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader.applicationVM = app->activity->vm;
    loader.applicationContext = app->activity->clazz;
    if (!XrOk("xrInitializeLoaderKHR", initialize(
            reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader)))) return false;
    if (!ExtensionsAvailable()) return false;

    XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM = app->activity->vm;
    android.applicationActivity = app->activity->clazz;
    const char* extensions[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                                XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    info.next = &android;
    std::strncpy(info.applicationInfo.applicationName, "OpenXR Vulkan Lab",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    info.enabledExtensionCount = 2;
    info.enabledExtensionNames = extensions;
    if (!XrOk("xrCreateInstance", xrCreateInstance(&info, &state.instance))) return false;

    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    if (!XrOk("xrGetInstanceProperties", xrGetInstanceProperties(state.instance, &properties))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR runtime: %s", properties.runtimeName);
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!XrOk("xrGetSystem", xrGetSystem(state.instance, &systemInfo, &state.system))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "xrGetSystem: 0");
    return true;
}

bool InitSession(State& state) {
    PFN_xrGetVulkanGraphicsRequirements2KHR requirementsFn = nullptr;
    PFN_xrCreateVulkanInstanceKHR instanceFn = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR graphicsDeviceFn = nullptr;
    PFN_xrCreateVulkanDeviceKHR deviceFn = nullptr;
    if (!Load(state.instance, "xrGetVulkanGraphicsRequirements2KHR", &requirementsFn) ||
        !Load(state.instance, "xrCreateVulkanInstanceKHR", &instanceFn) ||
        !Load(state.instance, "xrGetVulkanGraphicsDevice2KHR", &graphicsDeviceFn) ||
        !Load(state.instance, "xrCreateVulkanDeviceKHR", &deviceFn)) return false;

    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (!XrOk("xrGetVulkanGraphicsRequirements2KHR",
              requirementsFn(state.instance, state.system, &requirements))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "Vulkan required API: %u.%u to %u.%u",
                        XR_VERSION_MAJOR(requirements.minApiVersionSupported),
                        XR_VERSION_MINOR(requirements.minApiVersionSupported),
                        XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
                        XR_VERSION_MINOR(requirements.maxApiVersionSupported));
    const XrVersion requested = XR_MAKE_VERSION(1, 0, 0);
    if (requested < requirements.minApiVersionSupported ||
        requested > requirements.maxApiVersionSupported) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Vulkan API 1.0 outside runtime requirements");
        return false;
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "OpenXR Vulkan Lab";
    application.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo vkInstanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vkInstanceInfo.pApplicationInfo = &application;
    XrVulkanInstanceCreateInfoKHR xrInstanceInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrInstanceInfo.systemId = state.system;
    xrInstanceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrInstanceInfo.vulkanCreateInfo = &vkInstanceInfo;
    VkResult vkResult = VK_SUCCESS;
    if (!XrOk("xrCreateVulkanInstanceKHR", instanceFn(
            state.instance, &xrInstanceInfo, &state.vkInstance, &vkResult)) ||
        !VkOk("xrCreateVulkanInstanceKHR/Vulkan", vkResult)) return false;

    XrVulkanGraphicsDeviceGetInfoKHR getInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    getInfo.systemId = state.system;
    getInfo.vulkanInstance = state.vkInstance;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    if (!XrOk("xrGetVulkanGraphicsDevice2KHR", graphicsDeviceFn(
            state.instance, &getInfo, &physical))) return false;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
    if (familyCount == 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "No Vulkan queue families");
        return false;
    }
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
    uint32_t graphicsFamily = familyCount;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
            break;
        }
    }
    if (graphicsFamily == familyCount) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "No Vulkan graphics queue family");
        return false;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = graphicsFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo vkDeviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    vkDeviceInfo.queueCreateInfoCount = 1;
    vkDeviceInfo.pQueueCreateInfos = &queue;
    XrVulkanDeviceCreateInfoKHR xrDeviceInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrDeviceInfo.systemId = state.system;
    xrDeviceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrDeviceInfo.vulkanPhysicalDevice = physical;
    xrDeviceInfo.vulkanCreateInfo = &vkDeviceInfo;
    vkResult = VK_SUCCESS;
    if (!XrOk("xrCreateVulkanDeviceKHR", deviceFn(
            state.instance, &xrDeviceInfo, &state.vkDevice, &vkResult)) ||
        !VkOk("xrCreateVulkanDeviceKHR/Vulkan", vkResult)) return false;

    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = state.vkInstance;
    binding.physicalDevice = physical;
    binding.device = state.vkDevice;
    binding.queueFamilyIndex = graphicsFamily;
    binding.queueIndex = 0;
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = state.system;
    if (!XrOk("xrCreateSession", xrCreateSession(state.instance, &sessionInfo, &state.session))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR session created (graphics queue=%u)", graphicsFamily);

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    if (!XrOk("xrCreateReferenceSpace(LOCAL)", xrCreateReferenceSpace(
            state.session, &spaceInfo, &state.space))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "LOCAL reference space created");

    uint32_t blendCount = 0;
    if (!XrOk("xrEnumerateEnvironmentBlendModes(count)", xrEnumerateEnvironmentBlendModes(
            state.instance, state.system, kView, 0, &blendCount, nullptr))) return false;
    std::vector<XrEnvironmentBlendMode> modes(blendCount);
    if (!XrOk("xrEnumerateEnvironmentBlendModes(list)", xrEnumerateEnvironmentBlendModes(
            state.instance, state.system, kView, blendCount, &blendCount, modes.data()))) return false;
    if (blendCount == 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "No environment blend modes");
        return false;
    }
    state.blend = modes[0];
    for (const auto mode : modes) {
        if (mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) state.blend = mode;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "Environment blend mode: %d", state.blend);
    return true;
}

bool PollEvents(State& state) {
    for (;;) {
        XrEventDataBuffer buffer{XR_TYPE_EVENT_DATA_BUFFER};
        const XrResult result = xrPollEvent(state.instance, &buffer);
        if (result == XR_EVENT_UNAVAILABLE) return true;
        if (!XrOk("xrPollEvent", result)) return false;
        if (buffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& event = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&buffer);
            if (event.session != state.session) {
                __android_log_print(ANDROID_LOG_ERROR, kTag, "State event for unknown session");
                return false;
            }
            __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR session state: %d", event.state);
            if (event.state == XR_SESSION_STATE_READY && !state.running) {
                XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};
                info.primaryViewConfigurationType = kView;
                if (!XrOk("xrBeginSession", xrBeginSession(state.session, &info))) return false;
                state.running = true;
                __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR session started");
            } else if (event.state == XR_SESSION_STATE_STOPPING && state.running) {
                state.running = false;
                if (!XrOk("xrEndSession", xrEndSession(state.session))) return false;
                __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR session stopped");
            } else if (event.state == XR_SESSION_STATE_EXITING ||
                       event.state == XR_SESSION_STATE_LOSS_PENDING) {
                __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR session ending: %d", event.state);
                state.exiting = true;
                return true;
            }
        } else if (buffer.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "OpenXR instance loss pending");
            state.exiting = true;
            return true;
        } else if (buffer.type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
            const auto& event = *reinterpret_cast<const XrEventDataEventsLost*>(&buffer);
            __android_log_print(ANDROID_LOG_WARN, kTag, "OpenXR events lost: %u", event.lostEventCount);
        }
    }
}

bool RunFrame(State& state) {
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    if (!XrOk("xrWaitFrame", xrWaitFrame(state.session, &wait, &frame))) return false;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (!XrOk("xrBeginFrame", xrBeginFrame(state.session, &begin))) return false;
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = frame.predictedDisplayTime;
    end.environmentBlendMode = state.blend;
    // No swapchains or composition layers until the rendering issue.
    if (!XrOk("xrEndFrame", xrEndFrame(state.session, &end))) return false;
    ++state.frames;
    if (state.frames == 1 || state.frames % 120 == 0) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR frame %llu completed; shouldRender=%u",
                            static_cast<unsigned long long>(state.frames), frame.shouldRender);
    }
    return true;
}

void Shutdown(State& state) {
    if (state.space != XR_NULL_HANDLE) XrOk("xrDestroySpace", xrDestroySpace(state.space));
    if (state.session != XR_NULL_HANDLE) XrOk("xrDestroySession", xrDestroySession(state.session));
    if (state.vkDevice != VK_NULL_HANDLE) vkDestroyDevice(state.vkDevice, nullptr);
    if (state.vkInstance != VK_NULL_HANDLE) vkDestroyInstance(state.vkInstance, nullptr);
    if (state.instance != XR_NULL_HANDLE) XrOk("xrDestroyInstance", xrDestroyInstance(state.instance));
}
}  // namespace

void android_main(android_app* app) {
    app->onAppCmd = OnCommand;
    app->onInputEvent = OnInput;
    __android_log_print(ANDROID_LOG_INFO, kTag, "native app started");
    State state;
    bool healthy = InitInstance(app, state);
    if (healthy) healthy = InitSession(state);
    if (!healthy) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Initialization failed; finishing activity");
        ANativeActivity_finish(app->activity);
    }
    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        // A stopped session needs periodic OpenXR event checks to resume.
        const int timeout = healthy && state.running ? 0 : 50;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested) break;
            source = nullptr;
            if (timeout != 0) break;
        }
        if (app->destroyRequested || !healthy) continue;
        healthy = PollEvents(state);
        if (healthy && state.exiting) {
            healthy = false;
            ANativeActivity_finish(app->activity);
        } else if (healthy && state.running) {
            healthy = RunFrame(state);
        }
        if (!healthy && !state.exiting) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "OpenXR loop failed; finishing activity");
            ANativeActivity_finish(app->activity);
        }
    }
    Shutdown(state);
    __android_log_print(ANDROID_LOG_INFO, kTag, "native app exited");
}
