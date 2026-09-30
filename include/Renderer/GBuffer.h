#pragma once

#include "Graphics/RenderImage.h"

namespace SUN {
    struct GBuffer {
        RenderImage albedo;
        RenderImage normal;
        RenderImage depth;
    };
}
