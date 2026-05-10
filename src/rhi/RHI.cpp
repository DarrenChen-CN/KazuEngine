// ============================================================================
// KazuEngine - RHI: RHI Context (implementation)
// ============================================================================

#include "RHI.h"
#include "../core/Context.h"
#include "../core/Swapchain.h"
#include "../core/RenderPass.h"
#include "../core/PipelineLayout.h"
#include "../core/GraphicsPipeline.h"
#include "../core/CommandPool.h"
#include "../core/CommandBuffer.h"
#include "../core/SyncObjects.h"
#include "ShaderLibrary.h"
#include "PipelineBuilder.h"
#include "PipelineCache.h"
#include "DescriptorSetLayoutCache.h"
#include "../core/Utils.h"

#include <array>
#include <spdlog/spdlog.h>

namespace kazu {

RHI::RHI() = default;
RHI::~RHI() { cleanup(); }

bool RHI::init(GLFWwindow* window) {
    m_window = window;
    m_ctx = std::make_unique<Context>("KazuEngine", true);
    m_swapchain = std::make_unique<Swapchain>(*m_ctx, window, VK_NULL_HANDLE);
    m_shaderLibrary = std::make_unique<ShaderLibrary>(*m_ctx);
    m_descriptorSetLayoutCache = std::make_unique<DescriptorSetLayoutCache>(*m_ctx);
    m_pipelineCache = std::make_unique<PipelineCache>(*m_ctx);

    createRenderPass();
    m_swapchain->createFramebuffers(m_renderPass->handle());
    createGraphicsPipeline();
    m_shaderLibrary->logReflections();

    // Verify DescriptorSetLayoutCache deduplication
    VkDescriptorSetLayoutBinding testBinding{};
    testBinding.binding = 0;
    testBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    testBinding.descriptorCount = 1;
    testBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto h1 = m_descriptorSetLayoutCache->getOrCreate({testBinding});
    auto h2 = m_descriptorSetLayoutCache->getOrCreate({testBinding});
    if (h1 == h2 && m_descriptorSetLayoutCache->cacheSize() == 1) {
        spdlog::info("[DescriptorSetLayoutCache] Verified: deduplication works");
    }

    createCommandPoolAndBuffers();
    createSyncObjects();
    return true;
}

void RHI::cleanup() {
    if (!m_ctx) return;
    vkDeviceWaitIdle(m_ctx->device());

    m_syncObjects.reset();
    m_commandBuffers.clear();
    m_commandPool.reset();

    m_graphicsPipeline = nullptr;
    m_shaderLibrary.reset();
    m_pipelineLayout.reset();
    m_pipelineCache.reset();
    m_descriptorSetLayout = VK_NULL_HANDLE;
    m_descriptorSetLayoutCache.reset();
    m_renderPass.reset();
    m_swapchain.reset();
    m_ctx.reset();
    m_window = nullptr;
}

void RHI::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchain->format();
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

    m_renderPass = std::make_unique<RenderPass>(*m_ctx, renderPassInfo);
}

void RHI::createGraphicsPipeline() {
    PipelineBuilder builder(*m_ctx, *m_shaderLibrary, *m_descriptorSetLayoutCache);
    builder.shader("shaders/triangle.vert.spv")
           .shader("shaders/triangle.frag.spv")
           .renderPass(m_renderPass->handle())
           .frontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    auto result = builder.build(*m_pipelineCache);

    m_graphicsPipeline = result.pipeline;
    m_pipelineLayout = std::move(result.layout);
    m_descriptorSetLayout = result.descriptorSetLayout;
}

void RHI::createCommandPoolAndBuffers() {
    m_commandPool = std::make_unique<CommandPool>(*m_ctx, m_ctx->graphicsFamily());
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_commandBuffers[i] = std::make_unique<CommandBuffer>(*m_ctx, m_commandPool->handle());
    }
}

void RHI::createSyncObjects() {
    m_syncObjects = std::make_unique<SyncObjects>(*m_ctx, MAX_FRAMES_IN_FLIGHT, m_swapchain->imageCount());
}

bool RHI::beginFrame(uint32_t& imageIndex) {
    m_syncObjects->waitFence(m_currentFrame);

    VkResult result = m_swapchain->acquireNextImage(
        m_syncObjects->imageAvailable(m_currentFrame), imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        fatalError("Failed to acquire swap chain image!");
    }

    if (m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapchain();
        return false;
    }

    m_syncObjects->resetFence(m_currentFrame);
    m_commandBuffers[m_currentFrame]->reset();
    m_commandBuffers[m_currentFrame]->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    return true;
}

void RHI::endFrame(uint32_t imageIndex) {
    m_commandBuffers[m_currentFrame]->end();

    VkSemaphore renderFinished = m_syncObjects->imageRenderFinished(imageIndex);
    m_commandBuffers[m_currentFrame]->submit(
        m_ctx->graphicsQueue(),
        m_syncObjects->imageAvailable(m_currentFrame),
        renderFinished,
        m_syncObjects->inFlight(m_currentFrame));

    VkResult result = m_swapchain->presentImage(imageIndex, renderFinished);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_framebufferResized = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        fatalError("Failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RHI::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    m_swapchain->recreate(m_renderPass->handle());
    m_syncObjects->recreateImageRenderFinished(m_swapchain->imageCount());
}

// Accessors
VkCommandBuffer RHI::currentCmd() const {
    return m_commandBuffers[m_currentFrame]->handle();
}
VkPipeline RHI::graphicsPipeline() const {
    return m_graphicsPipeline->handle();
}
VkPipelineLayout RHI::pipelineLayout() const {
    return m_pipelineLayout->handle();
}
VkRenderPass RHI::renderPass() const {
    return m_renderPass->handle();
}
VkFramebuffer RHI::framebuffer(uint32_t imageIndex) const {
    return m_swapchain->framebuffer(imageIndex);
}
VkExtent2D RHI::extent() const {
    return m_swapchain->extent();
}
float RHI::aspect() const {
    return static_cast<float>(m_swapchain->extent().width) / m_swapchain->extent().height;
}
ShaderLibrary& RHI::shaderLib() {
    return *m_shaderLibrary;
}
DescriptorSetLayoutCache& RHI::dslCache() {
    return *m_descriptorSetLayoutCache;
}
Context& RHI::ctx() {
    return *m_ctx;
}

} // namespace kazu
