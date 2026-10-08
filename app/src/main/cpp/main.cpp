#include <android/log.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cube_shaders.h"

namespace {
constexpr char kTag[] = "OpenXRVulkanLab";
constexpr XrViewConfigurationType kView = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
constexpr uint32_t kEyeCount = 2;
constexpr float kNear = 0.05f;
constexpr float kFar = 100.0f;
constexpr uint32_t kBenchmarkWidth = 1024;
constexpr uint32_t kBenchmarkHeight = 1024;
constexpr uint32_t kGridSide = 5;
constexpr uint32_t kGridDepth = 4;
constexpr uint32_t kCubeCount = kGridSide * kGridSide * kGridDepth;
constexpr uint32_t kWarmupFrames = 300;
constexpr uint32_t kMeasuredFrames = 1800;
using Clock = std::chrono::steady_clock;

struct Mat4 { float v[16]{}; };
struct Vertex { float position[3]; float color[3]; };
struct PushConstants { Mat4 mvp; };

// A 40 cm cube centered at the model origin. The benchmark draws a fixed grid
// of these cubes in LOCAL space. Each face has its own color.
constexpr std::array<Vertex, 24> kCubeVertices{{
    // Front (+Z), red
    {{-0.2f, -0.2f,  0.2f}, {1.0f, 0.25f, 0.2f}},
    {{ 0.2f, -0.2f,  0.2f}, {1.0f, 0.25f, 0.2f}},
    {{ 0.2f,  0.2f,  0.2f}, {1.0f, 0.25f, 0.2f}},
    {{-0.2f,  0.2f,  0.2f}, {1.0f, 0.25f, 0.2f}},

    // Back (-Z), green
    {{ 0.2f, -0.2f, -0.2f}, {0.2f, 0.9f, 0.35f}},
    {{-0.2f, -0.2f, -0.2f}, {0.2f, 0.9f, 0.35f}},
    {{-0.2f,  0.2f, -0.2f}, {0.2f, 0.9f, 0.35f}},
    {{ 0.2f,  0.2f, -0.2f}, {0.2f, 0.9f, 0.35f}},

    // Left (-X), cyan
    {{-0.2f, -0.2f, -0.2f}, {0.2f, 0.7f, 1.0f}},
    {{-0.2f, -0.2f,  0.2f}, {0.2f, 0.7f, 1.0f}},
    {{-0.2f,  0.2f,  0.2f}, {0.2f, 0.7f, 1.0f}},
    {{-0.2f,  0.2f, -0.2f}, {0.2f, 0.7f, 1.0f}},

    // Right (+X), yellow
    {{ 0.2f, -0.2f,  0.2f}, {1.0f, 0.85f, 0.2f}},
    {{ 0.2f, -0.2f, -0.2f}, {1.0f, 0.85f, 0.2f}},
    {{ 0.2f,  0.2f, -0.2f}, {1.0f, 0.85f, 0.2f}},
    {{ 0.2f,  0.2f,  0.2f}, {1.0f, 0.85f, 0.2f}},

    // Top (+Y), purple
    {{-0.2f,  0.2f,  0.2f}, {0.9f, 0.3f, 1.0f}},
    {{ 0.2f,  0.2f,  0.2f}, {0.9f, 0.3f, 1.0f}},
    {{ 0.2f,  0.2f, -0.2f}, {0.9f, 0.3f, 1.0f}},
    {{-0.2f,  0.2f, -0.2f}, {0.9f, 0.3f, 1.0f}},

    // Bottom (-Y), orange
    {{-0.2f, -0.2f, -0.2f}, {1.0f, 0.55f, 0.15f}},
    {{ 0.2f, -0.2f, -0.2f}, {1.0f, 0.55f, 0.15f}},
    {{ 0.2f, -0.2f,  0.2f}, {1.0f, 0.55f, 0.15f}},
    {{-0.2f, -0.2f,  0.2f}, {1.0f, 0.55f, 0.15f}},
}};
constexpr std::array<uint16_t, 36> kCubeIndices{{
    0, 1, 2, 2, 3, 0,        // Front
    4, 5, 6, 6, 7, 4,        // Back
    8, 9, 10, 10, 11, 8,     // Left
    12, 13, 14, 14, 15, 12,  // Right
    16, 17, 18, 18, 19, 16,  // Top
    20, 21, 22, 22, 23, 20,  // Bottom
}};

Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result.v[col * 4 + row] += a.v[k * 4 + row] * b.v[col * 4 + k];
    return result;
}

