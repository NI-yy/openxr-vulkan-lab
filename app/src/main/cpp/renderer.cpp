#include "renderer.h"
#include "cube_shaders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

constexpr float kNear = 0.05f;
constexpr float kFar = 100.0f;
constexpr uint32_t kBenchmarkWidth = 1024;
constexpr uint32_t kBenchmarkHeight = 1024;

struct Mat4 { float v[16]{}; };
struct Vertex { float position[3]; float color[3]; };
struct PushConstants { Mat4 mvp[kEyeCount]; };

struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<XrSwapchainImageVulkan2KHR> images;
    std::vector<XrSwapchainImageFoveationVulkanFB> densityImages;
    std::vector<VkImageView> views;
    std::vector<VkImageView> densityViews;
    std::vector<VkFramebuffer> framebuffers;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
};

struct RenderState {
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
    bool multiviewRequested = true;
    bool multiviewSupported = false;
    bool useMultiview = false;
    Foveation requestedFoveation = Foveation::Off;
    Foveation foveation = Foveation::Off;
    bool xrFoveationSupported = false;
    bool vkFoveationSupported = false;
    XrFoveationProfileFB foveationProfile = XR_NULL_HANDLE;
    PFN_xrDestroyFoveationProfileFB destroyFoveationProfile = nullptr;
    PFN_xrCreateFoveationProfileFB createFoveationProfile = nullptr;
    PFN_xrUpdateSwapchainFB updateSwapchain = nullptr;
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT densityFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT};
    VkPhysicalDeviceFragmentDensityMap2FeaturesEXT density2Features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT};
    std::array<EyeSwapchain, kEyeCount> eyes;
};

namespace {
// A 40 cm cube centered at the model origin. Each face has its own color.
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

Mat4 CubeModel(const CubePosition& position) {
    Mat4 m{};
    m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1;
    m.v[12] = position.x;
    m.v[13] = position.y;
    m.v[14] = position.z;
    return m;
}

}  // namespace

