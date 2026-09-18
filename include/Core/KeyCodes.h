#pragma once

#include <GLFW/glfw3.h>

namespace SUN {
    enum class KeyCode : uint16_t {
        Space       = GLFW_KEY_SPACE,
        Escape      = GLFW_KEY_ESCAPE,
        Enter       = GLFW_KEY_ENTER,
        Tab         = GLFW_KEY_TAB,

        W           = GLFW_KEY_W,
        A           = GLFW_KEY_A,
        S           = GLFW_KEY_S,
        D           = GLFW_KEY_D,

        Q           = GLFW_KEY_Q,
        E           = GLFW_KEY_E,

        LeftShift   = GLFW_KEY_LEFT_SHIFT,
        LeftCtrl    = GLFW_KEY_LEFT_CONTROL,
        LeftAlt     = GLFW_KEY_LEFT_ALT,

        Up          = GLFW_KEY_UP,
        Down        = GLFW_KEY_DOWN,
        Left        = GLFW_KEY_LEFT,
        Right       = GLFW_KEY_RIGHT
    };
}