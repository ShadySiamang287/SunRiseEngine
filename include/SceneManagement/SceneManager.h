#pragma once

#include <memory>

#include "SceneManagement/BaseScene.h"

namespace SUN {
    class SceneManager {
    public:
        template<typename T, typename... Args>
        void LoadScene(Args&&... args){
            if (mCurrentScene) {
                mCurrentScene->OnExit();
            }
            mCurrentScene = std::make_unique<T>(std::forward<Args>(args)...);
            mCurrentScene->OnEnter();
        }

        void HandleInput()
        {
            mCurrentScene->HandleInput();
        }

        void Update(const float& dt)
        {
            mCurrentScene->Update(dt);
        }

        void Render()
        {
            mCurrentScene->Render();
        }

    private:
        std::unique_ptr<BaseScene> mCurrentScene;
    };
}