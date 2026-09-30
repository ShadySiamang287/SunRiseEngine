#pragma once
#include "SceneManagement/BaseScene.h"
#include "AssetManagement/Asset.h"

namespace SUN {
    class AssetManager;

    class BindlessTextureTestScene final : public BaseScene {
    public:
        explicit BindlessTextureTestScene(AssetManager& assetManager);
        void OnEnter() override;
        void Render(RenderContext& context) override;

    private:
        AssetManager& mAssetManager;
        Camera mCamera;
        AssetID mCheckerTexture = INVALID_ASSET_ID;
    };
}
