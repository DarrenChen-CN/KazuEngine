// ============================================================================
// KazuEngine - RHI: RHI Context (implementation)
// ============================================================================

#include "RHI.h"
#include "../core/Path.h"
#include "../core/Context.h"
#include "../core/Swapchain.h"
#include "../core/CommandPool.h"
#include "../core/CommandBuffer.h"
#include "../core/SyncObjects.h"
#include "ShaderLibrary.h"
#include "ShaderEffect.h"
#include "PipelineCache.h"
#include "DescriptorSetLayoutCache.h"
#include "../core/Utils.h"

#include <array>
#include <spdlog/spdlog.h>

namespace kazu {

RHI::RHI() = default;
RHI::~RHI() { cleanup(); }

bool RHI::init(GLFWwindow* window, uint32_t uiPanelWidth) {
    m_window = window;
    m_uiPanelWidth = uiPanelWidth;
    m_ctx = std::make_unique<Context>("KazuEngine", true);
    m_swapchain = std::make_unique<Swapchain>(*m_ctx, window, VK_NULL_HANDLE);
    updateRenderExtent();
    m_shaderLibrary = std::make_unique<ShaderLibrary>(*m_ctx);
    m_descriptorSetLayoutCache = std::make_unique<DescriptorSetLayoutCache>(*m_ctx);
    m_pipelineCache = std::make_unique<PipelineCache>(*m_ctx);

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

    ShaderEffect::clearCache();
    m_shaderLibrary.reset();
    m_pipelineCache.reset();
    m_descriptorSetLayoutCache.reset();
    m_swapchain.reset();
    m_ctx.reset();
    m_window = nullptr;
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

    // Recreate before acquiring. If we acquire first and then skip submit,
    // the imageAvailable semaphore remains signaled and cannot be reused by
    // the next vkAcquireNextImageKHR call.
    if (m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapchain();
        return false;
    }

    VkResult result = m_swapchain->acquireNextImage(
        m_syncObjects->imageAvailable(m_currentFrame), imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        fatalError("Failed to acquire swap chain image!");
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

    m_swapchain->recreate(VK_NULL_HANDLE); // framebuffers managed by passes now
    updateRenderExtent();
    m_syncObjects->recreateImageRenderFinished(m_swapchain->imageCount());
}

void RHI::updateRenderExtent() {
    VkExtent2D swap = m_swapchain->extent();
    m_renderExtent.width = (swap.width > m_uiPanelWidth) ? (swap.width - m_uiPanelWidth) : 1;
    m_renderExtent.height = swap.height;
}

// Accessors
VkCommandBuffer RHI::currentCmd() const {
    return m_commandBuffers[m_currentFrame]->handle();
}
VkExtent2D RHI::extent() const {
    return m_renderExtent;
}
float RHI::aspect() const {
    return static_cast<float>(m_renderExtent.width) / static_cast<float>(m_renderExtent.height);
}
VkExtent2D RHI::swapchainExtent() const {
    return m_swapchain->extent();
}
VkImage RHI::swapchainImage(uint32_t imageIndex) const {
    return m_swapchain->image(imageIndex);
}
VkImageView RHI::swapchainImageView(uint32_t imageIndex) const {
    return m_swapchain->imageView(imageIndex);
}
uint32_t RHI::swapchainImageCount() const {
    return m_swapchain->imageCount();
}
VkFormat RHI::swapchainFormat() const {
    return m_swapchain->format();
}
ShaderLibrary& RHI::shaderLib() {
    return *m_shaderLibrary;
}
DescriptorSetLayoutCache& RHI::dslCache() {
    return *m_descriptorSetLayoutCache;
}
PipelineCache& RHI::pipelineCache() {
    return *m_pipelineCache;
}
Context& RHI::ctx() {
    return *m_ctx;
}

VkCommandPool RHI::commandPool() const {
    return m_commandPool->handle();
}

} // namespace kazu

