#pragma once
#include "Application.h"

namespace SUN {

    extern Application* CreateApplication();
}

int main(){
    std::unique_ptr<SUN::Application> app {SUN::CreateApplication()};

    app->Run();

    return 0;
}