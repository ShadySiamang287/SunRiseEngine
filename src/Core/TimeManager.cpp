#include "Core/TimeManager.h"

using namespace SUN;

void TimeManager::Update() {
    const auto currentTime = Clock::now();

    const std::chrono::duration<float> elapsed = currentTime - mLastTime;
    mLastTime = currentTime;
    mDeltaTime = elapsed.count();
}

