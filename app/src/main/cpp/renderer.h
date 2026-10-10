#pragma once

#include "xr_common.h"
#include "scene.h"
#include <array>
#include <cstdint>
#include <memory>

struct RenderState;

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool InitializeDevice(XrInstance instance, XrSystemId system,
                          bool xrFoveationSupported, bool multiviewRequested,
                          Foveation requestedFoveation);
    XrGraphicsBindingVulkan2KHR GraphicsBinding() const;
    bool InitializeSwapchains(XrInstance instance, XrSystemId system,
                              XrSession session, const SceneData& scene);
    bool Render(const SceneData& scene, const std::array<XrView, kEyeCount>& views,
                uint32_t swapchainIndex, uint32_t imageIndex, double& gpuMs);
    bool UseMultiview() const;
    uint32_t SwapchainCount() const;
    XrSwapchain SwapchainAt(uint32_t index) const;
    uint32_t WidthAt(uint32_t index) const;
    uint32_t HeightAt(uint32_t index) const;
    void Destroy();

private:
    std::unique_ptr<RenderState> state_;
};
