#include <android/log.h>
#include <android_native_app_glue.h>
#include <sys/system_properties.h>
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

#include "app_state.h"

namespace {
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

void UpdateScene(State& state) {
    char requested[PROP_VALUE_MAX]{};
    __system_property_get("debug.openxrvulkanlab.scene", requested);
    if (state.sceneProperty == requested) return;
    state.sceneProperty = requested;
    SceneId id{};
    if (!ParseScene(requested, id)) {
        __android_log_print(ANDROID_LOG_WARN, kTag,
            "Unknown scene '%s'; keeping %s", requested, SceneName(state.scene.id));
        return;
    }
    if (id == state.scene.id) return;
    state.scene = MakeScene(id);
    state.renderedFrames = 0;
    state.samples.clear();
    state.havePreviousFrameStart = false;
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "Scene selected: %s cubes=%u; benchmark reset",
        SceneName(state.scene.id), state.scene.cubeCount);
}

bool ExtensionsAvailable(State& state) {
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
    state.xrFoveationSupported = true;
    for (const char* optional : {XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,
                                 XR_FB_FOVEATION_EXTENSION_NAME,
                                 XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
                                 XR_FB_FOVEATION_VULKAN_EXTENSION_NAME,
                                 XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME}) {
        const bool found = std::any_of(available.begin(), available.end(),
            [optional](const XrExtensionProperties& entry) {
                return std::strcmp(entry.extensionName, optional) == 0;
            });
        __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR extension %s: %s",
                            optional, found ? "available" : "missing");
        state.xrFoveationSupported &= found;
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
    if (!ExtensionsAvailable(state)) return false;

    XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM = app->activity->vm;
    android.applicationActivity = app->activity->clazz;
    const char* extensions[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                                XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
                                XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
                                XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,
                                XR_FB_FOVEATION_EXTENSION_NAME,
                                XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
                                XR_FB_FOVEATION_VULKAN_EXTENSION_NAME,
                                XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    info.next = &android;
    std::strncpy(info.applicationInfo.applicationName, "OpenXR Vulkan Lab",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    info.enabledExtensionCount = state.xrFoveationSupported ? 8 : 3;
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
    if (!state.renderer.InitializeDevice(state.instance, state.system,
            state.xrFoveationSupported, state.multiviewRequested,
            state.requestedFoveation)) return false;
    XrGraphicsBindingVulkan2KHR binding = state.renderer.GraphicsBinding();
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = state.system;
    if (!XrOk("xrCreateSession", xrCreateSession(state.instance, &sessionInfo, &state.session))) return false;
    __android_log_print(ANDROID_LOG_INFO, kTag, "OpenXR session created (graphics queue=%u)", binding.queueFamilyIndex);
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
                        "Benchmark complete: rendered=%u warmup=%u measured=%zu scene=%s",
                        state.renderedFrames, kWarmupFrames, state.samples.size(),
                        SceneName(state.scene.id));
    std::vector<double> values;
    values.reserve(state.samples.size());
    for (const auto& sample : state.samples) values.push_back(sample.intervalMs);
    LogPercentiles("frame_interval", values);
    if (state.renderer.UseMultiview()) {
        values.clear();
        for (const auto& sample : state.samples) values.push_back(sample.multiviewGpuMs);
        LogPercentiles("multiview_gpu", values);
    } else {
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
}

bool RunFrame(State& state) {
    UpdateScene(state);
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
            const uint32_t swapchainCount = state.renderer.SwapchainCount();
            for (uint32_t swapchainIndex = 0; swapchainIndex < swapchainCount; ++swapchainIndex) {
                const XrSwapchain swapchain = state.renderer.SwapchainAt(swapchainIndex);
                uint32_t imageIndex = 0;
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if (!XrOk("xrAcquireSwapchainImage", xrAcquireSwapchainImage(
                        swapchain, &acquire, &imageIndex))) return false;
                XrSwapchainImageWaitInfo waitImage{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                waitImage.timeout = XR_INFINITE_DURATION;
                if (!XrOk("xrWaitSwapchainImage", xrWaitSwapchainImage(
                        swapchain, &waitImage))) return false;
                double gpuMs = 0;
                if (!state.renderer.Render(state.scene, state.views, swapchainIndex,
                                           imageIndex, gpuMs)) return false;
                if (state.renderer.UseMultiview()) sample.multiviewGpuMs = gpuMs;
                else if (swapchainIndex == 0) sample.leftGpuMs = gpuMs;
                else sample.rightGpuMs = gpuMs;
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if (!XrOk("xrReleaseSwapchainImage", xrReleaseSwapchainImage(
                        swapchain, &release))) return false;
            }
            for (uint32_t eyeIndex = 0; eyeIndex < kEyeCount; ++eyeIndex) {
                const uint32_t swapchainIndex = state.renderer.UseMultiview() ? 0 : eyeIndex;
                auto& projection = layerViews[eyeIndex];
                projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                projection.pose = state.views[eyeIndex].pose;
                projection.fov = state.views[eyeIndex].fov;
                projection.subImage.swapchain = state.renderer.SwapchainAt(swapchainIndex);
                projection.subImage.imageArrayIndex = state.renderer.UseMultiview() ? eyeIndex : 0;
                projection.subImage.imageRect.extent = {
                    static_cast<int32_t>(state.renderer.WidthAt(swapchainIndex)),
                    static_cast<int32_t>(state.renderer.HeightAt(swapchainIndex))};
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
    state.renderer.Destroy();
    if (state.space != XR_NULL_HANDLE) XrOk("xrDestroySpace", xrDestroySpace(state.space));
    if (state.session != XR_NULL_HANDLE) XrOk("xrDestroySession", xrDestroySession(state.session));
    if (state.instance != XR_NULL_HANDLE) XrOk("xrDestroyInstance", xrDestroyInstance(state.instance));
}
}  // namespace

void android_main(android_app* app) {
    app->onAppCmd = OnCommand;
    app->onInputEvent = OnInput;
    __android_log_print(ANDROID_LOG_INFO, kTag, "native app started");
    State state;
    UpdateScene(state);
    char mode[PROP_VALUE_MAX]{};
    __system_property_get("debug.openxrvulkanlab.mode", mode);
    if (std::strcmp(mode, "dual-pass") == 0) state.multiviewRequested = false;
    else if (mode[0] != '\0' && std::strcmp(mode, "multiview") != 0) {
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "Unknown render mode '%s'; selecting multiview when supported", mode);
    }
    char foveation[PROP_VALUE_MAX]{};
    __system_property_get("debug.openxrvulkanlab.foveation", foveation);
    if (std::strcmp(foveation, "low") == 0) state.requestedFoveation = Foveation::Low;
    else if (std::strcmp(foveation, "high") == 0) state.requestedFoveation = Foveation::High;
    else if (foveation[0] != '\0' && std::strcmp(foveation, "off") != 0)
        __android_log_print(ANDROID_LOG_WARN, kTag,
            "Unknown foveation level '%s'; selecting off", foveation);
    bool operationSucceeded = InitInstance(app, state);
    if (operationSucceeded) operationSucceeded = InitSession(state);
    if (operationSucceeded) operationSucceeded = state.renderer.InitializeSwapchains(
        state.instance, state.system, state.session, state.scene);
    if (operationSucceeded) {
        for (auto& view : state.views) view.type = XR_TYPE_VIEW;
        state.samples.reserve(kMeasuredFrames);
    }
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
