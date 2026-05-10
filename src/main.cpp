// ============================================================================
// KazuEngine - Nano-Feature 01: Dirty MVP (Triangle)
// 
// Goal: Render a colored triangle using raw Vulkan API calls.
// No classes, no abstractions, no error recovery. Just the minimum viable
// pipeline from Instance to Present.
//
// Expected output: A window with a colorful triangle (red/green/blue vertices).
// ============================================================================

#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "core/Utils.h"
#include "core/Context.h"
#include "core/Swapchain.h"
#include "core/Buffer.h"
#include "core/RenderPass.h"
#include "core/PipelineLayout.h"
#include "core/GraphicsPipeline.h"
#include "core/DescriptorSetLayout.h"
#include "core/DescriptorPool.h"
#include "core/Image.h"
#include "core/CommandPool.h"
#include "core/CommandBuffer.h"
#include "core/SyncObjects.h"
#include "core/Sampler.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/PipelineBuilder.h"
#include "rhi/PipelineCache.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rhi/Texture.h"
#include "rhi/Material.h"
#include "rhi/Mesh.h"
#include "scene/Scene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "core/stb_image.h"

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <optional>
#include <set>
#include <array>

// ============================================================================
// Section 1: Configuration & Globals
// ============================================================================

const uint32_t WINDOW_WIDTH = 800;
const uint32_t WINDOW_HEIGHT = 600;
const int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

// Global handles (dirty code: globals for simplicity)
// Week 2: Context encapsulates Instance/Device/Queues
std::unique_ptr<kazu::Context> g_ctx;
std::unique_ptr<kazu::Swapchain> g_swapchain;
GLFWwindow* window = nullptr;
std::unique_ptr<kazu::RenderPass> g_renderPass;
std::unique_ptr<kazu::PipelineLayout> g_pipelineLayout;
kazu::GraphicsPipeline* g_graphicsPipeline = nullptr;  // borrowed from PipelineCache
std::unique_ptr<kazu::ShaderLibrary> g_shaderLibrary;
std::unique_ptr<kazu::PipelineCache> g_pipelineCache;
std::unique_ptr<kazu::CommandPool> g_commandPool;
std::vector<std::unique_ptr<kazu::CommandBuffer>> g_commandBuffers;
std::unique_ptr<kazu::SyncObjects> g_syncObjects;
uint32_t currentFrame = 0;
bool g_framebufferResized = false;

std::unique_ptr<kazu::DescriptorSetLayoutCache> g_descriptorSetLayoutCache;
VkDescriptorSetLayout g_descriptorSetLayout = VK_NULL_HANDLE;  // from DescriptorSetLayoutCache
std::unique_ptr<kazu::Scene> g_scene;
int g_displayMode = 0;  // 0 = color, 1 = depth

// ============================================================================
// Section 2: Utility Functions
// ============================================================================

// ============================================================================
// Section 3: Queue Family / Surface / Swapchain (moved to Context/Swapchain classes)
// ============================================================================

// ============================================================================
// Section 9: RenderPass & Pipeline
// ============================================================================

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_D && action == GLFW_PRESS) {
        g_displayMode = (g_displayMode + 1) % 2;
        spdlog::info("Display mode switched to: {}", g_displayMode == 0 ? "color" : "depth");
    }
}

void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = g_swapchain->format();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    g_renderPass = std::make_unique<kazu::RenderPass>(*g_ctx, renderPassInfo);
}

void createGraphicsPipeline() {
    kazu::PipelineBuilder builder(*g_ctx, *g_shaderLibrary, *g_descriptorSetLayoutCache);
    builder.shader("shaders/triangle.vert.spv")
           .shader("shaders/triangle.frag.spv")
           .renderPass(g_renderPass->handle())
           .frontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    auto result = builder.build(*g_pipelineCache);

    g_graphicsPipeline = result.pipeline;
    g_pipelineLayout = std::move(result.layout);
    g_descriptorSetLayout = result.descriptorSetLayout;
}

// ============================================================================
// Section 10: Framebuffers, CommandPool, CommandBuffers
// ============================================================================


void createCommandPoolAndBuffers() {
    g_commandPool = std::make_unique<kazu::CommandPool>(*g_ctx, g_ctx->graphicsFamily());
    g_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        g_commandBuffers[i] = std::make_unique<kazu::CommandBuffer>(*g_ctx, g_commandPool->handle());
    }
}