Mat4 EyeView(const XrPosef& pose) {
    const auto& q = pose.orientation;
    const float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    const float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    const float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    // Inverse of the eye's rigid transform. Columns contain world axes in eye space.
    Mat4 view{};
    view.v[0] = 1-2*(yy+zz); view.v[1] = 2*(xy-wz); view.v[2] = 2*(xz+wy);
    view.v[4] = 2*(xy+wz); view.v[5] = 1-2*(xx+zz); view.v[6] = 2*(yz-wx);
    view.v[8] = 2*(xz-wy); view.v[9] = 2*(yz+wx); view.v[10] = 1-2*(xx+yy);
    view.v[12] = -(view.v[0]*pose.position.x + view.v[4]*pose.position.y + view.v[8]*pose.position.z);
    view.v[13] = -(view.v[1]*pose.position.x + view.v[5]*pose.position.y + view.v[9]*pose.position.z);
    view.v[14] = -(view.v[2]*pose.position.x + view.v[6]*pose.position.y + view.v[10]*pose.position.z);
    view.v[15] = 1;
    return view;
}

Mat4 Projection(const XrFovf& fov) {
    const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
    const float d = std::tan(fov.angleDown), u = std::tan(fov.angleUp);
    Mat4 p{};
    p.v[0] = 2/(r-l);
    // Vulkan's positive-height viewport points Y down; flip clip-space Y here.
    p.v[5] = -2/(u-d);
    p.v[8] = (r+l)/(r-l);
    p.v[9] = -(u+d)/(u-d);
    p.v[10] = kFar/(kNear-kFar);
    p.v[11] = -1;
    p.v[14] = (kNear*kFar)/(kNear-kFar);
    return p;
}

Mat4 CubeModel(uint32_t cubeIndex) {
    Mat4 m{};
    m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1;
    const uint32_t column = cubeIndex % kGridSide;
    const uint32_t row = (cubeIndex / kGridSide) % kGridSide;
    const uint32_t depth = cubeIndex / (kGridSide * kGridSide);
    m.v[12] = (static_cast<float>(column) - 2.0f) * 0.55f;
    m.v[13] = (static_cast<float>(row) - 2.0f) * 0.55f;
    m.v[14] = -2.0f - static_cast<float>(depth) * 0.65f;
    return m;
}

struct FrameSample {
    double intervalMs = 0;
    double leftGpuMs = 0;
    double rightGpuMs = 0;
};

struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<XrSwapchainImageVulkan2KHR> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
};

