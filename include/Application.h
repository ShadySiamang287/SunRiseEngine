#pragma once
#include <memory>

#include "Logger.h"
#include "Graphics/Window.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/ResourceFactory.h"
#include "Graphics/GraphicsCommands.h"
#include "SceneManagement/BaseScene.h"


namespace SUN{
    class Application {
    public:
        Application(){
            Logger::Init();
            mWindowPtr = std::make_unique<Window>();
            mWindowPtr->Init();

            mGraphicsContextPtr = std::make_unique<GraphicsContext>();
            mGraphicsContextPtr->Init(mWindowPtr.get());

            mResourceFactoryPtr = std::make_unique<ResourceFactory>(mGraphicsContextPtr.get());
        
            GraphicsCommands::RegisterContext(mGraphicsContextPtr.get());
        }
    
        ~Application(){

        }

        void Run(){
            while (!mWindowPtr->ShouldClose()){
                mWindowPtr->PollEvents();
                mCurrentScenePtr->HandleInput();
                mCurrentScenePtr->Update();

                GraphicsCommands::BeginFrame();
                mCurrentScenePtr->Render();
                GraphicsCommands::EndFrame();
            }

            mGraphicsContextPtr->Shutdown();
            mCurrentScenePtr.reset();
            mWindowPtr->Shutdown();
            Logger::Shutdown();
        }
    protected:
        std::unique_ptr<BaseScene> mCurrentScenePtr;
    
    private:
        std::unique_ptr<Window> mWindowPtr;
        std::unique_ptr<GraphicsContext> mGraphicsContextPtr;
        std::unique_ptr<ResourceFactory> mResourceFactoryPtr;
    };
}