#pragma once

#include "Renderer/PostProcessor/PostProcessorPass.h"

namespace SUN {
    
    class PostProcessor {
    public:

        void Begin(RenderImage& source, uint32_t frameIndex);

        void Execute(PostProcessPass& pass);

        RenderImage& GetCurrentImage(uint32_t frameIndex);

    private:
        RenderImage* mCurrentInput = nullptr;
        uint32_t mFrameIndex = 0;
    };
}