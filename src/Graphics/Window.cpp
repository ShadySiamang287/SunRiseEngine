#include "Graphics/Window.h"


static void FrameBufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto windowManager = reinterpret_cast<SUN::Window*>(glfwGetWindowUserPointer(window));
    windowManager->mResized = true;
}

using namespace SUN;

void Window::Init() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    
    mWindowPtr = glfwCreateWindow(mWidth, mHeight, "Vulkan App", nullptr, nullptr);
    glfwSetWindowUserPointer(mWindowPtr, this);
    glfwSetFramebufferSizeCallback(mWindowPtr, FrameBufferResizeCallback);
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
