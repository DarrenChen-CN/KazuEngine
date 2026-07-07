// ============================================================================
// KazuEngine - RHI: RHI Context
//
// Encapsulates Vulkan object lifecycle (init/cleanup) and per-frame
// acquire/present. Holds all Core + RHI layer objects.
// Application layer (main.cpp) uses this to record rendering commands.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

namespace kazu {
class Context;
class Swapchain;
class ShaderLibrary;
class PipelineCache;
class DescriptorSetLayoutCache;
class CommandPool;
class CommandBuffer;
class SyncObjects;
}

namespace kazu {

class RHI {
public:
    RHI();
    ~RHI();

    bool init(GLFWwindow* window, uint32_t uiPanelWidth = 0);
    void cleanup();

    // Per-frame: acquire -> record -> present
    bool beginFrame(uint32_t& imageIndex);   // false = out-of-date, skip frame
    void endFrame(uint32_t imageIndex);

    // Per-frame command buffer
    VkCommandBuffer currentCmd() const;

    // Extent used by the 3D rendering pipeline (swapchain minus UI panel).
    VkExtent2D extent() const;
    float aspect() const;

    // Full swapchain extent (including the UI panel area).
    VkExtent2D swapchainExtent() const;

    // Swapchain queries
    VkImage swapchainImage(uint32_t imageIndex) const;
    VkImageView swapchainImageView(uint32_t imageIndex) const;
    uint32_t swapchainImageCount() const;
    VkFormat swapchainFormat() const;

    // For scene loading
    ShaderLibrary& shaderLib();
    DescriptorSetLayoutCache& dslCache();
    PipelineCache& pipelineCache();
    Context& ctx();
    VkCommandPool commandPool() const;

    void setFramebufferResized(bool v) { m_framebufferResized = v; }

private:
    void createCommandPoolAndBuffers();
    void createSyncObjects();
    void recreateSwapchain();
    void updateRenderExtent();

    static const int MAX_FRAMES_IN_FLIGHT = 2;

    GLFWwindow* m_window = nullptr;
    std::unique_ptr<Context> m_ctx;
    std::unique_ptr<Swapchain> m_swapchain;
    std::unique_ptr<ShaderLibrary> m_shaderLibrary;
    std::unique_ptr<PipelineCache> m_pipelineCache;
    std::unique_ptr<DescriptorSetLayoutCache> m_descriptorSetLayoutCache;
    std::unique_ptr<CommandPool> m_commandPool;
    std::vector<std::unique_ptr<CommandBuffer>> m_commandBuffers;
    std::unique_ptr<SyncObjects> m_syncObjects;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;

    uint32_t m_uiPanelWidth = 0;
    VkExtent2D m_renderExtent{};
};

} // namespace kazu
