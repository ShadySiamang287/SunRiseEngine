#include "Core/Application.h"

#include "Logger.h"

#include "Graphics/GraphicsCommands.h"
#include "Graphics/Buffers.h"

#include "Renderer/Renderer3D.h"
#include "Core/Input.h"

using namespace SUN;

Application::Application() {
    Logger::Init();

    mWindowPtr = std::make_unique<Window>();
    mWindowPtr->Init();

    Input::RegisterWindow(mWindowPtr.get());

    mGraphicsContextPtr = std::make_unique<GraphicsContext>();
    mGraphicsContextPtr->Init(mWindowPtr.get());

    mResourceFactoryPtr = std::make_unique<ResourceFactory>(mGraphicsContextPtr.get());

    GraphicsCommands::RegisterContext(mGraphicsContextPtr.get());
    Buffer::RegisterContext(mGraphicsContextPtr.get());

    mRenderer3D = std::make_unique<Renderer3D>();
}

Application::~Application() {
    mGraphicsContextPtr->mDevice.waitIdle();
    mRenderer3D.reset();
    mLayerStack.Clear();
    mGraphicsContextPtr->Shutdown();
    mWindowPtr->Shutdown();
    Logger::Shutdown();
}

void Application::Run() {
    while (!mWindowPtr->ShouldClose()){
        Input::BeginFrame();
        mWindowPtr->PollEvents();

        mTimeManager.Update();
        const float dt = mTimeManager.GetDeltaTime();

        for (auto& layer : mLayerStack) {
            layer->OnUpdate(dt);
        }

        if (!GraphicsCommands::BeginFrame()) {
            HandleRendererResize();
            continue;
        }
        HandleRendererResize();

        RenderContext context {
            mRenderer3D.get(),
            mGraphicsContextPtr->GetFrameIndex()
        };

        for (auto& layer : mLayerStack) {
            layer->OnRender(context);
        }
        GraphicsCommands::EndFrame();
    }
}

void Application::PushLayer(std::unique_ptr<Layer> layer){
    mLayerStack.PushLayer(std::move(layer));
}

void Application::HandleRendererResize() {
    const uint64_t generation = mGraphicsContextPtr->GetSwapchainGeneration(); 


    if (generation == mRendererSwapchainGeneration)
        return;

    mRenderer3D->Resize(
        GraphicsCommands::GetSwapchainExtent()
    );

    mRendererSwapchainGeneration = generation;
}