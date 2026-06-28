// ============================================================================
// KazuEngine - Core Layer: GPU Timer
//
// Lightweight Vulkan timestamp-query helper for per-pass GPU time.
// Uses a ring of (begin, end) timestamp pairs. Results are read back
// without CPU stalls via VK_QUERY_RESULT_WITH_AVAILABILITY_BIT.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace kazu {

class GPUTimer {
public:
    GPUTimer(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t ringSize = 2);
    ~GPUTimer();

    GPUTimer(const GPUTimer&) = delete;
    GPUTimer& operator=(const GPUTimer&) = delete;
    GPUTimer(GPUTimer&&) = delete;
    GPUTimer& operator=(GPUTimer&&) = delete;

    bool supported() const { return m_supported; }

    // Reset the timestamp pair for the given ring slot before recording.
    void resetSlot(VkCommandBuffer cmd, uint32_t slot);

    // Record timestamps. Use TOP_OF_PIPE / BOTTOM_OF_PIPE to capture the
    // full pass execution time.
    void begin(VkCommandBuffer cmd, uint32_t slot);
    void end(VkCommandBuffer cmd, uint32_t slot);

    // Try to read back the elapsed time for a slot. Returns false if the
    // results are not yet available (no CPU wait).
    bool fetchMs(uint32_t slot, float& outMs);

    // Rolling average over all currently valid slots.
    float averageMs() const;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    uint32_t m_ringSize = 2;
    bool m_supported = false;
    float m_period = 0.0f; // seconds per timestamp tick

    std::vector<bool> m_valid;
    std::vector<float> m_elapsedMs;
};

} // namespace kazu
