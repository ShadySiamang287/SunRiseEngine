#include "Core/LayerStack.h"
#include "Core/Layer.h"

using namespace SUN;

LayerStack::~LayerStack() {
    for (auto& layer : mLayers) {
        layer->OnDetach();
        layer.reset();
    }
}

void LayerStack::PushLayer(std::unique_ptr<Layer> layer) {
    layer->OnAttach();
    mLayers.push_back(std::move(layer));
}

void LayerStack::PopLayer(Layer* layer) {
    auto it = std::find_if(mLayers.begin(), mLayers.end(), 
    [layer](const auto& ptr) {
        return ptr.get() == layer;
    });

    if (it != mLayers.end()) {
        (*it)->OnDetach();
        mLayers.erase(it);
    }
}