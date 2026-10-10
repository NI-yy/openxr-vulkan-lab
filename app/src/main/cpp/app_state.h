#pragma once

#include "renderer.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

struct FrameSample {
    double intervalMs = 0;
    double leftGpuMs = 0;
    double rightGpuMs = 0;
    double multiviewGpuMs = 0;
};

struct State {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    Renderer renderer;
    bool multiviewRequested = true;
    Foveation requestedFoveation = Foveation::Off;
    bool xrFoveationSupported = false;
    std::array<XrView, kEyeCount> views{};
    XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool running = false;
    bool exiting = false;
    uint64_t frames = 0;
    Clock::time_point previousFrameStart{};
    bool havePreviousFrameStart = false;
    std::vector<FrameSample> samples;
    uint32_t renderedFrames = 0;
    SceneData scene = MakeScene(SceneId::Grid100);
    std::string sceneProperty;
};
