#include "Renderer/RenderFrameData.h"

#include <algorithm>

using namespace SUN;

RenderFrameData::RenderFrameData() {
    mFrameDataBuffer.Init(sizeof(FrameData));

    mObjectDataBuffer.Init(
        sizeof(ObjectData) * MAX_OBJECTS,
        true
    );

    mDirectionalLightDataBuffer.Init(
        sizeof(GPUDirectionalLight) * MAX_DIRECTIONAL_LIGHTS,
        true
    );

    mPointLightDataBuffer.Init(
        sizeof(GPUPointLight) * MAX_POINT_LIGHTS,
        true
    );
}

PushConstants RenderFrameData::Prepare(
    const Camera& camera,
    std::span<const ObjectData> objects,
    std::span<const GPUDirectionalLight> directionalLights,
    std::span<const GPUPointLight> pointLights) {

    const size_t directionalCount = std::min(
        directionalLights.size(),
        static_cast<size_t>(MAX_DIRECTIONAL_LIGHTS)
    );

    const size_t pointCount = std::min(
        pointLights.size(),
        static_cast<size_t>(MAX_POINT_LIGHTS)
    );

    mFrameData.view = camera.GetViewMatrix();
    mFrameData.inverseView = glm::inverse(mFrameData.view);

    mFrameData.proj = camera.GetProjectionMatrix();
    mFrameData.inverseProjection = glm::inverse(mFrameData.proj);

    mFrameData.cameraPosition = {
        camera.Position,
        1.f
    };

    mFrameData.directionalLightCount =
        static_cast<uint32_t>(directionalCount);

    mFrameData.pointLightCount =
        static_cast<uint32_t>(pointCount);

    mFrameDataBuffer.Upload(
        &mFrameData,
        sizeof(FrameData)
    );

    if (!objects.empty()) {
        mObjectDataBuffer.Upload(
            objects.data(),
            objects.size_bytes()
        );
    }

    if (directionalCount > 0) {
        mDirectionalLightDataBuffer.Upload(
            directionalLights.data(),
            sizeof(GPUDirectionalLight) * directionalCount
        );
    }

    if (pointCount > 0) {
        mPointLightDataBuffer.Upload(
            pointLights.data(),
            sizeof(GPUPointLight) * pointCount
        );
    }

    return {
        mFrameDataBuffer.GetDeviceAddress(),
        mObjectDataBuffer.GetDeviceAddress(),
        mDirectionalLightDataBuffer.GetDeviceAddress(),
        mPointLightDataBuffer.GetDeviceAddress()
    };
}
