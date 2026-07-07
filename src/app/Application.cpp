// ============================================================================
// KazuEngine - Application Layer: Main Application Controller (Implementation)
// ============================================================================

#include "app/Application.h"
#include "app/AppUI.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "technique/Technique.h"
#include "technique/DeferredShading.h"
#include "precompute/PrecomputeManager.h"
#include "precompute/EquirectToCubePass.h"
#include "precompute/IrradiancePass.h"
#include "precompute/PrefilterEnvPass.h"
#include "precompute/BRDFLutPass.h"
#include "rhi/Texture.h"
#include "rendergraph/RenderGraph.h"
#include <spdlog/spdlog.h>
#include <vector>

namespace kazu {

// 05-00e-6 smoke test: verify cubemap + mip chain + per-mip view allocation.
static void smokeTestCubemapMip(RHI* rhi) {
    RenderGraph rg(rhi->ctx());
    auto cube = rg.addTexture("CubeSmoke",
        {.width = 64,
         .height = 64,
         .mipLevels = 5,
         .arrayLayers = 6,
         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
         .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT});

    rg.addPass("CubeSmokeWrite", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.writeStorageImage(cube);
        b.execute = [](const PassExecuteContext&) {};
    });

    if (!rg.compile()) {
        spdlog::error("[CubeSmoke] RenderGraph compile failed");
        return;
    }

    Image* image = rg.getImage(cube);
    if (!image) {
        spdlog::error("[CubeSmoke] Failed to get image");
        return;
    }

    const auto& desc = image->desc();
    bool ok = true;
    ok &= (desc.mipLevels == 5);
    ok &= (desc.arrayLayers == 6);
    ok &= ((desc.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0);

    VkImageView cubeView = image->view();
    ok &= (cubeView != VK_NULL_HANDLE);

    VkImageView mip1View = image->createView({
        VK_IMAGE_VIEW_TYPE_2D,
        VK_FORMAT_UNDEFINED,
        1, 1,
        0, 1});
    ok &= (mip1View != VK_NULL_HANDLE);

    VkImageView face0Mip2View = image->createView({
        VK_IMAGE_VIEW_TYPE_2D,
        VK_FORMAT_UNDEFINED,
        2, 1,
        0, 1});
    ok &= (face0Mip2View != VK_NULL_HANDLE);

    if (ok) {
        spdlog::info("[CubeSmoke] Cubemap + mip chain + per-mip view OK (mipLevels={}, arrayLayers={})",
                     desc.mipLevels, desc.arrayLayers);
    } else {
        spdlog::error("[CubeSmoke] Cubemap verification failed");
    }
}

Application::Application() = default;

Application::~Application() {
    cleanup();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Application::init(const std::string& scenePath) {
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
    m_rhi->init(m_window, UI_PANEL_WIDTH);

    // 05-00e-6 validation: cubemap + mip chain + per-mip view.
    smokeTestCubemapMip(m_rhi.get());

    m_scene = std::make_unique<Scene>();
    spdlog::info("Loading scene: {}", scenePath);
    m_scene->loadFromFile(m_rhi->ctx(), scenePath);

    // Run one-shot GPU precompute passes after scene load, before technique init.
    m_precomputeManager = std::make_unique<PrecomputeManager>();
    m_precomputeManager->init(m_rhi.get(), m_scene.get());

    auto brdfLutPass = std::make_unique<BRDFLutPass>();
    brdfLutPass->setSize(512);
    m_precomputeManager->registerPass(std::move(brdfLutPass));

    // 05-01b: load HDR equirect and run IBL precompute passes if configured.
    const auto& env = m_scene->rendererSettings().environment;
    if (env.enabled && !env.hdrPath.empty()) {
        m_equirectTexture = std::make_unique<Texture>(m_rhi->ctx(), env.hdrPath, false);
        m_precomputeManager->registerPass(std::make_unique<EquirectToCubePass>(m_equirectTexture.get()));
        m_precomputeManager->registerPass(std::make_unique<IrradiancePass>());
        m_precomputeManager->registerPass(std::make_unique<PrefilterEnvPass>(128));
    }

    m_precomputeManager->run();

    const auto& cfg = m_scene->config();
    if (cfg.windowWidth != m_windowWidth || cfg.windowHeight != m_windowHeight) {
        // The config stores the desired rendering resolution; the actual window
        // must also accommodate the left-side UI panel.
        uint32_t totalWidth = cfg.windowWidth + UI_PANEL_WIDTH;
        uint32_t totalHeight = cfg.windowHeight;
        glfwSetWindowSize(m_window,
            static_cast<int>(totalWidth),
            static_cast<int>(totalHeight));
        m_windowWidth = totalWidth;
        m_windowHeight = totalHeight;
    }

    m_camera = std::make_unique<Camera>();
    m_camera->setPosition(cfg.cameraEye);
    m_camera->setTarget(cfg.cameraTarget);
    m_camera->setUp(cfg.cameraUp);

    Texture* irradiance     = m_precomputeManager->getTexture("Irradiance");
    Texture* prefilter      = m_precomputeManager->getTexture("PrefilterEnv");
    Texture* brdfLut        = m_precomputeManager->getTexture("BRDFLut");
    Texture* environmentCube = m_precomputeManager->getTexture("EnvironmentCube");
    m_technique = std::make_unique<DeferredShading>();
    m_technique->setIBL(irradiance, prefilter, brdfLut);
    m_technique->setEnvironment(environmentCube);
    m_technique->init(m_rhi.get(), m_scene.get(), m_camera.get());

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
            if (m_technique) m_technique->init(m_rhi.get(), m_scene.get(), m_camera.get());
            continue;
        }

        recordFrame(imageIndex);
        m_rhi->endFrame(imageIndex);
    }
}

void Application::cleanup() {
    m_appUI.reset();
    m_precomputeManager.reset();
    m_technique.reset();
    m_camera.reset();
    m_scene.reset();
    m_equirectTexture.reset();
    m_rhi.reset();
}

void Application::recordFrame(uint32_t imageIndex) {
    RenderFrameContext frame{};
    frame.cmd = m_rhi->currentCmd();
    frame.imageIndex = imageIndex;
    frame.swapchainImage = m_rhi->swapchainImage(imageIndex);
    frame.swapchainImageView = m_rhi->swapchainImageView(imageIndex);
    m_technique->render(frame);

    m_appUI->beginFrame();

    PanelDesc desc;
    m_technique->exposePanel(desc);
    m_appUI->drawPanel(desc);
    m_appUI->endFrame(m_rhi->currentCmd(), imageIndex);
}

// ---------------------------------------------------------------------------
// Input handlers
// ---------------------------------------------------------------------------

void Application::onKey(int key, int scancode, int action, int mods) {
    if (m_technique && m_technique->onKey(key, scancode, action, mods)) return;
}

void Application::onMouseButton(int button, int action, int mods) {
    (void)mods;
    if (AppUI::wantsMouseInput()) return;
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
    if (m_dragButton == -1 || !m_camera || AppUI::wantsMouseInput()) return;
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
    if (AppUI::wantsMouseInput()) return;
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