namespace {
bool InitDevice(RenderState& state, XrInstance instance, XrSystemId system,
                bool xrFoveationSupported, bool multiviewRequested,
                Foveation requestedFoveation) {
    state.xrFoveationSupported = xrFoveationSupported;
    state.multiviewRequested = multiviewRequested;
    state.requestedFoveation = requestedFoveation;
    PFN_xrGetVulkanGraphicsRequirements2KHR requirementsFn = nullptr;
    PFN_xrCreateVulkanInstanceKHR instanceFn = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR graphicsDeviceFn = nullptr;
    PFN_xrCreateVulkanDeviceKHR deviceFn = nullptr;
    if (!Load(instance, "xrGetVulkanGraphicsRequirements2KHR", &requirementsFn) ||
        !Load(instance, "xrCreateVulkanInstanceKHR", &instanceFn) ||
        !Load(instance, "xrGetVulkanGraphicsDevice2KHR", &graphicsDeviceFn) ||
        !Load(instance, "xrCreateVulkanDeviceKHR", &deviceFn)) return false;

    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (!XrOk("xrGetVulkanGraphicsRequirements2KHR",
              requirementsFn(instance, system, &requirements))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "Vulkan required API: %u.%u to %u.%u",
                        XR_VERSION_MAJOR(requirements.minApiVersionSupported),
                        XR_VERSION_MINOR(requirements.minApiVersionSupported),
                        XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
                        XR_VERSION_MINOR(requirements.maxApiVersionSupported));
    const XrVersion requested =
        requirements.maxApiVersionSupported >= XR_MAKE_VERSION(1, 1, 0) &&
        requirements.minApiVersionSupported <= XR_MAKE_VERSION(1, 1, 0)
            ? XR_MAKE_VERSION(1, 1, 0) : XR_MAKE_VERSION(1, 0, 0);
    if (requested < requirements.minApiVersionSupported ||
        requested > requirements.maxApiVersionSupported) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Vulkan API outside runtime requirements");
        return false;
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "OpenXR Vulkan Lab";
    application.apiVersion = requested >= XR_MAKE_VERSION(1, 1, 0)
        ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    VkInstanceCreateInfo vkInstanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vkInstanceInfo.pApplicationInfo = &application;
    XrVulkanInstanceCreateInfoKHR xrInstanceInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrInstanceInfo.systemId = system;
    xrInstanceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrInstanceInfo.vulkanCreateInfo = &vkInstanceInfo;
    VkResult vkResult = VK_SUCCESS;
    if (!XrOk("xrCreateVulkanInstanceKHR", instanceFn(
            instance, &xrInstanceInfo, &state.vkInstance, &vkResult)) ||
        !VkOk("xrCreateVulkanInstanceKHR/Vulkan", vkResult)) return false;

    XrVulkanGraphicsDeviceGetInfoKHR getInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    getInfo.systemId = system;
    getInfo.vulkanInstance = state.vkInstance;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    if (!XrOk("xrGetVulkanGraphicsDevice2KHR", graphicsDeviceFn(
            instance, &getInfo, &physicalDevice))) return false;
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
    if (application.apiVersion >= VK_API_VERSION_1_1 &&
        deviceProperties.apiVersion >= VK_API_VERSION_1_1) {
        VkPhysicalDeviceMultiviewFeatures multiview{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &multiview;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
        VkPhysicalDeviceMultiviewProperties multiviewProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &multiviewProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
        state.multiviewSupported = multiview.multiview == VK_TRUE &&
            multiviewProperties.maxMultiviewViewCount >= kEyeCount;
        __android_log_print(ANDROID_LOG_INFO, kTag,
                            "Vulkan multiview feature=%u maxViews=%u",
                            multiview.multiview, multiviewProperties.maxMultiviewViewCount);
    }
    state.useMultiview = state.multiviewRequested && state.multiviewSupported;
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "Vulkan multiview: supported=%u requested=%u selected=%s",
                        state.multiviewSupported, state.multiviewRequested,
                        state.useMultiview ? "multiview" : "dual-pass");
    uint32_t extensionCount = 0;
    if (!VkOk("vkEnumerateDeviceExtensionProperties(count)",
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                                 &extensionCount, nullptr))) return false;
    std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
    if (!VkOk("vkEnumerateDeviceExtensionProperties(list)",
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                                 &extensionCount, deviceExtensions.data()))) return false;
    bool densityExtension = false;
    bool density2Extension = false;
    for (const char* name : {VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME,
                             VK_EXT_FRAGMENT_DENSITY_MAP_2_EXTENSION_NAME}) {
        const bool found = std::any_of(deviceExtensions.begin(), deviceExtensions.end(),
            [name](const VkExtensionProperties& entry) {
                return std::strcmp(entry.extensionName, name) == 0;
            });
        __android_log_print(ANDROID_LOG_INFO, kTag, "Vulkan extension %s: %s",
                            name, found ? "available" : "missing");
        if (std::strcmp(name, VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME) == 0)
            densityExtension = found;
        else density2Extension = found;
    }
    if (densityExtension && density2Extension && application.apiVersion >= VK_API_VERSION_1_1) {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &state.densityFeatures;
        state.densityFeatures.pNext = &state.density2Features;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
        state.vkFoveationSupported = state.densityFeatures.fragmentDensityMap == VK_TRUE;
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "Vulkan fragment density features: map=%u dynamic=%u nonSubsampled=%u deferred=%u",
            state.densityFeatures.fragmentDensityMap,
            state.densityFeatures.fragmentDensityMapDynamic,
            state.densityFeatures.fragmentDensityMapNonSubsampledImages,
            state.density2Features.fragmentDensityMapDeferred);
    }
    state.foveation = state.useMultiview && state.xrFoveationSupported &&
        state.vkFoveationSupported ? state.requestedFoveation : Foveation::Off;
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "Foveation: requested=%s xrSupported=%u vkSupported=%u selected=%s",
        FoveationName(state.requestedFoveation), state.xrFoveationSupported,
        state.vkFoveationSupported, FoveationName(state.foveation));
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
    VkPhysicalDeviceMultiviewFeatures enabledMultiview{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    if (state.useMultiview) {
        enabledMultiview.multiview = VK_TRUE;
        vkDeviceInfo.pNext = &enabledMultiview;
    }
    const char* foveationDeviceExtensions[] = {
        VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME,
        VK_EXT_FRAGMENT_DENSITY_MAP_2_EXTENSION_NAME};
    if (state.foveation != Foveation::Off) {
        state.densityFeatures.pNext = &state.density2Features;
        state.density2Features.pNext = state.useMultiview ? &enabledMultiview : nullptr;
        vkDeviceInfo.pNext = &state.densityFeatures;
        vkDeviceInfo.enabledExtensionCount = 2;
        vkDeviceInfo.ppEnabledExtensionNames = foveationDeviceExtensions;
    }
    XrVulkanDeviceCreateInfoKHR xrDeviceInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrDeviceInfo.systemId = system;
    xrDeviceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrDeviceInfo.vulkanPhysicalDevice = physicalDevice;
    xrDeviceInfo.vulkanCreateInfo = &vkDeviceInfo;
    vkResult = VK_SUCCESS;
    if (!XrOk("xrCreateVulkanDeviceKHR", deviceFn(
            instance, &xrDeviceInfo, &state.vkDevice, &vkResult)) ||
        !VkOk("xrCreateVulkanDeviceKHR/Vulkan", vkResult)) return false;
    state.graphicsFamily = graphicsFamily;
    vkGetDeviceQueue(state.vkDevice, graphicsFamily, 0, &state.vkQueue);

    return true;
}