struct State {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    VkInstance vkInstance = VK_NULL_HANDLE;
    VkPhysicalDevice vkPhysicalDevice = VK_NULL_HANDLE;
    VkDevice vkDevice = VK_NULL_HANDLE;
    VkQueue vkQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer geometryBuffer = VK_NULL_HANDLE;
    VkDeviceMemory geometryMemory = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence renderFence = VK_NULL_HANDLE;
    VkQueryPool timestampPool = VK_NULL_HANDLE;
    float timestampPeriodNs = 0;
    uint32_t timestampValidBits = 0;
    std::array<EyeSwapchain, kEyeCount> eyes;
    std::array<XrView, kEyeCount> views{};
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool running = false;
    bool exiting = false;
    uint64_t frames = 0;
    Clock::time_point previousFrameStart{};
    bool havePreviousFrameStart = false;
    std::vector<FrameSample> samples;
    uint32_t renderedFrames = 0;
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
                                 XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
                                 XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME}) {
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
                                XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
                                XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    info.next = &android;
    std::strncpy(info.applicationInfo.applicationName, "OpenXR Vulkan Lab",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    info.enabledExtensionCount = 3;
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
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    if (!XrOk("xrGetVulkanGraphicsDevice2KHR", graphicsDeviceFn(
            state.instance, &getInfo, &physicalDevice))) return false;
    state.vkPhysicalDevice = physicalDevice;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    if (familyCount == 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "No Vulkan queue families");
        return false;
    }
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
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
    if (families[graphicsFamily].timestampValidBits == 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Graphics queue does not support timestamps");
        return false;
    }
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    state.timestampPeriodNs = deviceProperties.limits.timestampPeriod;
    state.timestampValidBits = families[graphicsFamily].timestampValidBits;
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "GPU: %s; timestampValidBits=%u; timestampPeriodNs=%.6f",
                        deviceProperties.deviceName, families[graphicsFamily].timestampValidBits,
                        state.timestampPeriodNs);
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
    xrDeviceInfo.vulkanPhysicalDevice = physicalDevice;
    xrDeviceInfo.vulkanCreateInfo = &vkDeviceInfo;
    vkResult = VK_SUCCESS;
    if (!XrOk("xrCreateVulkanDeviceKHR", deviceFn(
            state.instance, &xrDeviceInfo, &state.vkDevice, &vkResult)) ||
        !VkOk("xrCreateVulkanDeviceKHR/Vulkan", vkResult)) return false;
    state.graphicsFamily = graphicsFamily;
    vkGetDeviceQueue(state.vkDevice, graphicsFamily, 0, &state.vkQueue);

    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = state.vkInstance;
    binding.physicalDevice = physicalDevice;
    binding.device = state.vkDevice;
    binding.queueFamilyIndex = graphicsFamily;
    binding.queueIndex = 0;
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = state.system;
    if (!XrOk("xrCreateSession", xrCreateSession(state.instance, &sessionInfo, &state.session))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR session created (graphics queue=%u)", graphicsFamily);
    PFN_xrGetDisplayRefreshRateFB getRefreshRate = nullptr;
    if (!Load(state.instance, "xrGetDisplayRefreshRateFB", &getRefreshRate)) return false;
    float refreshRate = 0;
    if (!XrOk("xrGetDisplayRefreshRateFB", getRefreshRate(state.session, &refreshRate))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR display refresh rate: %.2f Hz", refreshRate);

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

bool AllocateMemory(State& state, VkMemoryRequirements requirements,
                    VkMemoryPropertyFlags properties, VkDeviceMemory& memory) {
    VkPhysicalDeviceMemoryProperties types{};
    vkGetPhysicalDeviceMemoryProperties(state.vkPhysicalDevice, &types);
    for (uint32_t i = 0; i < types.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (types.memoryTypes[i].propertyFlags & properties) == properties) {
            VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            info.allocationSize = requirements.size;
            info.memoryTypeIndex = i;
            return VkOk("vkAllocateMemory", vkAllocateMemory(state.vkDevice, &info,
                                                              nullptr, &memory));
        }
    }
    __android_log_print(ANDROID_LOG_ERROR, kTag, "No matching Vulkan memory type");
    return false;
}

bool InitGeometry(State& state) {
    const VkDeviceSize vertexBytes = sizeof(kCubeVertices);
    const VkDeviceSize indexBytes = sizeof(kCubeIndices);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = vertexBytes + indexBytes;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!VkOk("vkCreateBuffer(geometry)", vkCreateBuffer(state.vkDevice, &info,
                   nullptr, &state.geometryBuffer))) return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(state.vkDevice, state.geometryBuffer, &requirements);
    if (!AllocateMemory(state, requirements,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            state.geometryMemory)) return false;
    if (!VkOk("vkBindBufferMemory(geometry)", vkBindBufferMemory(state.vkDevice,
            state.geometryBuffer, state.geometryMemory, 0))) return false;
    void* data = nullptr;
    if (!VkOk("vkMapMemory(geometry)", vkMapMemory(state.vkDevice,
            state.geometryMemory, 0, info.size, 0, &data))) return false;
    std::memcpy(data, kCubeVertices.data(), vertexBytes);
    std::memcpy(static_cast<char*>(data) + vertexBytes, kCubeIndices.data(), indexBytes);
    vkUnmapMemory(state.vkDevice, state.geometryMemory);
    return true;
}

