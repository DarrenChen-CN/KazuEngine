// ============================================================================
// KazuEngine - Pass Layer: Bloom Pass (Implementation)
// ============================================================================

#include "pass/BloomPass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "core/Image.h"
#include "rhi/RHI.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rendergraph/RenderGraph.h"
#include <array>
#include <algorithm>

namespace kazu {

struct BloomPush {
    float threshold;
    float intensity;
    float upsampleRadius;
    int   enabled;
};

BloomPass::BloomPass() = default;

BloomPass::~BloomPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    if (m_descriptorPool)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_thresholdPipeline)
        vkDestroyPipeline(device, m_thresholdPipeline, nullptr);
    if (m_downsamplePipeline)
        vkDestroyPipeline(device, m_downsamplePipeline, nullptr);
    if (m_upsamplePipeline)
        vkDestroyPipeline(device, m_upsamplePipeline, nullptr);
    if (m_pipelineLayout)
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_sampler)
        vkDestroySampler(device, m_sampler, nullptr);
    // Per-mip views and descriptor set layout are owned by Image / cache.
}

uint32_t BloomPass::computeMipLevels(uint32_t width, uint32_t height) const {
    uint32_t minDim = std::max(1u, std::min(width, height));
    uint32_t levels = 1;
    while (minDim > 1) {
        minDim >>= 1;
        ++levels;
    }
    // Cap at a reasonable number; bloom rarely needs more than 6 mips.
    return std::min(levels, 8u);
}

void BloomPass::declare(RHI* rhi, RenderGraph* rg) {
    VkExtent2D extent = rhi->extent();
    uint32_t halfW = std::max(1u, extent.width / 2);
    uint32_t halfH = std::max(1u, extent.height / 2);
    m_mipLevels = computeMipLevels(halfW, halfH);

    m_bloomDownHandle = rg->addTexture("BloomDown",
        {.width  = halfW,
         .height = halfH,
         .mipLevels = m_mipLevels,
         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    m_bloomHandle = rg->addTexture("Bloom",
        {.width  = halfW,
         .height = halfH,
         .mipLevels = m_mipLevels,
         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    BloomPass* self = this;
    m_passHandle = rg->addPass("Bloom", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.read(self->m_inputHDRHandle);
        b.writeStorageImage(self->m_bloomDownHandle);
        b.writeStorageImage(self->m_bloomHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void BloomPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_renderGraph = ctx.renderGraph;

    m_bloomDownImage = m_renderGraph->getImage(m_bloomDownHandle);
    m_bloomImage = m_renderGraph->getImage(m_bloomHandle);
    if (!m_bloomDownImage || !m_bloomImage) {
        fatalError("BloomPass: failed to get bloom images");
    }

    createPipelines();
    createDescriptorSets();
}

void BloomPass::createPipelines() {
    VkDevice device = m_rhi->ctx().device();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 32.0f;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler));

    std::vector<VkDescriptorSetLayoutBinding> bindings(3);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate(bindings);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(BloomPush);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout));

    auto createComputePipeline = [&](const char* shaderName) -> VkPipeline {
        VkShaderModule cs = m_rhi->shaderLib().load(
            kazu::Path::resolveShader(shaderName));

        VkComputePipelineCreateInfo pipeInfo{};
        pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeInfo.stage.module = cs;
        pipeInfo.stage.pName = "main";
        pipeInfo.layout = m_pipelineLayout;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline));
        return pipeline;
    };

    m_thresholdPipeline  = createComputePipeline("bloom_threshold.comp.spv");
    m_downsamplePipeline = createComputePipeline("bloom_downsample.comp.spv");
    m_upsamplePipeline   = createComputePipeline("bloom_upsample.comp.spv");
}