bool AllocateMemory(RenderState& state, VkMemoryRequirements requirements,
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

bool InitGeometry(RenderState& state) {
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

bool InitPipeline(RenderState& state) {
    VkShaderModule modules[2]{};
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = state.useMultiview ? sizeof(kCubeMultiviewVertexShader) : sizeof(kCubeVertexShader);
    shader.pCode = state.useMultiview ? kCubeMultiviewVertexShader : kCubeVertexShader;
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

bool InitDepth(RenderState& state, EyeSwapchain& eye) {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = state.depthFormat;
    image.extent = {eye.width, eye.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = state.useMultiview ? kEyeCount : 1;
    if (state.foveation != Foveation::Off) image.flags |= VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT;
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
    view.viewType = state.useMultiview ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = state.depthFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0,
                             state.useMultiview ? kEyeCount : 1};
    return VkOk("vkCreateImageView(depth)", vkCreateImageView(state.vkDevice,
            &view, nullptr, &eye.depthView));
}

bool InitSwapchains(RenderState& state, XrInstance instance, XrSystemId system,
                    XrSession session, const SceneData& scene) {
    uint32_t viewCount = 0;
    if (!XrOk("xrEnumerateViewConfigurationViews(count)", xrEnumerateViewConfigurationViews(
            instance, system, kView, 0, &viewCount, nullptr))) return false;
    if (viewCount != kEyeCount) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Expected two stereo views; got %u", viewCount);
        return false;
    }
    std::array<XrViewConfigurationView, kEyeCount> configViews{};
    for (auto& view : configViews) view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    if (!XrOk("xrEnumerateViewConfigurationViews(list)", xrEnumerateViewConfigurationViews(
            instance, system, kView, viewCount, &viewCount, configViews.data()))) return false;

    uint32_t formatCount = 0;
    if (!XrOk("xrEnumerateSwapchainFormats(count)", xrEnumerateSwapchainFormats(
            session, 0, &formatCount, nullptr))) return false;
    std::vector<int64_t> formats(formatCount);
    if (!XrOk("xrEnumerateSwapchainFormats(list)", xrEnumerateSwapchainFormats(
            session, formatCount, &formatCount, formats.data()))) return false;
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
    VkAttachmentDescription densityAttachment{};
    densityAttachment.format = VK_FORMAT_R8G8_UNORM;
    densityAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    densityAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    densityAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    densityAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    densityAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    densityAttachment.initialLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
    densityAttachment.finalLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
    const VkAttachmentDescription attachments[] = {attachment, depthAttachment, densityAttachment};
    VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = state.foveation == Foveation::Off ? 2 : 3;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    const uint32_t viewMask = (1u << kEyeCount) - 1;
    VkRenderPassMultiviewCreateInfo multiviewPass{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};
    if (state.useMultiview) {
        multiviewPass.subpassCount = 1;
        multiviewPass.pViewMasks = &viewMask;
        renderPassInfo.pNext = &multiviewPass;
    }
    VkAttachmentReference densityReference{2, VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT};
    VkRenderPassFragmentDensityMapCreateInfoEXT densityPass{
        VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT};
    if (state.foveation != Foveation::Off) {
        densityPass.fragmentDensityMapAttachment = densityReference;
        densityPass.pNext = renderPassInfo.pNext;
        renderPassInfo.pNext = &densityPass;
    }
    if (!VkOk("vkCreateRenderPass", vkCreateRenderPass(
            state.vkDevice, &renderPassInfo, nullptr, &state.renderPass))) return false;
    if (!InitGeometry(state) || !InitPipeline(state)) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "Cube geometry and graphics pipeline ready");

    for (uint32_t eyeIndex = 0; eyeIndex < kEyeCount; ++eyeIndex) {
        if (state.useMultiview && eyeIndex != 0) break;
        auto& eye = state.eyes[eyeIndex];
        bool resolutionSupported = true;
        for (uint32_t viewIndex = 0; viewIndex < kEyeCount; ++viewIndex) {
            if (!state.useMultiview && viewIndex != eyeIndex) continue;
            resolutionSupported &= configViews[viewIndex].maxImageRectWidth >= kBenchmarkWidth &&
                configViews[viewIndex].maxImageRectHeight >= kBenchmarkHeight;
        }
        if (!resolutionSupported) {
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
        createInfo.arraySize = state.useMultiview ? kEyeCount : 1;
        createInfo.mipCount = 1;
        XrSwapchainCreateInfoFoveationFB foveationCreate{
            XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB};
        XrVulkanSwapchainCreateInfoMETA vulkanCreate{
            XR_TYPE_VULKAN_SWAPCHAIN_CREATE_INFO_META};
        if (state.foveation != Foveation::Off) {
            foveationCreate.flags = XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB;
            vulkanCreate.additionalCreateFlags = VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT;
            foveationCreate.next = &vulkanCreate;
            createInfo.next = &foveationCreate;
            createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        }
        if (!XrOk("xrCreateSwapchain", xrCreateSwapchain(
                session, &createInfo, &eye.handle))) return false;
        if (state.foveation != Foveation::Off) {
            if (state.foveationProfile == XR_NULL_HANDLE) {
                XrFoveationLevelProfileCreateInfoFB level{
                    XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB};
                level.level = state.foveation == Foveation::Low
                    ? XR_FOVEATION_LEVEL_LOW_FB : XR_FOVEATION_LEVEL_HIGH_FB;
                level.dynamic = XR_FOVEATION_DYNAMIC_DISABLED_FB;
                XrFoveationProfileCreateInfoFB profile{
                    XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB};
                profile.next = &level;
                if (!XrOk("xrCreateFoveationProfileFB", state.createFoveationProfile(
                        session, &profile, &state.foveationProfile))) return false;
            }
            XrSwapchainStateFoveationFB foveationState{XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB};
            foveationState.profile = state.foveationProfile;
            if (!XrOk("xrUpdateSwapchainFB", state.updateSwapchain(eye.handle,
                    reinterpret_cast<const XrSwapchainStateBaseHeaderFB*>(&foveationState)))) return false;
        }
        uint32_t imageCount = 0;
        if (!XrOk("xrEnumerateSwapchainImages(count)", xrEnumerateSwapchainImages(
                eye.handle, 0, &imageCount, nullptr))) return false;
        if (imageCount == 0) return false;
        eye.images.resize(imageCount);
        if (state.foveation != Foveation::Off) {
            eye.densityImages.resize(imageCount);
            eye.densityViews.resize(imageCount, VK_NULL_HANDLE);
        }
        eye.views.resize(imageCount, VK_NULL_HANDLE);
        eye.framebuffers.resize(imageCount, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < imageCount; ++i) {
            eye.images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
            if (state.foveation != Foveation::Off) {
                eye.densityImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB;
                eye.images[i].next = &eye.densityImages[i];
            }
        }
        if (!XrOk("xrEnumerateSwapchainImages(list)", xrEnumerateSwapchainImages(
                eye.handle, imageCount, &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())))) return false;
        for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = eye.images[imageIndex].image;
            viewInfo.viewType = state.useMultiview ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = colorFormat;
            viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                         state.useMultiview ? kEyeCount : 1};
            if (!VkOk("vkCreateImageView", vkCreateImageView(
                    state.vkDevice, &viewInfo, nullptr, &eye.views[imageIndex]))) return false;
            if (state.foveation != Foveation::Off) {
                const auto& density = eye.densityImages[imageIndex];
                if (density.image == VK_NULL_HANDLE || density.width == 0 || density.height == 0) {
                    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "Missing fragment density map for swapchain %u image %u", eyeIndex, imageIndex);
                    return false;
                }
                VkImageViewCreateInfo densityViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                densityViewInfo.image = density.image;
                densityViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                densityViewInfo.format = VK_FORMAT_R8G8_UNORM;
                densityViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, kEyeCount};
                if (!VkOk("vkCreateImageView(fragment density map)", vkCreateImageView(
                        state.vkDevice, &densityViewInfo, nullptr,
                        &eye.densityViews[imageIndex]))) return false;
                __android_log_print(ANDROID_LOG_INFO, kTag,
                    "Fragment density map %u/%u: %ux%u", eyeIndex, imageIndex,
                    density.width, density.height);
            }
            VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebufferInfo.renderPass = state.renderPass;
            const VkImageView framebufferAttachments[] = {eye.views[imageIndex], eye.depthView,
                state.foveation != Foveation::Off ? eye.densityViews[imageIndex] : VK_NULL_HANDLE};
            framebufferInfo.attachmentCount = state.foveation == Foveation::Off ? 2 : 3;
            framebufferInfo.pAttachments = framebufferAttachments;
            framebufferInfo.width = eye.width;
            framebufferInfo.height = eye.height;
            framebufferInfo.layers = 1;
            if (!VkOk("vkCreateFramebuffer", vkCreateFramebuffer(
                    state.vkDevice, &framebufferInfo, nullptr,
                    &eye.framebuffers[imageIndex]))) return false;
        }
        __android_log_print(ANDROID_LOG_INFO, kTag, "Swapchain %u: %ux%u, %u images, %u layers",
                            eyeIndex, eye.width, eye.height, imageCount,
                            state.useMultiview ? kEyeCount : 1);
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
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "Benchmark config: %s cubes=%u resolution=%ux%u per eye samples=1 warmup=%u measure=%u colorFormat=%d depthFormat=%d foveation=%s scene=%s",
                        state.useMultiview ? "multiview" : "dual-pass",
                        scene.cubeCount, kBenchmarkWidth, kBenchmarkHeight, kWarmupFrames,
                        kMeasuredFrames, colorFormat, state.depthFormat,
                        FoveationName(state.foveation), SceneName(scene.id));
    return true;
}

