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
#include "core/DescriptorSetLayout.h"
#include "core/DescriptorPool.h"
#include "core/Image.h"
#include "core/CommandPool.h"
#include "core/CommandBuffer.h"
#include "core/SyncObjects.h"

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
std::unique_ptr<kazu::GraphicsPipeline> g_graphicsPipeline;
std::unique_ptr<kazu::CommandPool> g_commandPool;
std::vector<std::unique_ptr<kazu::CommandBuffer>> g_commandBuffers;
std::unique_ptr<kazu::SyncObjects> g_syncObjects;
uint32_t currentFrame = 0;
bool g_framebufferResized = false;

// Vertex data for triangle MVP
struct Vertex {
    float pos[2];
    float color[3];
    float texCoord[2];
};

const std::vector<Vertex> g_vertices = {
    {{-1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
    {{ 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-1.0f,  1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}}
};

std::unique_ptr<kazu::Buffer> g_vertexBuffer;
std::unique_ptr<kazu::Image> g_textureImage;
VkSampler g_textureSampler = VK_NULL_HANDLE;
std::unique_ptr<kazu::DescriptorSetLayout> g_descriptorSetLayout;
std::unique_ptr<kazu::DescriptorPool> g_descriptorPool;
VkDescriptorSet g_descriptorSet = VK_NULL_HANDLE;

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

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

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

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = 1;
    descriptorSetLayoutInfo.pBindings = &samplerLayoutBinding;
    g_descriptorSetLayout = std::make_unique<kazu::DescriptorSetLayout>(*g_ctx, descriptorSetLayoutInfo);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout dslHandle = g_descriptorSetLayout->handle();
    pipelineLayoutInfo.pSetLayouts = &dslHandle;
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


void createCommandPoolAndBuffers() {
    g_commandPool = std::make_unique<kazu::CommandPool>(*g_ctx, g_ctx->graphicsFamily());
    g_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        g_commandBuffers[i] = std::make_unique<kazu::CommandBuffer>(*g_ctx, g_commandPool->handle());
    }
}

void createTextureImage() {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load("container.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        kazu::fatalError("Failed to load texture image!");
    }
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    kazu::Buffer stagingBuffer(*g_ctx, imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.upload(pixels, imageSize);
    stbi_image_free(pixels);

    g_textureImage = std::make_unique<kazu::Image>(*g_ctx,
        static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight),
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // One-time command buffer for layout transition + copy
    kazu::CommandBuffer cmd(*g_ctx, g_commandPool->handle());
    cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    g_textureImage->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};
    vkCmdCopyBufferToImage(cmd.handle(), stagingBuffer.handle(), g_textureImage->handle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    g_textureImage->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    cmd.end();
    cmd.submit(g_ctx->graphicsQueue());
    vkQueueWaitIdle(g_ctx->graphicsQueue());

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(g_ctx->device(), &samplerInfo, nullptr, &g_textureSampler));
}

void createDescriptorSet() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    g_descriptorPool = std::make_unique<kazu::DescriptorPool>(*g_ctx, poolInfo);

    g_descriptorSet = g_descriptorPool->allocate(g_descriptorSetLayout->handle());

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = g_textureImage->view();
    imageInfo.sampler = g_textureSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = g_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(g_ctx->device(), 1, &descriptorWrite, 0, nullptr);
}

void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
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
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipelineLayout->handle(),
                            0, 1, &g_descriptorSet, 0, nullptr);
    vkCmdDraw(cmd, static_cast<uint32_t>(g_vertices.size()), 1, 0, 0);
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

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
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
    createCommandPoolAndBuffers();
    createSyncObjects();
    createTextureImage();
    createDescriptorSet();
}

void cleanup() {
    if (!g_ctx) return;

    vkDeviceWaitIdle(g_ctx->device());

    g_syncObjects.reset();

    g_commandBuffers.clear();
    g_commandPool.reset();

    if (g_textureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(g_ctx->device(), g_textureSampler, nullptr);
        g_textureSampler = VK_NULL_HANDLE;
    }
    g_textureImage.reset();
    g_descriptorPool.reset();
    g_vertexBuffer.reset();

    g_graphicsPipeline.reset();
    g_pipelineLayout.reset();
    g_descriptorSetLayout.reset();
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