bool InitPipeline(State& state) {
    VkShaderModule modules[2]{};
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = sizeof(kCubeVertexShader);
    shader.pCode = kCubeVertexShader;
    if (!VkOk("vkCreateShaderModule(vertex)", vkCreateShaderModule(state.vkDevice,
            &shader, nullptr, &modules[0]))) return false;
    shader.codeSize = sizeof(kCubeFragmentShader);
    shader.pCode = kCubeFragmentShader;
    if (!VkOk("vkCreateShaderModule(fragment)", vkCreateShaderModule(state.vkDevice,
            &shader, nullptr, &modules[1]))) {
        vkDestroyShaderModule(state.vkDevice, modules[0], nullptr);
        return false;
    }
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    bool ok = VkOk("vkCreatePipelineLayout", vkCreatePipelineLayout(state.vkDevice,
            &layout, nullptr, &state.pipelineLayout));
    if (ok) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[i].module = modules[i];
            stages[i].pName = "main";
        }
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[2] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}};
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = 2;
        vertex.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState color{};
        color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &color;
        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &vertex;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = state.pipelineLayout;
        pipeline.renderPass = state.renderPass;
        ok = VkOk("vkCreateGraphicsPipelines", vkCreateGraphicsPipelines(state.vkDevice,
                VK_NULL_HANDLE, 1, &pipeline, nullptr, &state.pipeline));
    }
    vkDestroyShaderModule(state.vkDevice, modules[1], nullptr);
    vkDestroyShaderModule(state.vkDevice, modules[0], nullptr);
    return ok;
}

bool InitDepth(State& state, EyeSwapchain& eye) {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = state.depthFormat;
    image.extent = {eye.width, eye.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!VkOk("vkCreateImage(depth)", vkCreateImage(state.vkDevice, &image,
            nullptr, &eye.depthImage))) return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(state.vkDevice, eye.depthImage, &requirements);
    if (!AllocateMemory(state, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            eye.depthMemory)) return false;
    if (!VkOk("vkBindImageMemory(depth)", vkBindImageMemory(state.vkDevice,
            eye.depthImage, eye.depthMemory, 0))) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = eye.depthImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = state.depthFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    return VkOk("vkCreateImageView(depth)", vkCreateImageView(state.vkDevice,
            &view, nullptr, &eye.depthView));
}

