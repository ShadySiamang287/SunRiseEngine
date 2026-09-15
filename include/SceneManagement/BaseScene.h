#pragma once

namespace SUN{
    class BaseScene{
    public:
        virtual ~BaseScene() = default;

        virtual void HandleInput()  {};
        virtual void Update() {};
        virtual void Render() {};
    };
}