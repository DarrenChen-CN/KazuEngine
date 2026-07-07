// ============================================================================
// KazuEngine - Core Layer: GPU Timer (Implementation)
// ============================================================================

#include "core/GPUTimer.h"
#include "core/Utils.h"
#include <cstring>

namespace kazu {

GPUTimer::GPUTimer(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t ringSize)
    : m_device(device)
    , m_ringSize(ringSize)
    , m_valid(ringSize, false)
    , m_elapsedMs(ringSize, 0.0f)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);

    // timestampPeriod is in seconds per tick; 0.0f means unsupported on this queue.
    m_period = props.limits.timestampPeriod;
    m_supported = (props.limits.timestampComputeAndGraphics != VK_FALSE) && (m_period > 0.0f);

    if (!m_supported) {
        return;
    }

    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = ringSize * 2;
    VK_CHECK(vkCreateQueryPool(m_device, &info, nullptr, &m_pool));

    // Host-side reset so fetchMs can be called before the first command buffer
    // that uses the timer has been submitted.
    vkResetQueryPool(m_device, m_pool, 0, info.queryCount);
}

GPUTimer::~GPUTimer() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_pool, nullptr);
    }
}

void GPUTimer::resetSlot(VkCommandBuffer cmd, uint32_t slot) {
    if (!m_supported) return;
    vkCmdResetQueryPool(cmd, m_pool, slot * 2, 2);
}

void GPUTimer::begin(VkCommandBuffer cmd, uint32_t slot) {
    if (!m_supported) return;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_pool, slot * 2);
}

void GPUTimer::end(VkCommandBuffer cmd, uint32_t slot) {
    if (!m_supported) return;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool, slot * 2 + 1);
}

bool GPUTimer::fetchMs(uint32_t slot, float& outMs) {
    outMs = 0.0f;
    if (!m_supported || slot >= m_ringSize) return false;

    struct Result {
        uint64_t value;
        uint64_t availability;
    };
    Result results[2]{};

    VkResult r = vkGetQueryPoolResults(
        m_device, m_pool,
        slot * 2, 2,
        sizeof(results), results,
        sizeof(Result),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (r == VK_NOT_READY) {
        return false;
    }
    if (r != VK_SUCCESS) {
        return false;
    }

    // Availability of 0 means the query has not been processed yet.
    if (results[0].availability == 0 || results[1].availability == 0) {
        return false;
    }

    uint64_t begin = results[0].value;
    uint64_t end = results[1].value;
    if (end <= begin) {
        return false;
    }

    // timestampPeriod is in nanoseconds per tick, so divide by 1'000'000 to get ms.
    float ms = static_cast<float>(static_cast<double>(end - begin) * m_period / 1'000'000.0);
    m_elapsedMs[slot] = ms;
    m_valid[slot] = true;
    outMs = ms;
    return true;
}

float GPUTimer::averageMs() const {
    float sum = 0.0f;
    uint32_t count = 0;
    for (uint32_t i = 0; i < m_ringSize; ++i) {
        if (m_valid[i]) {
            sum += m_elapsedMs[i];
            ++count;
        }
    }
    return (count > 0) ? (sum / static_cast<float>(count)) : 0.0f;
}

} // namespace kazu
