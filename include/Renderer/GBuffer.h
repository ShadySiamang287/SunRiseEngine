#pragma once

#include "Renderer/PostProcessor/PostProcessorPass.h"

namespace SUN {
    struct GBuffer {
        RenderImage albedo;
        RenderImage normal;
        RenderImage depth;
    };
}
