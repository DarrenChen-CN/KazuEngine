// ============================================================================
// KazuEngine - Main Entry Point
//
// Minimal application layer: window, input, scene, camera.
// All rendering technique logic lives in technique/DeferredShading.
// ============================================================================

#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "technique/DeferredShading.h"
#include "rendergraph/RenderGraph.h"

#include <memory>

// Window
const uint32_t WINDOW_WIDTH = 800;
const uint32_t WINDOW_HEIGHT = 600;
GLFWwindow* g_window = nullptr;

// Application objects
std::unique_ptr<kazu::RHI> g_rhi;
std::unique_ptr<kazu::Scene> g_scene;
std::unique_ptr<kazu::Camera> g_camera;
std::unique_ptr<kazu::DeferredShading> g_deferred;

// Mouse input
int g_dragButton = -1;
double g_lastMouseX = 0.0;
double g_lastMouseY = 0.0;
const float MOUSE_SENSITIVITY = 0.005f;
const float ZOOM_SENSITIVITY = 0.5f;
const float PAN_SENSITIVITY = 2.0f;

// ============================================================================
// Input callbacks
// ============================================================================

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_D && action == GLFW_PRESS && g_deferred) {
        int mode = (g_deferred->displayMode() + 1) % 3;
        g_deferred->setDisplayMode(mode);
        const char* name = (mode == 0) ? "lighting" : (mode == 1) ? "albedo" : "normal";
        spdlog::info("Display mode: {}", name);
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        g_dragButton = 0;
        glfwGetCursorPos(window, &g_lastMouseX, &g_lastMouseY);
    } else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        if (g_dragButton == 0) g_dragButton = -1;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        g_dragButton = 1;
        glfwGetCursorPos(window, &g_lastMouseX, &g_lastMouseY);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        if (g_dragButton == 1) g_dragButton = -1;
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (g_dragButton == -1 || !g_camera) return;
    double dx = xpos - g_lastMouseX;
    double dy = ypos - g_lastMouseY;
    g_lastMouseX = xpos;
    g_lastMouseY = ypos;

    if (g_dragButton == 0) {
        g_camera->orbit(static_cast<float>(-dx) * MOUSE_SENSITIVITY,
                        static_cast<float>(dy) * MOUSE_SENSITIVITY);
    } else if (g_dragButton == 1) {
        g_camera->pan(static_cast<float>(dx) * MOUSE_SENSITIVITY * PAN_SENSITIVITY,
                      static_cast<float>(dy) * MOUSE_SENSITIVITY * PAN_SENSITIVITY);
    }
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_camera) {
        g_camera->zoom(static_cast<float>(yoffset) * ZOOM_SENSITIVITY);
    }
}

void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    if (g_rhi) g_rhi->setFramebufferResized(true);
}

// ============================================================================
// Frame rendering
// ============================================================================

void recordFrame(uint32_t imageIndex) {
    g_deferred->setCurrentImageIndex(imageIndex);
    g_deferred->renderGraph()->execute(g_rhi->currentCmd());
}

// ============================================================================
// Initialization & main loop
// ============================================================================

void initApp() {
    g_rhi = std::make_unique<kazu::RHI>();
    g_rhi->init(g_window);

    g_scene = std::make_unique<kazu::Scene>();
    g_scene->loadFromFile(g_rhi->ctx(), g_rhi->shaderLib(), g_rhi->dslCache(),
                          "assets/scenes/sample-scene.json");

    const auto& cfg = g_scene->config();
    g_camera = std::make_unique<kazu::Camera>();
    g_camera->setPosition(cfg.cameraEye);
    g_camera->setTarget(cfg.cameraTarget);
    g_camera->setUp(cfg.cameraUp);

    g_deferred = std::make_unique<kazu::DeferredShading>();
    g_deferred->init(g_rhi.get(), g_scene.get(), g_camera.get());
}

void cleanupApp() {
    g_deferred.reset();
    g_camera.reset();
    g_scene.reset();
    g_rhi.reset();
}

int main() {
    kazu::initLogger();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    g_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "KazuEngine", nullptr, nullptr);
    if (!g_window) {
        kazu::fatalError("Failed to create GLFW window!");
    }

    glfwSetKeyCallback(g_window, keyCallback);
    glfwSetMouseButtonCallback(g_window, mouseButtonCallback);
    glfwSetCursorPosCallback(g_window, cursorPosCallback);
    glfwSetScrollCallback(g_window, scrollCallback);
    glfwSetFramebufferSizeCallback(g_window, framebufferResizeCallback);

    try {
        initApp();
        spdlog::info("KazuEngine initialized. Close window to exit.");

        while (!glfwWindowShouldClose(g_window)) {
            glfwPollEvents();

            uint32_t imageIndex = 0;
            if (!g_rhi->beginFrame(imageIndex)) continue;

            recordFrame(imageIndex);

            g_rhi->endFrame(imageIndex);
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        cleanupApp();
        glfwDestroyWindow(g_window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    cleanupApp();
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
