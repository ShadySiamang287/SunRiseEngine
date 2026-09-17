#pragma once
#include <memory>

#include "Graphics/Window.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/ResourceFactory.h"
#include "Core/Layer.h"
#include "LayerStack.h"

namespace SUN{
    class Window;
    class GraphicsContext;
    class ResourceFactory;

    class Application {
    public:
        Application();

        void Run();

        void PushLayer(std::unique_ptr<Layer> layer);
    protected:
        LayerStack mLayerStack;
    
    private:
        std::unique_ptr<Window> mWindowPtr;
        std::unique_ptr<GraphicsContext> mGraphicsContextPtr;
        std::unique_ptr<ResourceFactory> mResourceFactoryPtr;
    };
}