// ============================================================================
// KazuEngine - Core Layer: SyncObjects (Implementation)
// ============================================================================

#include "SyncObjects.h"
#include "Utils.h"

namespace kazu {

SyncObjects::SyncObjects(Context& ctx, uint32_t frameCount, uint32_t imageCount)
    : m_ctx(&ctx)
{
    m_imageAvailable.resize(frameCount);
    m_inFlight.resize(frameCount);
    m_imageRenderFinished.resize(imageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < frameCount; ++i) {
        VK_CHECK(vkCreateSemaphore(m_ctx->device(), &semaphoreInfo, nullptr, &m_imageAvailable[i]));
        VK_CHECK(vkCreateFence(m_ctx->device(), &fenceInfo, nullptr, &m_inFlight[i]));
    }

    for (uint32_t i = 0; i < imageCount; ++i) {
        VK_CHECK(vkCreateSemaphore(m_ctx->device(), &semaphoreInfo, nullptr, &m_imageRenderFinished[i]));
    }
}

SyncObjects::~SyncObjects() {
    if (!m_ctx) return;

    for (auto& sem : m_imageRenderFinished) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(m_ctx->device(), sem, nullptr);
    }
    for (auto& fence : m_inFlight) {
        if (fence != VK_NULL_HANDLE) vkDestroyFence(m_ctx->device(), fence, nullptr);
    }
    for (auto& sem : m_imageAvailable) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(m_ctx->device(), sem, nullptr);
    }
}

SyncObjects::SyncObjects(SyncObjects&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_imageAvailable(std::move(other.m_imageAvailable))
    , m_inFlight(std::move(other.m_inFlight))
    , m_imageRenderFinished(std::move(other.m_imageRenderFinished))
{}

SyncObjects& SyncObjects::operator=(SyncObjects&& other) noexcept {
    if (this != &other) {
        this->~SyncObjects();
        m_ctx = other.m_ctx;
        m_imageAvailable = std::move(other.m_imageAvailable);
        m_inFlight = std::move(other.m_inFlight);
        m_imageRenderFinished = std::move(other.m_imageRenderFinished);
    }
    return *this;
}

VkSemaphore SyncObjects::imageAvailable(uint32_t frame) const {
    return m_imageAvailable[frame];
}

VkFence SyncObjects::inFlight(uint32_t frame) const {
    return m_inFlight[frame];
}

VkSemaphore SyncObjects::imageRenderFinished(uint32_t imageIndex) const {
    return m_imageRenderFinished[imageIndex];
}

void SyncObjects::waitFence(uint32_t frame) {
    VK_CHECK(vkWaitForFences(m_ctx->device(), 1, &m_inFlight[frame], VK_TRUE, UINT64_MAX));
}

void SyncObjects::resetFence(uint32_t frame) {
    VK_CHECK(vkResetFences(m_ctx->device(), 1, &m_inFlight[frame]));
}

void SyncObjects::recreateImageRenderFinished(uint32_t newImageCount) {
    if (newImageCount == m_imageRenderFinished.size()) return;

    for (auto& sem : m_imageRenderFinished) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(m_ctx->device(), sem, nullptr);
    }
    m_imageRenderFinished.clear();
    m_imageRenderFinished.resize(newImageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < newImageCount; ++i) {
        VK_CHECK(vkCreateSemaphore(m_ctx->device(), &semaphoreInfo, nullptr, &m_imageRenderFinished[i]));
    }
}

} // namespace kazu
