#include "Core/Input.h"

#include <Core./Window.h>

using namespace SUN;

Input::KeyboardState Input::sKeyboard{};
Input::MouseState Input::sMouse{};

Window* Input::sWindow = nullptr;
CursorMode Input::sCursorMode = CursorMode::Normal;

void Input::RegisterWindow(Window* window)
{
    sWindow = window;
}

void Input::BeginFrame()
{
    sKeyboard.previous = sKeyboard.current;

    sMouse.previous = sMouse.current;

    sMouse.delta = sMouse.position - sMouse.previousPosition;
    sMouse.previousPosition = sMouse.position;

    sMouse.scrollDelta = 0.0f;
}

bool Input::IsKeyDown(KeyCode key)
{
    return sKeyboard.current[(int)key];
}

bool Input::IsKeyPressed(KeyCode key)
{
    auto k = (int)key;
    return sKeyboard.current[k] && !sKeyboard.previous[k];
}

bool Input::IsKeyReleased(KeyCode key)
{
    auto k = (int)key;
    return !sKeyboard.current[k] && sKeyboard.previous[k];
}

glm::vec2 Input::GetMousePosition()
{
    return sMouse.position;
}

glm::vec2 Input::GetMouseDelta()
{
    return sMouse.delta;
}

float Input::GetScrollDelta()
{
    return sMouse.scrollDelta;
}

void Input::SetCursorMode(CursorMode mode)
{
    sCursorMode = mode;

    switch (mode)
    {
        case CursorMode::Normal:
            glfwSetInputMode(sWindow->mWindowPtr, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;

        case CursorMode::Hidden:
            glfwSetInputMode(sWindow->mWindowPtr, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            break;

        case CursorMode::Disabled:
            glfwSetInputMode(sWindow->mWindowPtr, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            break;
    }
}

void Input::KeyCallback(GLFWwindow*, int key, int, int action, int)
{
    if (key < 0) return;

    sKeyboard.current[key] = action != GLFW_RELEASE;
}

void Input::MouseButtonCallback(GLFWwindow*, int button, int action, int)
{
    if (button < 0) return;

    sMouse.current[button] = action != GLFW_RELEASE;
}

void Input::CursorPosCallback(GLFWwindow*, double x, double y)
{
    sMouse.position = { (float)x, (float)y };
}

void Input::ScrollCallback(GLFWwindow*, double, double y)
{
    sMouse.scrollDelta += (float)y;
}