void BloomPass::createDescriptorSets() {
    VkDevice device = m_rhi->ctx().device();

    // Create per-mip views of the bloom downsample and upsample images.
    m_downMipViews.resize(m_mipLevels);
    m_mipViews.resize(m_mipLevels);
    for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
        m_downMipViews[mip] = m_bloomDownImage->createView({
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            mip, 1,
            0, 1});
        m_mipViews[mip] = m_bloomImage->createView({
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            mip, 1,
            0, 1});
    }

    uint32_t setCount = 1;
    if (m_mipLevels > 1) {
        setCount += 2 * (m_mipLevels - 1);
    }
    m_descriptorSets.resize(setCount);

    std::vector<VkDescriptorPoolSize> poolSizes(2);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = setCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = setCount * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

    std::vector<VkDescriptorSetLayout> layouts(setCount, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()));

    VkImageView hdrView = m_renderGraph->getImageView(m_inputHDRHandle);

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(setCount * 3);
    std::vector<VkDescriptorImageInfo> infos;
    infos.reserve(setCount * 3);

    auto writeSet = [&](uint32_t setIndex,
                        VkImageView storageView,
                        VkImageView sampledView0,
                        VkImageLayout sampledLayout0,
                        VkImageView sampledView1,
                        VkImageLayout sampledLayout1) {
        uint32_t infoBase = static_cast<uint32_t>(infos.size());
        VkDescriptorImageInfo storageInfo{};
        storageInfo.imageView = storageView;
        storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageInfo.sampler = VK_NULL_HANDLE;
        infos.push_back(storageInfo);

        VkDescriptorImageInfo sampledInfo0{};
        sampledInfo0.sampler = m_sampler;
        sampledInfo0.imageView = sampledView0;
        sampledInfo0.imageLayout = sampledLayout0;
        infos.push_back(sampledInfo0);

        VkDescriptorImageInfo sampledInfo1{};
        sampledInfo1.sampler = m_sampler;
        sampledInfo1.imageView = sampledView1;
        sampledInfo1.imageLayout = sampledLayout1;
        infos.push_back(sampledInfo1);

        VkWriteDescriptorSet storageWrite{};
        storageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        storageWrite.dstSet = m_descriptorSets[setIndex];
        storageWrite.dstBinding = 0;
        storageWrite.dstArrayElement = 0;
        storageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageWrite.descriptorCount = 1;
        storageWrite.pImageInfo = &infos[infoBase];
        writes.push_back(storageWrite);

        VkWriteDescriptorSet sampledWrite0{};
        sampledWrite0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sampledWrite0.dstSet = m_descriptorSets[setIndex];
        sampledWrite0.dstBinding = 1;
        sampledWrite0.dstArrayElement = 0;
        sampledWrite0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampledWrite0.descriptorCount = 1;
        sampledWrite0.pImageInfo = &infos[infoBase + 1];
        writes.push_back(sampledWrite0);

        VkWriteDescriptorSet sampledWrite1{};
        sampledWrite1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sampledWrite1.dstSet = m_descriptorSets[setIndex];
        sampledWrite1.dstBinding = 2;
        sampledWrite1.dstArrayElement = 0;
        sampledWrite1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampledWrite1.descriptorCount = 1;
        sampledWrite1.pImageInfo = &infos[infoBase + 2];
        writes.push_back(sampledWrite1);
    };

    // Set 0: threshold - write BloomDown mip0, read HDR.
    writeSet(0,
             m_downMipViews[0],
             hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
             hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    uint32_t setIndex = 1;
    // Downsample sets: write BloomDown mip[i+1], read BloomDown mip[i].
    for (uint32_t i = 0; i + 1 < m_mipLevels; ++i) {
        writeSet(setIndex++,
                 m_downMipViews[i + 1],
                 m_downMipViews[i], VK_IMAGE_LAYOUT_GENERAL,
                 m_downMipViews[i], VK_IMAGE_LAYOUT_GENERAL);
    }
    // Upsample sets indexed by output mip i: write Bloom mip[i],
    // read current BloomDown mip[i] and accumulated previous smaller mip.
    for (uint32_t i = 0; i + 1 < m_mipLevels; ++i) {
        VkImageView prevView = (i + 2 == m_mipLevels) ? m_downMipViews[i + 1] : m_mipViews[i + 1];
        writeSet(setIndex++,
                 m_mipViews[i],
                 m_downMipViews[i], VK_IMAGE_LAYOUT_GENERAL,
                 prevView, VK_IMAGE_LAYOUT_GENERAL);
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
void BloomPass::insertComputeBarrier(VkCommandBuffer cmd) const {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);
}

void BloomPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    VkExtent2D extent = m_rhi->extent();
    VkExtent2D halfSize{};
    halfSize.width  = std::max(1u, extent.width / 2);
    halfSize.height = std::max(1u, extent.height / 2);

    // Threshold / clear pass.
    {
        BloomPush push{};
        push.threshold = m_threshold;
        push.intensity = m_intensity;
        push.upsampleRadius = m_upsampleRadius;
        push.enabled = m_enabled ? 1 : 0;

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(BloomPush), &push);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_thresholdPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_pipelineLayout, 0, 1, &m_descriptorSets[0], 0, nullptr);

        uint32_t groupsX = (halfSize.width + 15) / 16;
        uint32_t groupsY = (halfSize.height + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }

    if (m_mipLevels <= 1) {
        // No blur chain needed.
        return;
    }

    insertComputeBarrier(cmd);

    uint32_t setIndex = 1;

    // Downsample chain.
    for (uint32_t i = 0; i + 1 < m_mipLevels; ++i) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_pipelineLayout, 0, 1, &m_descriptorSets[setIndex++], 0, nullptr);

        uint32_t dstW = std::max(1u, halfSize.width >> (i + 1));
        uint32_t dstH = std::max(1u, halfSize.height >> (i + 1));
        uint32_t groupsX = (dstW + 15) / 16;
        uint32_t groupsY = (dstH + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        insertComputeBarrier(cmd);
    }

    // Upsample chain.
    {
        BloomPush push{};
        push.upsampleRadius = m_upsampleRadius;
        // Other fields are unused by the upsample shader.

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(BloomPush), &push);
    }

    uint32_t upsampleSetBase = setIndex;
    for (int32_t i = static_cast<int32_t>(m_mipLevels) - 2; i >= 0; --i) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_upsamplePipeline);
        VkDescriptorSet set = m_descriptorSets[upsampleSetBase + static_cast<uint32_t>(i)];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_pipelineLayout, 0, 1, &set, 0, nullptr);

        uint32_t dstW = std::max(1u, halfSize.width >> static_cast<uint32_t>(i));
        uint32_t dstH = std::max(1u, halfSize.height >> static_cast<uint32_t>(i));
        uint32_t groupsX = (dstW + 15) / 16;
        uint32_t groupsY = (dstH + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        if (i > 0) {
            insertComputeBarrier(cmd);
        }
    }
}

} // namespace kazu
