// ============================================================================
// KazuEngine - Core Layer: Swapchain
//
// Encapsulates the window presentation pipeline:
//   - VkSurfaceKHR
//   - VkSwapchainKHR
//   - VkImageView[] (per swapchain image)
//   - VkFramebuffer[] (per swapchain image)
//
// Supports dynamic window resize via recreate().
// ============================================================================

#pragma once

#include "Context.h"
#include <GLFW/glfw3.h>
#include <vector>

namespace kazu {

class Swapchain {
public:
    Swapchain(Context& ctx, GLFWwindow* window, VkRenderPass renderPass);
    ~Swapchain();

    // Non-copyable, movable
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;

    // Core operations
    VkResult acquireNextImage(VkSemaphore signalSemaphore, uint32_t& imageIndex);
    VkResult presentImage(uint32_t imageIndex, VkSemaphore waitSemaphore);
    void recreate(VkRenderPass renderPass);

    // Queries
    VkFormat format() const { return m_format; }
    VkExtent2D extent() const { return m_extent; }
    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }
    VkFramebuffer framebuffer(uint32_t index) const { return m_framebuffers[index]; }
    VkSwapchainKHR handle() const { return m_swapchain; }

private:
    Context* m_ctx = nullptr;
    GLFWwindow* m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent = {};

    // Depth buffer
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthAllocation = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    void createSurface();
    void createSwapchain();
    void createImageViews();
    void createDepthResources();
public:
    void createFramebuffers(VkRenderPass renderPass);
private:
    void cleanup();

    SwapChainSupportDetails querySwapChainSupport();
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps);
};

} // namespace kazu
