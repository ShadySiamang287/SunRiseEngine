#pragma once
#include <memory>

#include "Graphics/GraphicsContext.h"
#include "Graphics/ResourceFactory.h"
#include "Renderer/Renderer3D.h"
#include "Core/Window.h"
#include "Core/Layer.h"
#include "LayerStack.h"

namespace SUN{
    class Window;
    class GraphicsContext;
    class ResourceFactory;
    class Renderer3D;

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
        std::unique_ptr<Renderer3D> mRenderer3D;
    };
}