bool InitSwapchains(State& state) {
    uint32_t viewCount = 0;
    if (!XrOk("xrEnumerateViewConfigurationViews(count)", xrEnumerateViewConfigurationViews(
            state.instance, state.system, kView, 0, &viewCount, nullptr))) return false;
    if (viewCount != kEyeCount) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Expected two stereo views; got %u", viewCount);
        return false;
    }
    std::array<XrViewConfigurationView, kEyeCount> configViews{};
    for (auto& view : configViews) view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    if (!XrOk("xrEnumerateViewConfigurationViews(list)", xrEnumerateViewConfigurationViews(
            state.instance, state.system, kView, viewCount, &viewCount, configViews.data()))) return false;

    uint32_t formatCount = 0;
    if (!XrOk("xrEnumerateSwapchainFormats(count)", xrEnumerateSwapchainFormats(
            state.session, 0, &formatCount, nullptr))) return false;
    std::vector<int64_t> formats(formatCount);
    if (!XrOk("xrEnumerateSwapchainFormats(list)", xrEnumerateSwapchainFormats(
            state.session, formatCount, &formatCount, formats.data()))) return false;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    for (VkFormat preferred : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                               VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(state.vkPhysicalDevice, preferred, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) continue;
        for (int64_t offered : formats) {
            if (offered == preferred) {
                colorFormat = preferred;
                break;
            }
        }
        if (colorFormat != VK_FORMAT_UNDEFINED) break;
    }
    if (colorFormat == VK_FORMAT_UNDEFINED) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "No supported RGBA color attachment format");
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "Swapchain format: %d", colorFormat);

    for (VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(state.vkPhysicalDevice, candidate, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            state.depthFormat = candidate;
            break;
        }
    }
    if (state.depthFormat == VK_FORMAT_UNDEFINED) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "No supported depth attachment format");
        return false;
    }

    VkAttachmentDescription attachment{};
    attachment.format = colorFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // OpenXR supplies acquired color images in a layout compatible with this layout.
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = state.depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentDescription attachments[] = {attachment, depthAttachment};
    VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (!VkOk("vkCreateRenderPass", vkCreateRenderPass(
            state.vkDevice, &renderPassInfo, nullptr, &state.renderPass))) return false;
    if (!InitGeometry(state) || !InitPipeline(state)) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "Cube geometry and graphics pipeline ready");

    for (uint32_t eyeIndex = 0; eyeIndex < kEyeCount; ++eyeIndex) {
        auto& eye = state.eyes[eyeIndex];
        if (configViews[eyeIndex].maxImageRectWidth < kBenchmarkWidth ||
            configViews[eyeIndex].maxImageRectHeight < kBenchmarkHeight) {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                                "Eye %u does not support %ux%u benchmark resolution",
                                eyeIndex, kBenchmarkWidth, kBenchmarkHeight);
            return false;
        }
        eye.width = kBenchmarkWidth;
        eye.height = kBenchmarkHeight;
        if (!InitDepth(state, eye)) return false;
        XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.format = colorFormat;
        createInfo.sampleCount = 1;
        createInfo.width = eye.width;
        createInfo.height = eye.height;
        createInfo.faceCount = 1;
        createInfo.arraySize = 1;
        createInfo.mipCount = 1;
        if (!XrOk("xrCreateSwapchain", xrCreateSwapchain(
                state.session, &createInfo, &eye.handle))) return false;
        uint32_t imageCount = 0;
        if (!XrOk("xrEnumerateSwapchainImages(count)", xrEnumerateSwapchainImages(
                eye.handle, 0, &imageCount, nullptr))) return false;
        if (imageCount == 0) return false;
        eye.images.resize(imageCount);
        eye.views.resize(imageCount, VK_NULL_HANDLE);
        eye.framebuffers.resize(imageCount, VK_NULL_HANDLE);
        for (auto& image : eye.images) image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
        if (!XrOk("xrEnumerateSwapchainImages(list)", xrEnumerateSwapchainImages(
                eye.handle, imageCount, &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())))) return false;
        for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = eye.images[imageIndex].image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = colorFormat;
            viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (!VkOk("vkCreateImageView", vkCreateImageView(
                    state.vkDevice, &viewInfo, nullptr, &eye.views[imageIndex]))) return false;
            VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebufferInfo.renderPass = state.renderPass;
            const VkImageView framebufferAttachments[] = {eye.views[imageIndex], eye.depthView};
            framebufferInfo.attachmentCount = 2;
            framebufferInfo.pAttachments = framebufferAttachments;
            framebufferInfo.width = eye.width;
            framebufferInfo.height = eye.height;
            framebufferInfo.layers = 1;
            if (!VkOk("vkCreateFramebuffer", vkCreateFramebuffer(
                    state.vkDevice, &framebufferInfo, nullptr,
                    &eye.framebuffers[imageIndex]))) return false;
        }
        __android_log_print(ANDROID_LOG_INFO, kTag, "Eye %u swapchain: %ux%u, %u images",
                            eyeIndex, eye.width, eye.height, imageCount);
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = state.graphicsFamily;
    if (!VkOk("vkCreateCommandPool", vkCreateCommandPool(
            state.vkDevice, &poolInfo, nullptr, &state.commandPool))) return false;
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = state.commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    if (!VkOk("vkAllocateCommandBuffers", vkAllocateCommandBuffers(
            state.vkDevice, &allocateInfo, &state.commandBuffer))) return false;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!VkOk("vkCreateFence", vkCreateFence(
            state.vkDevice, &fenceInfo, nullptr, &state.renderFence))) return false;
    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 2;
    if (!VkOk("vkCreateQueryPool(timestamp)", vkCreateQueryPool(
            state.vkDevice, &queryInfo, nullptr, &state.timestampPool))) return false;
    state.samples.reserve(kMeasuredFrames);
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "Benchmark config: dual-pass cubes=%u resolution=%ux%u per eye samples=1 warmup=%u measure=%u colorFormat=%d depthFormat=%d",
                        kCubeCount, kBenchmarkWidth, kBenchmarkHeight, kWarmupFrames,
                        kMeasuredFrames, colorFormat, state.depthFormat);
    for (auto& view : state.views) view.type = XR_TYPE_VIEW;
    return true;
}

