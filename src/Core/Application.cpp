#include "Core/Application.h"

#include "Logger.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/Buffers.h"

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

        for (auto& layer : mLayerStack) {
            layer->OnRender();
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