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
#include "core/Image.h"
#include "core/DescriptorPool.h"
#include "core/DescriptorSetLayout.h"
#include "core/Sampler.h"

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
int g_displayMode = 0;  // 0 = lighting, 1 = albedo, 2 = normal

// GBuffer resources
std::unique_ptr<kazu::Image> g_gbufferAlbedo;
std::unique_ptr<kazu::Image> g_gbufferNormal;
std::unique_ptr<kazu::Image> g_gbufferMaterial;
std::unique_ptr<kazu::Image> g_gbufferDepth;
VkRenderPass   g_gbufferRenderPass   = VK_NULL_HANDLE;
VkFramebuffer  g_gbufferFramebuffer  = VK_NULL_HANDLE;
VkPipeline     g_gbufferPipeline     = VK_NULL_HANDLE;
VkPipelineLayout g_gbufferPipelineLayout = VK_NULL_HANDLE;

// Lighting resources
VkPipeline     g_lightingPipeline     = VK_NULL_HANDLE;
VkPipelineLayout g_lightingPipelineLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout g_lightingDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSet  g_lightingDescriptorSet = VK_NULL_HANDLE;
VkDescriptorPool g_lightingDescriptorPool = VK_NULL_HANDLE;
VkSampler        g_lightingSampler = VK_NULL_HANDLE;

// RenderGraph
std::unique_ptr<kazu::RenderGraph> g_renderGraph;

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

// Current frame's swapchain image index, used by Lighting Pass execute lambda
uint32_t g_currentImageIndex = 0;

struct GBufferPush {
    glm::mat4 mvp;
    glm::vec4 lightPos;
    glm::vec4 viewPos;
    int displayMode;
    int _pad[3];
};

struct LightingPush {
    glm::vec4 lightPos;
    glm::vec4 viewPos;
    int displayMode;
};

