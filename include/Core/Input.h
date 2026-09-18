#pragma once

#include <array>
#include <glm/glm.hpp>

#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"
#include "Core/CursorMode.h"

namespace SUN {
    class Window;

    class Input{
    public:
        static void RegisterWindow(Window* window);

        static void BeginFrame();

        static bool IsKeyDown(KeyCode key);
        static bool IsKeyPressed(KeyCode key);
        static bool IsKeyReleased(KeyCode key);

        static bool IsMouseDown(MouseButton button);
        static bool IsMousePressed(MouseButton button);
        static bool IsMouseReleased(MouseButton button);

        static glm::vec2 GetMousePosition();
        static glm::vec2 GetMouseDelta();
        static float GetScrollDelta();

        static void SetCursorMode(CursorMode mode);
        static CursorMode GetCursorMode();

    private:
        friend class Window;

        static void KeyCallback(GLFWwindow*, int key, int scancode, int action, int mods);
        static void MouseButtonCallback(GLFWwindow*, int button, int action, int mods);
        static void CursorPosCallback(GLFWwindow*, double xpos, double ypos);
        static void ScrollCallback(GLFWwindow*, double xoffset, double yoffset);

        struct KeyboardState
        {
            std::array<bool, GLFW_KEY_LAST + 1> current{};
            std::array<bool, GLFW_KEY_LAST + 1> previous{};
        };

        struct MouseState
        {
            std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> current{};
            std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> previous{};

            glm::vec2 position{};
            glm::vec2 previousPosition{};
            glm::vec2 delta{};

            float scrollDelta = 0.0f;
        };

        static KeyboardState sKeyboard;
        static MouseState sMouse;

        static Window* sWindow;
        static CursorMode sCursorMode;
    };
}