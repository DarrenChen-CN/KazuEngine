// ============================================================================
// KazuEngine - Application Layer: Main Application Controller (Implementation)
// ============================================================================

#include "app/Application.h"
#include "app/AppUI.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "technique/DeferredShading.h"
#include <spdlog/spdlog.h>

namespace kazu {

Application::Application() = default;

Application::~Application() {
    cleanup();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Application::init() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(
        static_cast<int>(m_windowWidth),
        static_cast<int>(m_windowHeight),
        "KazuEngine", nullptr, nullptr);
    if (!m_window) {
        fatalError("Failed to create GLFW window!");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPosCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);

    m_rhi = std::make_unique<RHI>();
    m_rhi->init(m_window);

    m_scene = std::make_unique<Scene>();
    m_scene->loadFromFile(
        m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache(),
        "assets/scenes/sample-scene.json");

    const auto& cfg = m_scene->config();
    if (cfg.windowWidth != m_windowWidth || cfg.windowHeight != m_windowHeight) {
        glfwSetWindowSize(m_window,
            static_cast<int>(cfg.windowWidth),
            static_cast<int>(cfg.windowHeight));
        m_windowWidth = cfg.windowWidth;
        m_windowHeight = cfg.windowHeight;
    }

    m_camera = std::make_unique<Camera>();
    m_camera->setPosition(cfg.cameraEye);
    m_camera->setTarget(cfg.cameraTarget);
    m_camera->setUp(cfg.cameraUp);

    m_deferred = std::make_unique<DeferredShading>();
    m_deferred->init(m_rhi.get(), m_scene.get(), m_camera.get());

    m_appUI = std::make_unique<AppUI>();
    m_appUI->init(m_rhi.get(), m_window);

    return true;
}

void Application::run() {
    spdlog::info("KazuEngine initialized. Close window to exit.");
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        uint32_t imageIndex = 0;
        if (!m_rhi->beginFrame(imageIndex)) {
            if (m_appUI) m_appUI->onResize();
            continue;
        }

        recordFrame(imageIndex);
        m_rhi->endFrame(imageIndex);
    }
}

void Application::cleanup() {
    m_appUI.reset();
    m_deferred.reset();
    m_camera.reset();
    m_scene.reset();
    m_rhi.reset();
}

void Application::recordFrame(uint32_t imageIndex) {
    m_deferred->setCurrentImageIndex(imageIndex);
    m_deferred->renderGraph()->execute(m_rhi->currentCmd());

    m_appUI->beginFrame();
    PanelDesc desc;
    m_deferred->exposePanel(desc);
    m_appUI->drawPanel(desc);
    m_appUI->endFrame(m_rhi->currentCmd(), imageIndex);
}

// ---------------------------------------------------------------------------
// Input handlers
// ---------------------------------------------------------------------------

void Application::onKey(int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;
    if (key == GLFW_KEY_D && action == GLFW_PRESS && m_deferred) {
        int mode = (m_deferred->displayMode() + 1) % 3;
        m_deferred->setDisplayMode(mode);
        const char* name = (mode == 0) ? "lighting" : (mode == 1) ? "albedo" : "normal";
        spdlog::info("Display mode: {}", name);
    }
}

void Application::onMouseButton(int button, int action, int mods) {
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        m_dragButton = 0;
        glfwGetCursorPos(m_window, &m_lastMouseX, &m_lastMouseY);
    } else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        if (m_dragButton == 0) m_dragButton = -1;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        m_dragButton = 1;
        glfwGetCursorPos(m_window, &m_lastMouseX, &m_lastMouseY);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        if (m_dragButton == 1) m_dragButton = -1;
    }
}

void Application::onCursorPos(double xpos, double ypos) {
    if (m_dragButton == -1 || !m_camera) return;
    double dx = xpos - m_lastMouseX;
    double dy = ypos - m_lastMouseY;
    m_lastMouseX = xpos;
    m_lastMouseY = ypos;

    if (m_dragButton == 0) {
        m_camera->orbit(static_cast<float>(-dx) * MOUSE_SENSITIVITY,
                        static_cast<float>(dy) * MOUSE_SENSITIVITY);
    } else if (m_dragButton == 1) {
        m_camera->pan(static_cast<float>(dx) * MOUSE_SENSITIVITY * PAN_SENSITIVITY,
                      static_cast<float>(dy) * MOUSE_SENSITIVITY * PAN_SENSITIVITY);
    }
}

void Application::onScroll(double xoffset, double yoffset) {
    (void)xoffset;
    if (m_camera) {
        m_camera->zoom(static_cast<float>(yoffset) * ZOOM_SENSITIVITY);
    }
}

void Application::onFramebufferResize(int width, int height) {
    (void)width; (void)height;
    if (m_rhi) m_rhi->setFramebufferResized(true);
}

// ---------------------------------------------------------------------------
// GLFW static trampolines
// ---------------------------------------------------------------------------

void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) app->onKey(key, scancode, action, mods);
}

void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) app->onMouseButton(button, action, mods);
}

void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) app->onCursorPos(xpos, ypos);
}

void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) app->onScroll(xoffset, yoffset);
}

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) app->onFramebufferResize(width, height);
}

} // namespace kazu
