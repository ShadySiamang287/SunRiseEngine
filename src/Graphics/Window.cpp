#include "Graphics/Window.h"

using namespace SUN;

void Window::Init() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    mWindowPtr = glfwCreateWindow(mWidth, mHeight, "Vulkan App", nullptr, nullptr);
}

void Window::Shutdown(){
    glfwDestroyWindow(mWindowPtr);
    glfwTerminate();
}

void Window::PollEvents(){
    glfwPollEvents();
}

bool Window::ShouldClose(){
    return glfwWindowShouldClose(mWindowPtr);
}