bool RenderEye(State& state, EyeSwapchain& eye, uint32_t imageIndex,
               const XrView& xrView, double& gpuMs) {
    if (imageIndex >= eye.framebuffers.size()) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Invalid swapchain image index %u", imageIndex);
        return false;
    }
    if (!VkOk("vkResetCommandBuffer", vkResetCommandBuffer(state.commandBuffer, 0))) return false;
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!VkOk("vkBeginCommandBuffer", vkBeginCommandBuffer(state.commandBuffer, &beginInfo))) return false;
    vkCmdResetQueryPool(state.commandBuffer, state.timestampPool, 0, 2);
    vkCmdWriteTimestamp(state.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        state.timestampPool, 0);
    VkClearValue clear[2]{};
    clear[0].color.float32[0] = 0.02f;
    clear[0].color.float32[1] = 0.12f;
    clear[0].color.float32[2] = 0.55f;
    clear[0].color.float32[3] = 1.0f;
    clear[1].depthStencil.depth = 1.0f;
    VkRenderPassBeginInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    passInfo.renderPass = state.renderPass;
    passInfo.framebuffer = eye.framebuffers[imageIndex];
    passInfo.renderArea.extent = {eye.width, eye.height};
    passInfo.clearValueCount = 2;
    passInfo.pClearValues = clear;
    vkCmdBeginRenderPass(state.commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0, 0, static_cast<float>(eye.width),
                              static_cast<float>(eye.height), 0, 1};
    const VkRect2D scissor{{0, 0}, {eye.width, eye.height}};
    vkCmdSetViewport(state.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(state.commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(state.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(state.commandBuffer, 0, 1, &state.geometryBuffer, &offset);
    vkCmdBindIndexBuffer(state.commandBuffer, state.geometryBuffer,
                         sizeof(kCubeVertices), VK_INDEX_TYPE_UINT16);
    const Mat4 viewProjection = Multiply(Projection(xrView.fov), EyeView(xrView.pose));
    for (uint32_t cube = 0; cube < kCubeCount; ++cube) {
        PushConstants transform{Multiply(viewProjection, CubeModel(cube))};
        vkCmdPushConstants(state.commandBuffer, state.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(transform), &transform);
        vkCmdDrawIndexed(state.commandBuffer, static_cast<uint32_t>(kCubeIndices.size()),
                         1, 0, 0, 0);
    }
    vkCmdEndRenderPass(state.commandBuffer);
    vkCmdWriteTimestamp(state.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        state.timestampPool, 1);
    if (!VkOk("vkEndCommandBuffer", vkEndCommandBuffer(state.commandBuffer))) return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &state.commandBuffer;
    if (!VkOk("vkQueueSubmit", vkQueueSubmit(state.vkQueue, 1, &submit, state.renderFence))) return false;
    // OpenXR may use this queue in acquire/release/endFrame. All calls stay on this thread,
    // and the GPU work is finished before the image is released to the runtime.
    if (!VkOk("vkWaitForFences", vkWaitForFences(
            state.vkDevice, 1, &state.renderFence, VK_TRUE, UINT64_MAX))) return false;
    uint64_t timestamps[2]{};
    if (!VkOk("vkGetQueryPoolResults(timestamp)", vkGetQueryPoolResults(
            state.vkDevice, state.timestampPool, 0, 2, sizeof(timestamps), timestamps,
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT))) return false;
    uint64_t elapsedTicks = timestamps[1] - timestamps[0];
    if (state.timestampValidBits < 64) {
        elapsedTicks &= (uint64_t{1} << state.timestampValidBits) - 1;
    }
    gpuMs = static_cast<double>(elapsedTicks) *
            state.timestampPeriodNs / 1000000.0;
    if (!VkOk("vkResetFences", vkResetFences(state.vkDevice, 1, &state.renderFence))) return false;
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
                state.renderedFrames = 0;
                state.samples.clear();
                state.havePreviousFrameStart = false;
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
        } else if (buffer.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const auto& event = *reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&buffer);
            __android_log_print(ANDROID_LOG_INFO, kTag,
                "Reference space change pending: type=%d, poseValid=%u",
                event.referenceSpaceType, event.poseValid);
        }
    }
}