bool RenderScene(RenderState& state, EyeSwapchain& eye, uint32_t imageIndex,
                 uint32_t eyeIndex, const SceneData& scene,
                 const std::array<XrView, kEyeCount>& views, double& gpuMs) {
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
    for (uint32_t channel = 0; channel < 4; ++channel)
        clear[0].color.float32[channel] = scene.clearColor[channel];
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
    std::array<Mat4, kEyeCount> viewProjection{};
    for (uint32_t viewIndex = 0; viewIndex < (state.useMultiview ? kEyeCount : 1); ++viewIndex) {
        const XrView& xrView = views[state.useMultiview ? viewIndex : eyeIndex];
        viewProjection[viewIndex] = Multiply(Projection(xrView.fov), EyeView(xrView.pose));
    }
    for (uint32_t cube = 0; cube < scene.cubeCount; ++cube) {
        PushConstants transform{};
        const Mat4 model = CubeModel(scene.cubes[cube]);
        for (uint32_t viewIndex = 0; viewIndex < (state.useMultiview ? kEyeCount : 1); ++viewIndex) {
            transform.mvp[viewIndex] = Multiply(viewProjection[viewIndex], model);
        }
        vkCmdPushConstants(state.commandBuffer, state.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           state.useMultiview ? sizeof(transform) : sizeof(Mat4), &transform);
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

void DestroyRenderer(RenderState& state) {
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
        for (auto view : eye.densityViews) {
            if (view != VK_NULL_HANDLE) vkDestroyImageView(state.vkDevice, view, nullptr);
        }
        if (eye.depthView != VK_NULL_HANDLE) vkDestroyImageView(state.vkDevice, eye.depthView, nullptr);
        if (eye.depthImage != VK_NULL_HANDLE) vkDestroyImage(state.vkDevice, eye.depthImage, nullptr);
        if (eye.depthMemory != VK_NULL_HANDLE) vkFreeMemory(state.vkDevice, eye.depthMemory, nullptr);
        if (eye.handle != XR_NULL_HANDLE) XrOk("xrDestroySwapchain", xrDestroySwapchain(eye.handle));
    }
    if (state.foveationProfile != XR_NULL_HANDLE && state.destroyFoveationProfile)
        XrOk("xrDestroyFoveationProfileFB", state.destroyFoveationProfile(state.foveationProfile));
    if (state.renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(state.vkDevice, state.renderPass, nullptr);
    if (state.vkDevice != VK_NULL_HANDLE) vkDestroyDevice(state.vkDevice, nullptr);
    if (state.vkInstance != VK_NULL_HANDLE) vkDestroyInstance(state.vkInstance, nullptr);
}
}  // namespace

Renderer::Renderer() : state_(std::make_unique<RenderState>()) {}
Renderer::~Renderer() { Destroy(); }

bool Renderer::InitializeDevice(XrInstance instance, XrSystemId system,
                                bool xrFoveationSupported, bool multiviewRequested,
                                Foveation requestedFoveation) {
    return InitDevice(*state_, instance, system, xrFoveationSupported,
                      multiviewRequested, requestedFoveation);
}

XrGraphicsBindingVulkan2KHR Renderer::GraphicsBinding() const {
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = state_->vkInstance;
    binding.physicalDevice = state_->vkPhysicalDevice;
    binding.device = state_->vkDevice;
    binding.queueFamilyIndex = state_->graphicsFamily;
    binding.queueIndex = 0;
    return binding;
}

bool Renderer::InitializeSwapchains(XrInstance instance, XrSystemId system,
                                    XrSession session, const SceneData& scene) {
    if (state_->foveation != Foveation::Off) {
        if (!Load(instance, "xrCreateFoveationProfileFB", &state_->createFoveationProfile) ||
            !Load(instance, "xrDestroyFoveationProfileFB", &state_->destroyFoveationProfile) ||
            !Load(instance, "xrUpdateSwapchainFB", &state_->updateSwapchain)) return false;
    }
    return InitSwapchains(*state_, instance, system, session, scene);
}

bool Renderer::Render(const SceneData& scene, const std::array<XrView, kEyeCount>& views,
                      uint32_t swapchainIndex, uint32_t imageIndex, double& gpuMs) {
    return RenderScene(*state_, state_->eyes[swapchainIndex], imageIndex,
                       swapchainIndex, scene, views, gpuMs);
}

bool Renderer::UseMultiview() const { return state_->useMultiview; }
uint32_t Renderer::SwapchainCount() const { return state_->useMultiview ? 1 : kEyeCount; }
XrSwapchain Renderer::SwapchainAt(uint32_t index) const { return state_->eyes[index].handle; }
uint32_t Renderer::WidthAt(uint32_t index) const { return state_->eyes[index].width; }
uint32_t Renderer::HeightAt(uint32_t index) const { return state_->eyes[index].height; }

void Renderer::Destroy() {
    if (state_) {
        DestroyRenderer(*state_);
        state_.reset();
    }
}
