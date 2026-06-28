// ============================================================================
// KazuEngine - Pass Layer: Hi-Z Build (Implementation)
// ============================================================================

#include "pass/HiZPass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "core/Image.h"
#include "rhi/RHI.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rendergraph/RenderGraph.h"
#include <algorithm>

namespace kazu {

HiZPass::HiZPass() = default;

HiZPass::~HiZPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    if (m_descriptorPool)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_pipeline)
        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout)
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_sampler)
        vkDestroySampler(device, m_sampler, nullptr);
}

uint32_t HiZPass::computeMipLevels(uint32_t width, uint32_t height) {
    uint32_t size = std::max(width, height);
    uint32_t levels = 0;
    while (size > 0) {
        ++levels;
        size >>= 1;
    }
    return levels;
}

void HiZPass::declare(RHI* rhi, RenderGraph* rg) {
    VkExtent2D extent = rhi->extent();
    m_mipLevels = computeMipLevels(extent.width, extent.height);

    m_hizHandle = rg->addTexture("HiZPyramid",
        {.width     = extent.width,
         .height    = extent.height,
         .mipLevels = m_mipLevels,
         .format    = VK_FORMAT_R32_SFLOAT,
         .usage     = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    HiZPass* self = this;
    m_passHandle = rg->addPass("HiZBuild", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.read(self->m_depthHandle);
        b.writeStorageImage(self->m_hizHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void HiZPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_renderGraph = ctx.renderGraph;

    createPipeline();
    createDescriptorSets();
}

void HiZPass::createPipeline() {
    VkDevice device = m_rhi->ctx().device();

    // Binding 0 = source depth sampler
    // Binding 1 = destination HiZ mip storage image
    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate(bindings);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout));

    VkShaderModule cs = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("hiz_build.comp.spv"));

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = cs;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline));
}

void HiZPass::createDescriptorSets() {
    VkDevice device = m_rhi->ctx().device();

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = m_mipLevels;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = m_mipLevels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = m_mipLevels;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

    // Source for level 0 is the GBuffer depth texture; subsequent levels sample
    // the previously written HiZ mip.
    m_depthView = m_renderGraph->getImageView(m_depthHandle);

    Image* hizImage = m_renderGraph->getImage(m_hizHandle);
    m_dstViews.reserve(m_mipLevels);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 32.0f;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler));

    for (uint32_t i = 0; i < m_mipLevels; ++i) {
        VkImageView dstView = hizImage->createView(
            ImageViewDesc{VK_IMAGE_VIEW_TYPE_2D,
                          VK_FORMAT_R32_SFLOAT,
                          i, 1, 0, 1,
                          VK_IMAGE_ASPECT_COLOR_BIT});
        m_dstViews.push_back(dstView);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        VkDescriptorSet set;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &set));
        m_descriptorSets.push_back(set);

        VkImageView srcView = (i == 0) ? m_depthView
            : hizImage->createView(
                ImageViewDesc{VK_IMAGE_VIEW_TYPE_2D,
                              VK_FORMAT_R32_SFLOAT,
                              i - 1, 1, 0, 1,
                              VK_IMAGE_ASPECT_COLOR_BIT});

        VkDescriptorImageInfo srcInfo{};
        srcInfo.sampler = m_sampler;
        srcInfo.imageView = srcView;
        // Level 0 samples the depth texture which the RenderGraph keeps in
        // DEPTH_STENCIL_READ_ONLY_OPTIMAL. Subsequent levels sample mips of the
        // HiZ image, which stays in GENERAL for the duration of this pass.
        srcInfo.imageLayout = (i == 0)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo dstInfo{};
        dstInfo.sampler = VK_NULL_HANDLE;
        dstInfo.imageView = dstView;
        dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &srcInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &dstInfo;

        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

}

void HiZPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;
    Image* hizImage = m_renderGraph->getImage(m_hizHandle);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

    VkExtent2D baseExtent = m_rhi->extent();
    for (uint32_t i = 0; i < m_mipLevels; ++i) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_pipelineLayout, 0, 1, &m_descriptorSets[i], 0, nullptr);

        uint32_t width  = std::max(baseExtent.width  >> i, 1u);
        uint32_t height = std::max(baseExtent.height >> i, 1u);
        uint32_t groupsX = (width + 7) / 8;
        uint32_t groupsY = (height + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        // Ensure mip i is written before mip i+1 reads it.
        if (i + 1 < m_mipLevels) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = hizImage->handle();
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = i;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }
}

} // namespace kazu
