#pragma once
#include <memory>

#include "Logger.h"
#include "Graphics/Window.h"
#include "Graphics/GraphicsContext.h"


namespace SUN{
    class Application {
    public:
        Application(){
            Logger::Init();
            mWindowPtr = std::make_unique<Window>();
            mWindowPtr->Init();

            mGraphicsContextPtr = std::make_unique<GraphicsContext>();
            mGraphicsContextPtr->Init();
        }
    
        ~Application(){

        }

        void Run(){
            Logger::Log(Logger::LOG, "Main loop started!");
            while (!mWindowPtr->ShouldClose()){
                mWindowPtr->PollEvents();
            }

            mWindowPtr->Shutdown();
            Logger::Shutdown();
        }
    private:
        std::unique_ptr<Window> mWindowPtr;
        std::unique_ptr<GraphicsContext> mGraphicsContextPtr;
    };
}