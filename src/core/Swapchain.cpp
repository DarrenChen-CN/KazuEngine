// ============================================================================
// KazuEngine - Core Layer: Swapchain (Implementation)
// ============================================================================

#include "Swapchain.h"
#include "Utils.h"
#include <algorithm>
#include <limits>

namespace kazu {

// ============================================================================
// Construction / Destruction
// ============================================================================

Swapchain::Swapchain(Context& ctx, GLFWwindow* window, VkRenderPass renderPass)
    : m_ctx(&ctx)
    , m_window(window)
{
    createSurface();
    createSwapchain();
    createImageViews();
    if (renderPass != VK_NULL_HANDLE) {
        createFramebuffers(renderPass);
    }
}

Swapchain::~Swapchain() {
    cleanup();
}

// ============================================================================
// Move Semantics
// ============================================================================

Swapchain::Swapchain(Swapchain&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_window(other.m_window)
    , m_surface(other.m_surface)
    , m_swapchain(other.m_swapchain)
    , m_images(std::move(other.m_images))
    , m_imageViews(std::move(other.m_imageViews))
    , m_framebuffers(std::move(other.m_framebuffers))
    , m_format(other.m_format)
    , m_extent(other.m_extent)
{
    other.m_surface = VK_NULL_HANDLE;
    other.m_swapchain = VK_NULL_HANDLE;
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_ctx = other.m_ctx;
        m_window = other.m_window;
        m_surface = other.m_surface;
        m_swapchain = other.m_swapchain;
        m_images = std::move(other.m_images);
        m_imageViews = std::move(other.m_imageViews);
        m_framebuffers = std::move(other.m_framebuffers);
        m_format = other.m_format;
        m_extent = other.m_extent;

        other.m_surface = VK_NULL_HANDLE;
        other.m_swapchain = VK_NULL_HANDLE;
    }
    return *this;
}

// ============================================================================
// Surface
// ============================================================================

void Swapchain::createSurface() {
    VK_CHECK(glfwCreateWindowSurface(m_ctx->instance(), m_window, nullptr, &m_surface));
}

// ============================================================================
// Swapchain Creation
// ============================================================================

void Swapchain::createSwapchain() {
    SwapChainSupportDetails swapSupport = querySwapChainSupport();
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapSupport.capabilities);

    uint32_t imageCount = swapSupport.capabilities.minImageCount + 1;
    if (swapSupport.capabilities.maxImageCount > 0 && imageCount > swapSupport.capabilities.maxImageCount) {
        imageCount = swapSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = { m_ctx->graphicsFamily(), m_ctx->presentFamily() };
    if (m_ctx->graphicsFamily() != m_ctx->presentFamily()) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(m_ctx->device(), &createInfo, nullptr, &m_swapchain));

    vkGetSwapchainImagesKHR(m_ctx->device(), m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_ctx->device(), m_swapchain, &imageCount, m_images.data());
    m_format = surfaceFormat.format;
    m_extent = extent;
}

void Swapchain::createImageViews() {
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(m_ctx->device(), &createInfo, nullptr, &m_imageViews[i]));
    }
}

void Swapchain::createFramebuffers(VkRenderPass renderPass) {
    // Clean up old framebuffers if any
    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_ctx->device(), framebuffer, nullptr);
    }
    m_framebuffers.clear();

    m_framebuffers.resize(m_imageViews.size());
    for (size_t i = 0; i < m_imageViews.size(); ++i) {
        VkImageView attachments[] = { m_imageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_extent.width;
        framebufferInfo.height = m_extent.height;
        framebufferInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(m_ctx->device(), &framebufferInfo, nullptr, &m_framebuffers[i]));
    }
}

void Swapchain::cleanup() {
    if (!m_ctx) return;

    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_ctx->device(), framebuffer, nullptr);
    }
    m_framebuffers.clear();

    for (auto imageView : m_imageViews) {
        vkDestroyImageView(m_ctx->device(), imageView, nullptr);
    }
    m_imageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_ctx->device(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_ctx->instance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Recreate
// ============================================================================

void Swapchain::recreate(VkRenderPass renderPass) {
    vkDeviceWaitIdle(m_ctx->device());

    // Destroy swapchain-dependent resources, but keep surface
    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_ctx->device(), framebuffer, nullptr);
    }
    m_framebuffers.clear();

    for (auto imageView : m_imageViews) {
        vkDestroyImageView(m_ctx->device(), imageView, nullptr);
    }
    m_imageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_ctx->device(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }

    m_images.clear();

    createSwapchain();
    createImageViews();
    createFramebuffers(renderPass);
    spdlog::info("Swapchain recreated: {}x{}", m_extent.width, m_extent.height);
}

// ============================================================================
// Acquire / Present
// ============================================================================

VkResult Swapchain::acquireNextImage(VkSemaphore signalSemaphore, uint32_t& imageIndex) {
    return vkAcquireNextImageKHR(m_ctx->device(), m_swapchain, UINT64_MAX,
                                 signalSemaphore, VK_NULL_HANDLE, &imageIndex);
}

VkResult Swapchain::presentImage(uint32_t imageIndex, VkSemaphore waitSemaphore) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(m_ctx->presentQueue(), &presentInfo);
}

// ============================================================================
// Helpers
// ============================================================================

Swapchain::SwapChainSupportDetails Swapchain::querySwapChainSupport() {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_ctx->physicalDevice(), m_surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx->physicalDevice(), m_surface, &formatCount, nullptr);
    if (formatCount) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx->physicalDevice(), m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx->physicalDevice(), m_surface, &presentModeCount, nullptr);
    if (presentModeCount) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx->physicalDevice(), m_surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkSurfaceFormatKHR Swapchain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }
    return formats[0];
}

VkPresentModeKHR Swapchain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }

    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);

    VkExtent2D extent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

} // namespace kazu
