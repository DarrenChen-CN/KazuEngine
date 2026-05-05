// ============================================================================
// KazuEngine - Core Layer: SyncObjects
//
// RAII wrapper for per-frame synchronization primitives (Semaphore/Fence)
// plus per-swapchain-image semaphores.
// ============================================================================

#pragma once

#include "Context.h"
#include <vector>

namespace kazu {

class SyncObjects {
public:
    SyncObjects(Context& ctx, uint32_t frameCount, uint32_t imageCount);
    ~SyncObjects();

    SyncObjects(const SyncObjects&) = delete;
    SyncObjects& operator=(const SyncObjects&) = delete;
    SyncObjects(SyncObjects&& other) noexcept;
    SyncObjects& operator=(SyncObjects&& other) noexcept;

    VkSemaphore imageAvailable(uint32_t frame) const;
    VkSemaphore renderFinished(uint32_t frame) const;
    VkFence inFlight(uint32_t frame) const;
    VkSemaphore imageRenderFinished(uint32_t imageIndex) const;

    void waitFence(uint32_t frame);
    void resetFence(uint32_t frame);
    void recreateImageRenderFinished(uint32_t newImageCount);

private:
    Context* m_ctx = nullptr;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlight;
    std::vector<VkSemaphore> m_imageRenderFinished;
};

} // namespace kazu
