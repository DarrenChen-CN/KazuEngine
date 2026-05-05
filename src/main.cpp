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
#include "core/ShaderModule.h"
#include "core/RenderPass.h"
#include "core/PipelineLayout.h"
#include "core/GraphicsPipeline.h"

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
std::unique_ptr<kazu::GraphicsPipeline> g_graphicsPipeline;
VkCommandPool commandPool = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> commandBuffers;
std::vector<VkSemaphore> imageAvailableSemaphores;
std::vector<VkSemaphore> renderFinishedSemaphores;
std::vector<VkFence> inFlightFences;
std::vector<VkSemaphore> imageRenderFinishedSemaphores; // per swapchain image
uint32_t currentFrame = 0;
bool g_framebufferResized = false;

// Vertex data for triangle MVP
struct Vertex {
    float pos[2];
    float color[3];
};

const std::vector<Vertex> g_vertices = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

std::unique_ptr<kazu::Buffer> g_vertexBuffer;

// ============================================================================
// Section 2: Utility Functions
// ============================================================================

std::vector<char> readFile(const std::string& filename) {
    FILE* file = nullptr;
    fopen_s(&file, filename.c_str(), "rb");
    if (!file) {
        kazu::fatalError("Failed to open file: " + filename);
    }
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::vector<char> buffer(size);
    fread(buffer.data(), 1, size, file);
    fclose(file);
    return buffer;
}

// ============================================================================
// Section 3: Queue Family / Surface / Swapchain (moved to Context/Swapchain classes)
// ============================================================================

// ============================================================================
// Section 9: RenderPass & Pipeline
// ============================================================================

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

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    g_renderPass = std::make_unique<kazu::RenderPass>(*g_ctx, renderPassInfo);
}

void createGraphicsPipeline() {
    auto vertCode = readFile("shaders/triangle.vert.spv");
    auto fragCode = readFile("shaders/triangle.frag.spv");

    kazu::ShaderModule vertModule(*g_ctx, vertCode);
    kazu::ShaderModule fragModule(*g_ctx, fragCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule.handle();
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule.handle();
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    // Vertex Input: binding + attributes
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(g_swapchain->extent().width);
    viewport.height = static_cast<float>(g_swapchain->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = g_swapchain->extent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    g_pipelineLayout = std::make_unique<kazu::PipelineLayout>(*g_ctx, pipelineLayoutInfo);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = g_pipelineLayout->handle();
    pipelineInfo.renderPass = g_renderPass->handle();
    pipelineInfo.subpass = 0;

    g_graphicsPipeline = std::make_unique<kazu::GraphicsPipeline>(*g_ctx, pipelineInfo);

    // ShaderModules / PipelineLayout destroyed automatically by RAII when function returns
}

// ============================================================================
// Section 10: Framebuffers, CommandPool, CommandBuffers
// ============================================================================


void createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = g_ctx->graphicsFamily();

    if (vkCreateCommandPool(g_ctx->device(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        kazu::fatalError("Failed to create command pool!");
    }
}

void createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(g_ctx->device(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        kazu::fatalError("Failed to allocate command buffers!");
    }
}

void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = g_renderPass->handle();
    renderPassInfo.framebuffer = g_swapchain->framebuffer(imageIndex);
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = g_swapchain->extent();

    VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_graphicsPipeline->handle());

    VkBuffer vertexBuffers[] = { g_vertexBuffer->handle() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(cmd, static_cast<uint32_t>(g_vertices.size()), 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);
}

// ============================================================================
// Section 11: Sync Objects
// ============================================================================

void createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(g_ctx->device(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(g_ctx->device(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(g_ctx->device(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            kazu::fatalError("Failed to create sync objects!");
        }
    }

    // Create per-swapchain-image render finished semaphores to avoid reuse conflict
    imageRenderFinishedSemaphores.resize(g_swapchain->imageCount());
    for (size_t i = 0; i < g_swapchain->imageCount(); ++i) {
        if (vkCreateSemaphore(g_ctx->device(), &semaphoreInfo, nullptr, &imageRenderFinishedSemaphores[i]) != VK_SUCCESS) {
            kazu::fatalError("Failed to create image render finished semaphore!");
        }
    }
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
    uint32_t newImageCount = g_swapchain->imageCount();
    if (newImageCount != imageRenderFinishedSemaphores.size()) {
        for (auto sem : imageRenderFinishedSemaphores) {
            vkDestroySemaphore(g_ctx->device(), sem, nullptr);
        }
        imageRenderFinishedSemaphores.clear();
        imageRenderFinishedSemaphores.resize(newImageCount);
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (size_t i = 0; i < newImageCount; ++i) {
            VK_CHECK(vkCreateSemaphore(g_ctx->device(), &semaphoreInfo, nullptr, &imageRenderFinishedSemaphores[i]));
        }
        spdlog::info("Image render finished semaphores recreated: {} -> {}",
                     imageRenderFinishedSemaphores.size(), newImageCount);
    }
}

void drawFrame() {
    vkWaitForFences(g_ctx->device(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = g_swapchain->acquireNextImage(imageAvailableSemaphores[currentFrame], imageIndex);

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

    vkResetFences(g_ctx->device(), 1, &inFlightFences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore renderFinishedSemaphore = imageRenderFinishedSemaphores[imageIndex];
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_CHECK(vkQueueSubmit(g_ctx->graphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchainHandle = g_swapchain->handle();
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(g_ctx->presentQueue(), &presentInfo);
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

void createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(g_vertices[0]) * g_vertices.size();
    g_vertexBuffer = std::make_unique<kazu::Buffer>(
        *g_ctx, bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    g_vertexBuffer->upload(g_vertices.data(), bufferSize);
}

void initVulkan() {
    g_ctx = std::make_unique<kazu::Context>("KazuEngine", true);
    g_swapchain = std::make_unique<kazu::Swapchain>(*g_ctx, window, VK_NULL_HANDLE);
    createRenderPass();
    g_swapchain->createFramebuffers(g_renderPass->handle());
    createGraphicsPipeline();
    createVertexBuffer();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

void cleanup() {
    if (!g_ctx) return;

    vkDeviceWaitIdle(g_ctx->device());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyFence(g_ctx->device(), inFlightFences[i], nullptr);
        vkDestroySemaphore(g_ctx->device(), renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(g_ctx->device(), imageAvailableSemaphores[i], nullptr);
    }
    for (auto sem : imageRenderFinishedSemaphores) {
        vkDestroySemaphore(g_ctx->device(), sem, nullptr);
    }

    vkDestroyCommandPool(g_ctx->device(), commandPool, nullptr);

    g_vertexBuffer.reset();

    g_graphicsPipeline.reset();
    g_pipelineLayout.reset();
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

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "KazuEngine - Triangle MVP", nullptr, nullptr);
    if (!window) {
        kazu::fatalError("Failed to create GLFW window!");
    }

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
