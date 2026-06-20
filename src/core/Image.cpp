// ============================================================================
// KazuEngine - Core Layer: Image (Implementation)
// ============================================================================

#include "Image.h"
#include "Utils.h"
#include <spdlog/spdlog.h>

namespace {

VkImageAspectFlags getAspectMask(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

struct LayoutStageAccess {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
};

// stage/access for the *destination* of a transition into this layout.
LayoutStageAccess dstInfo(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    case VK_IMAGE_LAYOUT_GENERAL:
        return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0};
    default:
        return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    }
}

// stage/access for the *source* of a transition out of this layout.
LayoutStageAccess srcInfo(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    case VK_IMAGE_LAYOUT_GENERAL:
        return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    default:
        return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    }
}

} // anonymous namespace

namespace kazu {

Image::Image(Context& ctx, const ImageDesc& desc)
    : m_ctx(&ctx)
    , m_desc(desc)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = desc.flags;
    imageInfo.imageType = desc.type;
    imageInfo.extent = desc.extent;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = desc.format;
    imageInfo.tiling = desc.tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = desc.usage;
    imageInfo.samples = desc.samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.requiredFlags = desc.properties;

    VK_CHECK(vmaCreateImage(m_ctx->allocator(), &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr));

    createDefaultView();
}

Image::Image(Context& ctx, uint32_t width, uint32_t height, VkFormat format,
             VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
    : Image(ctx, ImageDesc{
          VK_IMAGE_TYPE_2D,
          {width, height, 1},
          1,
          1,
          format,
          usage,
          properties,
          0,
          VK_SAMPLE_COUNT_1_BIT,
          VK_IMAGE_TILING_OPTIMAL})
{
}

Image::~Image() {
    if (m_ctx) {
        for (VkImageView v : m_extraViews) {
            if (v != VK_NULL_HANDLE) vkDestroyImageView(m_ctx->device(), v, nullptr);
        }
        if (m_defaultView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_ctx->device(), m_defaultView, nullptr);
        }
        if (m_image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_ctx->allocator(), m_image, m_allocation);
        }
    }
}

Image::Image(Image&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_desc(other.m_desc)
    , m_image(other.m_image)
    , m_defaultView(other.m_defaultView)
    , m_extraViews(std::move(other.m_extraViews))
    , m_allocation(other.m_allocation)
{
    other.m_image = VK_NULL_HANDLE;
    other.m_defaultView = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        this->~Image();
        m_ctx = other.m_ctx;
        m_desc = other.m_desc;
        m_image = other.m_image;
        m_defaultView = other.m_defaultView;
        m_extraViews = std::move(other.m_extraViews);
        m_allocation = other.m_allocation;
        other.m_image = VK_NULL_HANDLE;
        other.m_defaultView = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
    }
    return *this;
}

void Image::createDefaultView() {
    ImageViewDesc desc{};
    switch (m_desc.type) {
    case VK_IMAGE_TYPE_1D:
        desc.viewType = (m_desc.arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        break;
    case VK_IMAGE_TYPE_2D:
        if ((m_desc.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && m_desc.arrayLayers >= 6) {
            desc.viewType = (m_desc.arrayLayers > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
            desc.layerCount = 6;
        } else {
            desc.viewType = (m_desc.arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        }
        break;
    case VK_IMAGE_TYPE_3D:
        desc.viewType = VK_IMAGE_VIEW_TYPE_3D;
        break;
    default:
        desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
        break;
    }
    desc.levelCount = m_desc.mipLevels;
    desc.layerCount = m_desc.arrayLayers;
    m_defaultView = createViewInternal(desc);
}

VkImageView Image::createViewInternal(const ImageViewDesc& desc) const {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = desc.viewType;
    viewInfo.format = (desc.format != VK_FORMAT_UNDEFINED) ? desc.format : m_desc.format;
    viewInfo.subresourceRange.aspectMask = (desc.aspectMask != 0) ? desc.aspectMask : getAspectMask(m_desc.format);
    viewInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
    viewInfo.subresourceRange.levelCount = desc.levelCount;
    viewInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
    viewInfo.subresourceRange.layerCount = desc.layerCount;

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(m_ctx->device(), &viewInfo, nullptr, &view));
    return view;
}

VkImageView Image::createView(const ImageViewDesc& desc) {
    VkImageView view = createViewInternal(desc);
    m_extraViews.push_back(view);
    return view;
}

void Image::transitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout) {
    if (oldLayout == newLayout) return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = getAspectMask(m_desc.format);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = m_desc.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = m_desc.arrayLayers;

    auto src = srcInfo(oldLayout);
    auto dst = dstInfo(newLayout);
    barrier.srcAccessMask = src.access;
    barrier.dstAccessMask = dst.access;

    vkCmdPipelineBarrier(cmd, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace kazu
