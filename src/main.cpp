// ============================================================================
// KazuEngine - Main Entry Point
//
// Minimal application layer: window, input, scene, camera.
// All Vulkan infrastructure lives in RHI.
// ============================================================================

#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "rhi/PipelineBuilder.h"
#include "rhi/PipelineCache.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <array>

// Window
const uint32_t WINDOW_WIDTH = 800;
const uint32_t WINDOW_HEIGHT = 600;
GLFWwindow* g_window = nullptr;

// Application objects
std::unique_ptr<kazu::RHI> g_rhi;
std::unique_ptr<kazu::Scene> g_scene;
std::unique_ptr<kazu::Camera> g_camera;
int g_displayMode = 0;  // 0 = color, 1 = depth

// Mouse input
// -1 = none, 0 = left (orbit), 1 = right (pan)
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
    if (key == GLFW_KEY_D && action == GLFW_PRESS) {
        g_displayMode = (g_displayMode + 1) % 2;
        spdlog::info("Display mode: {}", g_displayMode == 0 ? "color" : "depth");
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
        // Left drag: orbit
        g_camera->orbit(static_cast<float>(-dx) * MOUSE_SENSITIVITY,
                        static_cast<float>(dy) * MOUSE_SENSITIVITY);
    } else if (g_dragButton == 1) {
        // Right drag: pan
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

struct PushData {
    glm::mat4 mvp;
    glm::vec4 lightPos;
    glm::vec4 viewPos;
    int displayMode;
    int _pad[3];
};

void recordFrame(uint32_t imageIndex) {
    VkCommandBuffer cmd = g_rhi->currentCmd();

    // Begin render pass
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = g_rhi->renderPass();
    rpInfo.framebuffer = g_rhi->framebuffer(imageIndex);
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = g_rhi->extent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_rhi->graphicsPipeline());

    // Dynamic viewport & scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(g_rhi->extent().width);
    viewport.height = static_cast<float>(g_rhi->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = g_rhi->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push constants
    PushData push{};
    glm::mat4 view = g_camera->getViewMatrix();
    glm::mat4 proj = g_camera->getProjectionMatrix(g_rhi->aspect());
    push.mvp = proj * view;
    push.lightPos = glm::vec4(g_scene->config().lightPos, 0.0f);
    push.viewPos = glm::vec4(g_camera->position(), 0.0f);
    push.displayMode = g_displayMode;

    vkCmdPushConstants(cmd, g_rhi->pipelineLayout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PushData), &push);

    g_scene->draw(cmd, g_rhi->pipelineLayout());
    vkCmdEndRenderPass(cmd);
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

    // --- Week 4.2 Transient Resource allocation smoke test ---
    {
        kazu::RenderGraph rg(g_rhi->ctx());
        auto color = rg.addTexture("GBufferColor",
            {WINDOW_WIDTH, WINDOW_HEIGHT, VK_FORMAT_R8G8B8A8_UNORM,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
        auto depth = rg.addTexture("GBufferDepth",
            {WINDOW_WIDTH, WINDOW_HEIGHT, VK_FORMAT_D32_SFLOAT,
             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
        auto unused = rg.addTexture("Unused",
            {128, 128, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT});

        rg.addPass("GBuffer", [&](kazu::RenderGraph::PassBuilder& b) {
            b.writeColor(0, color);
            b.writeDepth(depth);
            b.execute = [](VkCommandBuffer) {};
        });
        rg.addPass("Lighting", [&](kazu::RenderGraph::PassBuilder& b) {
            b.read(color);
            b.read(depth);
            b.execute = [](VkCommandBuffer) {};
        });

        if (!rg.compile()) {
            spdlog::error("RenderGraph compile failed!");
        } else {
            assert(rg.getImageView(color) != VK_NULL_HANDLE);
            assert(rg.getImageView(depth) != VK_NULL_HANDLE);
            assert(rg.getImageView(unused) == VK_NULL_HANDLE);
            spdlog::info("RenderGraph 4.2 transient allocation test passed.");
        }
    }
    // ---

    // --- 4.3a GBuffer MRT Pipeline smoke test ---
    {
        VkAttachmentDescription attachments[4]{};
        // Albedo
        attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Normal
        attachments[1] = attachments[0];
        // Material
        attachments[2] = attachments[0];
        // Depth
        attachments[3].format = VK_FORMAT_D32_SFLOAT;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRefs[3] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        };
        VkAttachmentReference depthRef = {3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 3;
        subpass.pColorAttachments = colorRefs;
        subpass.pDepthStencilAttachment = &depthRef;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 4;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        VkRenderPass gbufferRP;
        VK_CHECK(vkCreateRenderPass(g_rhi->ctx().device(), &rpInfo, nullptr, &gbufferRP));

        kazu::PipelineCache tempCache(g_rhi->ctx());
        kazu::PipelineBuilder builder(g_rhi->ctx(), g_rhi->shaderLib(), g_rhi->dslCache());
        builder.shader("shaders/gbuffer.frag.spv")
               .shader("shaders/triangle.vert.spv")
               .renderPass(gbufferRP);
        auto result = builder.build(tempCache);

        assert(result.pipeline != nullptr);
        assert(result.layout != nullptr);
        spdlog::info("GBuffer MRT pipeline test passed");

        vkDestroyRenderPass(g_rhi->ctx().device(), gbufferRP, nullptr);
    }
    // ---
}

void cleanupApp() {
    if (g_rhi) {
        vkDeviceWaitIdle(g_rhi->ctx().device());
    }
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
