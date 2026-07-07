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
#include <vulkan/vulkan.h>

namespace kazu {

class RHI;
class Scene;
class Camera;
class Technique;
class AppUI;
class PrecomputeManager;
class Texture;

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

    // Layout: hard-coded left UI panel width; the right-side render area size
    // comes from the scene config's window.width/height (or the defaults below).
    static constexpr uint32_t UI_PANEL_WIDTH = 460;
    static constexpr uint32_t DEFAULT_RENDER_WIDTH = 1280;
    static constexpr uint32_t DEFAULT_RENDER_HEIGHT = 720;

    uint32_t m_windowWidth = DEFAULT_RENDER_WIDTH + UI_PANEL_WIDTH;
    uint32_t m_windowHeight = DEFAULT_RENDER_HEIGHT;
    GLFWwindow* m_window = nullptr;

    std::unique_ptr<RHI> m_rhi;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Technique> m_technique;
    std::unique_ptr<AppUI> m_appUI;
    std::unique_ptr<PrecomputeManager> m_precomputeManager;
    std::unique_ptr<Texture> m_equirectTexture;

    // Mouse input state
    int m_dragButton = -1;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    static constexpr float MOUSE_SENSITIVITY = 0.005f;
    static constexpr float ZOOM_SENSITIVITY = 0.5f;
    static constexpr float PAN_SENSITIVITY = 2.0f;
};

} // namespace kazu
