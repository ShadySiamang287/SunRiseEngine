#pragma once

#include <memory>

#include "SceneManagement/BaseScene.h"

namespace SUN {
    class SceneManager {
    public:
        template<typename T, typename... Args>
        void LoadScene(Args&&... args)
        {
            mPendingScene = std::make_unique<T>(
                std::forward<Args>(args)...
            );
        }

        void ApplyPendingScene()
        {
            if (!mPendingScene)
                return;

            if (mCurrentScene)
                mCurrentScene->OnExit();

            mCurrentScene = std::move(mPendingScene);
            mCurrentScene->OnEnter();
        }

        void HandleInput()
        {
            if (mCurrentScene)
                mCurrentScene->HandleInput();
        }

        void Update(float dt)
        {
            if (mCurrentScene)
                mCurrentScene->Update(dt);
        }

        void Render(RenderContext& context)
        {
            if (mCurrentScene)
                mCurrentScene->Render(context);
        }

        BaseScene* GetCurrentScene()
        {
            return mCurrentScene.get();
        }

    private:
        std::unique_ptr<BaseScene> mCurrentScene;
        std::unique_ptr<BaseScene> mPendingScene;
    };
}