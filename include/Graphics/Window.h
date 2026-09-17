#include <GLFW/glfw3.h>

namespace SUN{
    class GraphicsContext;

    class Window{
    public:
        void Init();
        void Shutdown();

        void PollEvents();

        bool ShouldClose();


        bool mResized = false;
    private:
        GLFWwindow* mWindowPtr {nullptr};
        uint32_t mWidth = 800;
        uint32_t mHeight = 600;

        friend GraphicsContext;
    };
}