void LogPercentiles(const char* metric, std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    const auto percentile = [&](double fraction) {
        const double position = fraction * static_cast<double>(n - 1);
        const size_t low = static_cast<size_t>(position);
        const size_t high = std::min(low + 1, n - 1);
        return values[low] + (values[high] - values[low]) * (position - low);
    };
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "Benchmark %s ms: p10=%.4f median=%.4f p90=%.4f",
                        metric, percentile(0.1), percentile(0.5), percentile(0.9));
}

void LogBenchmark(const State& state) {
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "Benchmark complete: rendered=%u warmup=%u measured=%zu",
                        state.renderedFrames, kWarmupFrames, state.samples.size());
    std::vector<double> values;
    values.reserve(state.samples.size());
    for (const auto& sample : state.samples) values.push_back(sample.intervalMs);
    LogPercentiles("frame_interval", values);
    values.clear();
    for (const auto& sample : state.samples) values.push_back(sample.leftGpuMs);
    LogPercentiles("left_gpu", values);
    values.clear();
    for (const auto& sample : state.samples) values.push_back(sample.rightGpuMs);
    LogPercentiles("right_gpu", values);
    values.clear();
    for (const auto& sample : state.samples) {
        values.push_back(sample.leftGpuMs + sample.rightGpuMs);
    }
    LogPercentiles("total_gpu", values);
}

bool RunFrame(State& state) {
    const auto frameStart = Clock::now();
    const double intervalMs = state.havePreviousFrameStart
        ? std::chrono::duration<double, std::milli>(frameStart - state.previousFrameStart).count()
        : 0.0;
    state.previousFrameStart = frameStart;
    state.havePreviousFrameStart = true;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    if (!XrOk("xrWaitFrame", xrWaitFrame(state.session, &wait, &frame))) return false;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (!XrOk("xrBeginFrame", xrBeginFrame(state.session, &begin))) return false;
    std::array<XrCompositionLayerProjectionView, kEyeCount> layerViews;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    bool rendered = false;
    FrameSample sample{};
    sample.intervalMs = intervalMs;
    if (frame.shouldRender) {
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = kView;
        locate.displayTime = frame.predictedDisplayTime;
        locate.space = state.space;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t located = 0;
        if (!XrOk("xrLocateViews", xrLocateViews(state.session, &locate, &viewState,
                kEyeCount, &located, state.views.data()))) return false;
        if (located != kEyeCount) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "Expected two located views; got %u", located);
            return false;
        }
        const XrViewStateFlags valid = XR_VIEW_STATE_POSITION_VALID_BIT |
                                       XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        if ((viewState.viewStateFlags & valid) == valid) {
            for (uint32_t eyeIndex = 0; eyeIndex < kEyeCount; ++eyeIndex) {
                auto& eye = state.eyes[eyeIndex];
                uint32_t imageIndex = 0;
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if (!XrOk("xrAcquireSwapchainImage", xrAcquireSwapchainImage(
                        eye.handle, &acquire, &imageIndex))) return false;
                XrSwapchainImageWaitInfo waitImage{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                waitImage.timeout = XR_INFINITE_DURATION;
                if (!XrOk("xrWaitSwapchainImage", xrWaitSwapchainImage(
                        eye.handle, &waitImage))) return false;
                double gpuMs = 0;
                if (!RenderEye(state, eye, imageIndex, state.views[eyeIndex], gpuMs)) return false;
                if (eyeIndex == 0) sample.leftGpuMs = gpuMs;
                else sample.rightGpuMs = gpuMs;
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if (!XrOk("xrReleaseSwapchainImage", xrReleaseSwapchainImage(
                        eye.handle, &release))) return false;
                auto& projection = layerViews[eyeIndex];
                projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                projection.pose = state.views[eyeIndex].pose;
                projection.fov = state.views[eyeIndex].fov;
                projection.subImage.swapchain = eye.handle;
                projection.subImage.imageRect.extent = {
                    static_cast<int32_t>(eye.width), static_cast<int32_t>(eye.height)};
            }
            layer.space = state.space;
            layer.viewCount = kEyeCount;
            layer.views = layerViews.data();
            rendered = true;
        }
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = frame.predictedDisplayTime;
    end.environmentBlendMode = state.blend;
    if (layer.views != nullptr) {
        end.layerCount = 1;
        end.layers = layers;
    }
    if (!XrOk("xrEndFrame", xrEndFrame(state.session, &end))) return false;
    if (rendered) {
        ++state.renderedFrames;
        if (state.renderedFrames > kWarmupFrames &&
            state.samples.size() < kMeasuredFrames && intervalMs > 0) {
            state.samples.push_back(sample);
            if (state.samples.size() == kMeasuredFrames) LogBenchmark(state);
        }
    } else {
        state.havePreviousFrameStart = false;
        if (state.renderedFrames != 0 && state.samples.size() < kMeasuredFrames) {
            state.renderedFrames = 0;
            state.samples.clear();
            __android_log_print(ANDROID_LOG_INFO, kTag,
                                "Benchmark reset after a frame without rendering");
        }
    }
    ++state.frames;
    if (state.frames == 1 || state.frames % 120 == 0) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR frame %llu completed; shouldRender=%u",
                            static_cast<unsigned long long>(state.frames), frame.shouldRender);
    }
    return true;
}