void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = g_renderPass->handle();
    renderPassInfo.framebuffer = g_swapchain->framebuffer(imageIndex);
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = g_swapchain->extent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_graphicsPipeline->handle());

    // Dynamic viewport & scissor (pipeline created with VK_DYNAMIC_STATE_VIEWPORT/SCISSOR)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(g_swapchain->extent().width);
    viewport.height = static_cast<float>(g_swapchain->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = g_swapchain->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Compute view/proj from scene camera config
    const auto& cfg = g_scene->config();
    glm::mat4 view = glm::lookAt(cfg.cameraEye, cfg.cameraTarget, cfg.cameraUp);
    float aspect = static_cast<float>(g_swapchain->extent().width) / g_swapchain->extent().height;
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    proj[1][1] *= -1.0f; // Vulkan Y-flip
    glm::mat4 mvp = proj * view;

    // Push constants: mat4(64) + vec4(16) + vec4(16) + int(4) + pad(12) = 112 bytes
    struct PushData {
        glm::mat4 mvp;
        glm::vec4 lightPos;
        glm::vec4 viewPos;
        int displayMode;
        int _pad[3];
    } push;
    push.mvp = mvp;
    push.lightPos = glm::vec4(cfg.lightPos, 0.0f);
    push.viewPos = glm::vec4(cfg.cameraEye, 0.0f);
    push.displayMode = g_displayMode;
    vkCmdPushConstants(cmd, g_pipelineLayout->handle(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushData), &push);

    g_scene->draw(cmd, g_pipelineLayout->handle());
    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// Section 11: Sync Objects
// ============================================================================

void createSyncObjects() {
    g_syncObjects = std::make_unique<kazu::SyncObjects>(*g_ctx, MAX_FRAMES_IN_FLIGHT, g_swapchain->imageCount());
}

// ============================================================================
// Section 12: Main Loop
// ============================================================================

void recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    g_swapchain->recreate(g_renderPass->handle());

    // Recreate per-swapchain-image semaphores if image count changed
    g_syncObjects->recreateImageRenderFinished(g_swapchain->imageCount());
}

void drawFrame() {
    g_syncObjects->waitFence(currentFrame);

    uint32_t imageIndex = 0;
    VkResult result = g_swapchain->acquireNextImage(g_syncObjects->imageAvailable(currentFrame), imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        kazu::fatalError("Failed to acquire swap chain image!");
    }

    // Check if resize was requested via callback while waiting for fence
    if (g_framebufferResized) {
        g_framebufferResized = false;
        recreateSwapchain();
        return;
    }

    g_syncObjects->resetFence(currentFrame);
    g_commandBuffers[currentFrame]->reset();
    g_commandBuffers[currentFrame]->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    recordCommandBuffer(g_commandBuffers[currentFrame]->handle(), imageIndex);
    g_commandBuffers[currentFrame]->end();

    VkSemaphore renderFinishedSemaphore = g_syncObjects->imageRenderFinished(imageIndex);
    g_commandBuffers[currentFrame]->submit(
        g_ctx->graphicsQueue(),
        g_syncObjects->imageAvailable(currentFrame),
        renderFinishedSemaphore,
        g_syncObjects->inFlight(currentFrame));

    result = g_swapchain->presentImage(imageIndex, renderFinishedSemaphore);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        g_framebufferResized = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        kazu::fatalError("Failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// Section 13: Initialization & Cleanup
// ============================================================================

void initVulkan() {
    g_ctx = std::make_unique<kazu::Context>("KazuEngine", true);
    g_swapchain = std::make_unique<kazu::Swapchain>(*g_ctx, window, VK_NULL_HANDLE);
    g_shaderLibrary = std::make_unique<kazu::ShaderLibrary>(*g_ctx);
    g_descriptorSetLayoutCache = std::make_unique<kazu::DescriptorSetLayoutCache>(*g_ctx);
    g_pipelineCache = std::make_unique<kazu::PipelineCache>(*g_ctx);
    createRenderPass();
    g_swapchain->createFramebuffers(g_renderPass->handle());
    createGraphicsPipeline();
    g_shaderLibrary->logReflections();

    // 3.2: DescriptorSetLayoutCache validation
    VkDescriptorSetLayoutBinding testBinding{};
    testBinding.binding = 0;
    testBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    testBinding.descriptorCount = 1;
    testBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto h1 = g_descriptorSetLayoutCache->getOrCreate({testBinding});
    auto h2 = g_descriptorSetLayoutCache->getOrCreate({testBinding});
    if (h1 == h2 && g_descriptorSetLayoutCache->cacheSize() == 1) {
        spdlog::info("[DescriptorSetLayoutCache] Verified: duplicate bindings return same handle");
    }

    createCommandPoolAndBuffers();
    createSyncObjects();

    g_scene = std::make_unique<kazu::Scene>();
    g_scene->loadFromFile(*g_ctx, *g_shaderLibrary, *g_descriptorSetLayoutCache, "assets/scenes/sample-scene.json");
}

void cleanup() {
    if (!g_ctx) return;

    vkDeviceWaitIdle(g_ctx->device());

    g_syncObjects.reset();

    g_commandBuffers.clear();
    g_commandPool.reset();

    g_scene.reset();

    g_graphicsPipeline = nullptr;            // owned by PipelineCache
    g_shaderLibrary.reset();
    g_pipelineLayout.reset();
    g_pipelineCache.reset();                  // destroys cached GraphicsPipelines
    g_descriptorSetLayout = VK_NULL_HANDLE;   // owned by DescriptorSetLayoutCache
    g_descriptorSetLayoutCache.reset();       // destroys cached DescriptorSetLayouts
    g_renderPass.reset();
    g_swapchain.reset();
    // Context destructor handles device/instance/debugMessenger cleanup
    g_ctx.reset();
}

// ============================================================================
// Section 14: Entry Point
// ============================================================================

int main() {
    kazu::initLogger();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "KazuEngine - Mesh (Marry)", nullptr, nullptr);
    if (!window) {
        kazu::fatalError("Failed to create GLFW window!");
    }
    glfwSetKeyCallback(window, keyCallback);

    try {
        initVulkan();
        spdlog::info("Vulkan initialized successfully. Close window to exit.");

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        cleanup();
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
