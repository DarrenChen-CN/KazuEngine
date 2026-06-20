// ============================================================================
// KazuEngine - RHI Layer: Texture (Implementation)
// ============================================================================

#include "Texture.h"
#include "../core/Utils.h"
#include "../core/Buffer.h"
#include "../core/CommandBuffer.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../core/stb_image.h"
#include <spdlog/spdlog.h>

namespace kazu {

Texture::Texture(Context& ctx, const std::string& path, bool srgb) : m_ctx(&ctx) {
    loadFromFile(ctx, path, srgb);
}

Texture::Texture(Context& ctx, std::unique_ptr<Image> image, const VkSamplerCreateInfo& samplerInfo)
    : m_ctx(&ctx)
    , m_image(std::move(image))
    , m_sampler(std::make_unique<Sampler>(ctx, samplerInfo)) {
}

void Texture::loadFromFile(Context& ctx, const std::string& path, bool srgb) {
    int width, height, channels;
    bool isHdr = stbi_is_hdr(path.c_str());

    VkDeviceSize imageSize = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    void* pixelData = nullptr;

    if (isHdr) {
        float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            fatalError("Failed to load HDR texture: " + path);
        }
        imageSize = static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);
        format = VK_FORMAT_R32G32B32A32_SFLOAT;
        pixelData = pixels;
    } else {
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            fatalError("Failed to load texture: " + path);
        }
        imageSize = static_cast<VkDeviceSize>(width) * height * 4;
        format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        pixelData = pixels;
    }

    Buffer stagingBuffer(ctx, imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.upload(pixelData, imageSize);
    stbi_image_free(pixelData);

    m_image = std::make_unique<Image>(ctx,
        ImageDesc{
            VK_IMAGE_TYPE_2D,
            {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
            1,
            1,
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});

    // One-time command buffer for layout transition + copy (from shared transient pool)
    CommandBuffer cmd(ctx, ctx.transientPool());
    cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    m_image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = m_image->extent();
    vkCmdCopyBufferToImage(cmd.handle(), stagingBuffer.handle(), m_image->handle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    m_image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    cmd.end();
    cmd.submit(ctx.graphicsQueue());
    vkQueueWaitIdle(ctx.graphicsQueue());

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    m_sampler = std::make_unique<Sampler>(ctx, samplerInfo);

    spdlog::info("[Texture] Loaded: {} ({}x{}, {})", path, width, height, isHdr ? "HDR" : (srgb ? "sRGB" : "linear"));
}

VkDescriptorImageInfo Texture::descriptorInfo() const {
    VkDescriptorImageInfo info{};
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView = m_image->view();
    info.sampler = m_sampler->handle();
    return info;
}

} // namespace kazu
