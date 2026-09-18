#pragma once

#include <chrono>

namespace SUN{
    class TimeManager{
    public:
        using Clock = std::chrono::steady_clock;

        void Update();

        float GetDeltaTime() const {
            return mDeltaTime;
        }

    private:
        Clock::time_point mLastTime = Clock::now();
        float mDeltaTime;
    };
}