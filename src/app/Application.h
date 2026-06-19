// ============================================================================
// KazuEngine - Application Layer: Main Application Controller
//
// Encapsulates window creation, input handling, and the render loop.
// GLFW callbacks are routed through static trampolines to instance methods.
// ============================================================================

#pragma once
#include <memory>
#include <string>
#include <GLFW/glfw3.h>

namespace kazu {

class RHI;
class Scene;
class Camera;
class Technique;
class AppUI;

class Application {
public:
    Application();
    ~Application();

    bool init(const std::string& scenePath = "assets/scenes/sample-scene.json");
    void run();

private:
    void cleanup();
    void recordFrame(uint32_t imageIndex);

    // Input handlers
    void onKey(int key, int scancode, int action, int mods);
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double xpos, double ypos);
    void onScroll(double xoffset, double yoffset);
    void onFramebufferResize(int width, int height);

    // GLFW static trampolines
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    uint32_t m_windowWidth = 1280;
    uint32_t m_windowHeight = 720;
    GLFWwindow* m_window = nullptr;

    std::unique_ptr<RHI> m_rhi;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Technique> m_technique;
    std::unique_ptr<AppUI> m_appUI;

    // Mouse input state
    int m_dragButton = -1;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    static constexpr float MOUSE_SENSITIVITY = 0.005f;
    static constexpr float ZOOM_SENSITIVITY = 0.5f;
    static constexpr float PAN_SENSITIVITY = 2.0f;
};

} // namespace kazu
