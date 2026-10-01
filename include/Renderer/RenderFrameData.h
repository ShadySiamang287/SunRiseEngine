#pragma once

#include <span>

#include "Renderer/RenderingStructs.h"

namespace SUN {
    class RenderFrameData {
    public:
        RenderFrameData();

        PushConstants Prepare(
            const Camera& camera,
            std::span<const ObjectData> objects,
            vk::DeviceAddress materialDataAddress,
            std::span<const GPUDirectionalLight> directionalLights,
            std::span<const GPUPointLight> pointLights
        );

    private:
        ShaderBuffer mFrameDataBuffer;
        ShaderBuffer mObjectDataBuffer;
        ShaderBuffer mDirectionalLightDataBuffer;
        ShaderBuffer mPointLightDataBuffer;

        FrameData mFrameData{};
    };
}
