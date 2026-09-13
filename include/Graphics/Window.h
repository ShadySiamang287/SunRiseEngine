#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace SUN{
    class GraphicsContext;

    class Window{
    public:
        void Init();
        void Shutdown();

        void PollEvents();

        bool ShouldClose();

    private:
        GLFWwindow* mWindowPtr {nullptr};
        uint32_t mWidth = 800;
        uint32_t mHeight = 600;

        friend GraphicsContext;
    };
}