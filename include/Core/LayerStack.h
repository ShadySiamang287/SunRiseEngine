#pragma once

#include <memory>
#include <vector>

namespace SUN {
    class Layer;
    class LayerStack {
    public:
        ~LayerStack();
        void PushLayer(std::unique_ptr<Layer> layer);
        void PopLayer(Layer* layer);

        void Clear();

        auto begin()  { return mLayers.begin(); }
        auto end()    { return mLayers.end(); }
        auto rbegin() { return mLayers.rbegin(); }
        auto rend()   { return mLayers.rend(); }
    private:
        std::vector<std::unique_ptr<Layer>> mLayers;
    };
}