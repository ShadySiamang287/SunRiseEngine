#include "Core/Application.h"

#include "Logger.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/Buffers.h"

#include "Renderer/Renderer3D.h"

using namespace SUN;

Application::Application() {
    Logger::Init();

    mWindowPtr = std::make_unique<Window>();
    mWindowPtr->Init();

    mGraphicsContextPtr = std::make_unique<GraphicsContext>();
    mGraphicsContextPtr->Init(mWindowPtr.get());

    mResourceFactoryPtr = std::make_unique<ResourceFactory>(mGraphicsContextPtr.get());

    GraphicsCommands::RegisterContext(mGraphicsContextPtr.get());
    Buffer::RegisterContext(mGraphicsContextPtr.get());

    mRenderer3D = std::make_unique<Renderer3D>();
}

void Application::Run() {
    while (!mWindowPtr->ShouldClose()){
        mWindowPtr->PollEvents();

        for (auto& layer : mLayerStack) {
            layer->OnUpdate(0.f);
        }

        if (!GraphicsCommands::BeginFrame()) {
            continue;
        }

        RenderContext context {
            mRenderer3D.get()
        };

        for (auto& layer : mLayerStack) {
            layer->OnRender(context);
        }
        GraphicsCommands::EndFrame();
    }

    mGraphicsContextPtr->mDevice.waitIdle();
    mLayerStack = {};
    mGraphicsContextPtr->Shutdown();
    mWindowPtr->Shutdown();
    Logger::Shutdown();
}

void Application::PushLayer(std::unique_ptr<Layer> layer){
    mLayerStack.PushLayer(std::move(layer));
}