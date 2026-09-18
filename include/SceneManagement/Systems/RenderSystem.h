#pragma once
#include "SceneManagement/BaseScene.h"


namespace SUN{
    class RenderSystem{
    public:
        static void Render(BaseScene& scene, Renderer3D* renderer);
    };
}