void recordFrame(uint32_t imageIndex) {
    VkCommandBuffer cmd = g_rhi->currentCmd();
    g_currentImageIndex = imageIndex;
    g_renderGraph->execute(cmd);
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

    // --- 4.3b Deferred Shading: GBuffer + Lighting ---
    g_renderGraph = std::make_unique<kazu::RenderGraph>(g_rhi->ctx());

    auto albedoHandle = g_renderGraph->addTexture("Albedo",
        {WINDOW_WIDTH, WINDOW_HEIGHT, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    auto normalHandle = g_renderGraph->addTexture("Normal",
        {WINDOW_WIDTH, WINDOW_HEIGHT, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    auto materialHandle = g_renderGraph->addTexture("Material",
        {WINDOW_WIDTH, WINDOW_HEIGHT, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    auto depthHandle = g_renderGraph->addTexture("Depth",
        {WINDOW_WIDTH, WINDOW_HEIGHT, VK_FORMAT_D32_SFLOAT,
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});

    // Passes will be added after compile so execute lambdas can capture handles
    // For now we add placeholder passes to drive compilation & barrier derivation
    g_renderGraph->addPass("GBuffer", [&](kazu::RenderGraph::PassBuilder& b) {
        b.writeColor(0, albedoHandle);
        b.writeColor(1, normalHandle);
        b.writeColor(2, materialHandle);
        b.writeDepth(depthHandle);
        b.execute = [](VkCommandBuffer cmd) {
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = g_gbufferRenderPass;
            rpInfo.framebuffer = g_gbufferFramebuffer;
            rpInfo.renderArea.offset = {0, 0};
            rpInfo.renderArea.extent = {WINDOW_WIDTH, WINDOW_HEIGHT};
            std::array<VkClearValue, 4> clears{};
            clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clears[1].color = {{0.5f, 0.5f, 1.0f, 1.0f}};
            clears[2].color = {{0.0f, 0.5f, 1.0f, 1.0f}};
            clears[3].depthStencil = {1.0f, 0};
            rpInfo.clearValueCount = 4;
            rpInfo.pClearValues = clears.data();
            vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_gbufferPipeline);

            VkViewport viewport{};
            viewport.width = static_cast<float>(WINDOW_WIDTH);
            viewport.height = static_cast<float>(WINDOW_HEIGHT);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = {WINDOW_WIDTH, WINDOW_HEIGHT};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            GBufferPush push{};
            push.mvp = g_camera->getProjectionMatrix(g_rhi->aspect()) * g_camera->getViewMatrix();
            push.lightPos = glm::vec4(g_scene->config().lightPos, 0.0f);
            push.viewPos = glm::vec4(g_camera->position(), 0.0f);
            push.displayMode = 0;
            vkCmdPushConstants(cmd, g_gbufferPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(GBufferPush), &push);

            g_scene->draw(cmd, g_gbufferPipelineLayout);
            vkCmdEndRenderPass(cmd);
        };
    });
    g_renderGraph->addPass("Lighting", [&](kazu::RenderGraph::PassBuilder& b) {
        b.read(albedoHandle);
        b.read(normalHandle);
        b.execute = [](VkCommandBuffer cmd) {
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = g_rhi->renderPass();
            rpInfo.framebuffer = g_rhi->framebuffer(g_currentImageIndex);
            rpInfo.renderArea.offset = {0, 0};
            rpInfo.renderArea.extent = g_rhi->extent();
            std::array<VkClearValue, 2> clears{};
            clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clears[1].depthStencil = {1.0f, 0};
            rpInfo.clearValueCount = 2;
            rpInfo.pClearValues = clears.data();
            vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_lightingPipeline);

            VkViewport viewport{};
            viewport.width = static_cast<float>(g_rhi->extent().width);
            viewport.height = static_cast<float>(g_rhi->extent().height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = g_rhi->extent();
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                g_lightingPipelineLayout, 0, 1, &g_lightingDescriptorSet, 0, nullptr);

            LightingPush push{};
            push.lightPos = glm::vec4(g_scene->config().lightPos, 0.0f);
            push.viewPos = glm::vec4(g_camera->position(), 0.0f);
            push.displayMode = g_displayMode;
            vkCmdPushConstants(cmd, g_lightingPipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(LightingPush), &push);

            vkCmdDraw(cmd, 4, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
        };
    });

    if (!g_renderGraph->compile()) {
        kazu::fatalError("RenderGraph compile failed");
    }

    // Retrieve allocated transient images
    VkImageView albedoView   = g_renderGraph->getImageView(albedoHandle);
    VkImageView normalView   = g_renderGraph->getImageView(normalHandle);
    VkImageView materialView = g_renderGraph->getImageView(materialHandle);
    VkImageView depthView    = g_renderGraph->getImageView(depthHandle);

    // ---- GBuffer RenderPass & Framebuffer ----
    {
        VkAttachmentDescription attachments[4]{};
        for (int i = 0; i < 3; ++i) {
            attachments[i].format = VK_FORMAT_R8G8B8A8_UNORM;
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        attachments[3].format = VK_FORMAT_D32_SFLOAT;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
        VK_CHECK(vkCreateRenderPass(g_rhi->ctx().device(), &rpInfo, nullptr, &g_gbufferRenderPass));

        VkImageView fbViews[4] = {albedoView, normalView, materialView, depthView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = g_gbufferRenderPass;
        fbInfo.attachmentCount = 4;
        fbInfo.pAttachments = fbViews;
        fbInfo.width = WINDOW_WIDTH;
        fbInfo.height = WINDOW_HEIGHT;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(g_rhi->ctx().device(), &fbInfo, nullptr, &g_gbufferFramebuffer));
    }

    // ---- GBuffer Pipeline ----
    {
        static kazu::PipelineCache s_gbufferCache(g_rhi->ctx());
        kazu::PipelineBuilder builder(g_rhi->ctx(), g_rhi->shaderLib(), g_rhi->dslCache());
        builder.shader("shaders/gbuffer.frag.spv")
               .shader("shaders/triangle.vert.spv")
               .renderPass(g_gbufferRenderPass);
        auto result = builder.build(s_gbufferCache);
        g_gbufferPipeline = result.pipeline->handle();
        g_gbufferPipelineLayout = result.layout->handle();
        (void)result.layout.release();
    }

    // ---- Lighting Pipeline ----
    {
        static kazu::PipelineCache s_lightingCache(g_rhi->ctx());
        kazu::PipelineBuilder builder(g_rhi->ctx(), g_rhi->shaderLib(), g_rhi->dslCache());
        builder.shader("shaders/lighting.frag.spv")
               .shader("shaders/lighting.vert.spv")
               .renderPass(g_rhi->renderPass());
        auto result = builder.build(s_lightingCache);
        g_lightingPipeline = result.pipeline->handle();
        g_lightingPipelineLayout = result.layout->handle();
        (void)result.layout.release();
    }

    // ---- Lighting Descriptor Set (Albedo + Normal samplers) ----
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(g_rhi->ctx().device(), &samplerInfo, nullptr, &g_lightingSampler));

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].pImmutableSamplers = nullptr;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = 2;
        dslInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(g_rhi->ctx().device(), &dslInfo, nullptr, &g_lightingDescriptorSetLayout));

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 2;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(g_rhi->ctx().device(), &poolInfo, nullptr, &g_lightingDescriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = g_lightingDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &g_lightingDescriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(g_rhi->ctx().device(), &allocInfo, &g_lightingDescriptorSet));

        VkDescriptorImageInfo imageInfos[2]{};
        imageInfos[0].sampler = g_lightingSampler;
        imageInfos[0].imageView = albedoView;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].sampler = g_lightingSampler;
        imageInfos[1].imageView = normalView;
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2]{};
        for (int i = 0; i < 2; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = g_lightingDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(g_rhi->ctx().device(), 2, writes, 0, nullptr);
    }

    // ---- Real execute lambdas (reference global handles, compiled once) ----
    // Note: execute lambdas capture nothing by value; they read globals at execution time
    spdlog::info("Deferred Shading pipeline initialized (GBuffer + Lighting)");
    // ---
}

void cleanupApp() {
    if (g_rhi) {
        vkDeviceWaitIdle(g_rhi->ctx().device());
    }

    if (g_lightingDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(g_rhi->ctx().device(), g_lightingDescriptorPool, nullptr);
    }
    if (g_lightingDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(g_rhi->ctx().device(), g_lightingDescriptorSetLayout, nullptr);
    }
    if (g_lightingSampler != VK_NULL_HANDLE) {
        vkDestroySampler(g_rhi->ctx().device(), g_lightingSampler, nullptr);
    }
    if (g_gbufferFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(g_rhi->ctx().device(), g_gbufferFramebuffer, nullptr);
    }
    if (g_gbufferRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(g_rhi->ctx().device(), g_gbufferRenderPass, nullptr);
    }

    g_renderGraph.reset();
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