void Shutdown(State& state) {
    if (state.vkDevice != VK_NULL_HANDLE) VkOk("vkDeviceWaitIdle", vkDeviceWaitIdle(state.vkDevice));
    if (state.timestampPool != VK_NULL_HANDLE) vkDestroyQueryPool(state.vkDevice, state.timestampPool, nullptr);
    if (state.renderFence != VK_NULL_HANDLE) vkDestroyFence(state.vkDevice, state.renderFence, nullptr);
    if (state.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(state.vkDevice, state.commandPool, nullptr);
    if (state.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(state.vkDevice, state.pipeline, nullptr);
    if (state.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(state.vkDevice, state.pipelineLayout, nullptr);
    if (state.geometryBuffer != VK_NULL_HANDLE) vkDestroyBuffer(state.vkDevice, state.geometryBuffer, nullptr);
    if (state.geometryMemory != VK_NULL_HANDLE) vkFreeMemory(state.vkDevice, state.geometryMemory, nullptr);
    for (auto& eye : state.eyes) {
        for (auto framebuffer : eye.framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(state.vkDevice, framebuffer, nullptr);
        }
        for (auto view : eye.views) {
            if (view != VK_NULL_HANDLE) vkDestroyImageView(state.vkDevice, view, nullptr);
        }
        if (eye.depthView != VK_NULL_HANDLE) vkDestroyImageView(state.vkDevice, eye.depthView, nullptr);
        if (eye.depthImage != VK_NULL_HANDLE) vkDestroyImage(state.vkDevice, eye.depthImage, nullptr);
        if (eye.depthMemory != VK_NULL_HANDLE) vkFreeMemory(state.vkDevice, eye.depthMemory, nullptr);
        if (eye.handle != XR_NULL_HANDLE) XrOk("xrDestroySwapchain", xrDestroySwapchain(eye.handle));
    }
    if (state.renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(state.vkDevice, state.renderPass, nullptr);
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
    bool operationSucceeded = InitInstance(app, state);
    if (operationSucceeded) operationSucceeded = InitSession(state);
    if (operationSucceeded) operationSucceeded = InitSwapchains(state);
    if (!operationSucceeded) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Initialization failed; finishing activity");
        ANativeActivity_finish(app->activity);
    }
    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        // A stopped session needs periodic OpenXR event checks to resume.
        const int timeout = operationSucceeded && state.running ? 0 : 50;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested) break;
            source = nullptr;
            if (timeout != 0) break;
        }
        if (app->destroyRequested || !operationSucceeded) continue;
        operationSucceeded = PollEvents(state);
        if (operationSucceeded && state.exiting) {
            operationSucceeded = false;
            ANativeActivity_finish(app->activity);
        } else if (operationSucceeded && state.running) {
            operationSucceeded = RunFrame(state);
        }
        if (!operationSucceeded && !state.exiting) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "OpenXR loop failed; finishing activity");
            ANativeActivity_finish(app->activity);
        }
    }
    Shutdown(state);
    __android_log_print(ANDROID_LOG_INFO, kTag, "native app exited");
}
