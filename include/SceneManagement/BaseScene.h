#pragma once

namespace SUN{
    class BaseScene{
    public:
        virtual ~BaseScene() = default;

        virtual void OnEnter() {};
        virtual void OnExit() {};

        virtual void HandleInput()  {};
        virtual void Update(const float& dt) {};
        virtual void Render() {};
    };
}