// ============================================================================
// KazuEngine - Precompute Layer: Prefiltered Environment Map Pass (Implementation)
// ============================================================================

#include "precompute/PrefilterEnvPass.h"
#include "precompute/PrecomputeManager.h"
#include "core/Utils.h"
#include "core/Path.h"
#include "core/Image.h"
#include "rendergraph/RenderGraph.h"
#include "rhi/RHI.h"
#include "rhi/Texture.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include <spdlog/spdlog.h>

namespace kazu {

uint32_t PrefilterEnvPass::mipCountForSize(uint32_t size) {
    uint32_t count = 0;
    while (size > 0) {
        ++count;
        size >>= 1;
    }
    return count;
}

PrefilterEnvPass::PrefilterEnvPass(uint32_t size)
    : m_size(size)
    , m_mipLevels(mipCountForSize(size)) {
}

PrefilterEnvPass::~PrefilterEnvPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
}

PrecomputePass::OutputDesc PrefilterEnvPass::outputDesc() const {
    return {
        "PrefilterEnv",
        m_size, m_size,
        m_mipLevels, 6,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    };
}

void PrefilterEnvPass::setOutputResource(RenderGraph::ResourceHandle handle, Image* image) {
    m_outputHandle = handle;
    m_outputImage = image;
}

void PrefilterEnvPass::resolveInputs(PrecomputeManager* mgr) {
    m_environmentCube = mgr->getTexture("EnvironmentCube");
    if (!m_environmentCube) {
        fatalError("PrefilterEnvPass: EnvironmentCube not found in PrecomputeManager");
    }
}

void PrefilterEnvPass::createPipeline() {
    VkDevice device = m_rhi->ctx().device();

    VkShaderModule shaderModule = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("prefilter_env.comp.spv"));

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate(
        {bindings[0], bindings[1]});

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout));

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_pipelineLayout;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));
}

void PrefilterEnvPass::createDescriptorSets() {
    VkDevice device = m_rhi->ctx().device();

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = m_mipLevels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = m_mipLevels;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

    std::vector<VkDescriptorSetLayout> layouts(m_mipLevels, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = m_mipLevels;
    allocInfo.pSetLayouts = layouts.data();
    m_descriptorSets.resize(m_mipLevels);
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()));

    VkDescriptorImageInfo inputInfo{};
    inputInfo.imageView = m_environmentCube->view();
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputInfo.sampler = m_environmentCube->sampler();

    m_mipArrayViews.reserve(m_mipLevels);
    std::vector<VkDescriptorImageInfo> outputInfos(m_mipLevels);
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(m_mipLevels * 2);

    for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
        VkImageView view = m_outputImage->createView({
            VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            mip, 1,
            0, 6});
        m_mipArrayViews.push_back(view);

        outputInfos[mip].imageView = view;
        outputInfos[mip].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        outputInfos[mip].sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet inputWrite{};
        inputWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        inputWrite.dstSet = m_descriptorSets[mip];
        inputWrite.dstBinding = 0;
        inputWrite.dstArrayElement = 0;
        inputWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        inputWrite.descriptorCount = 1;
        inputWrite.pImageInfo = &inputInfo;
        writes.push_back(inputWrite);

        VkWriteDescriptorSet outputWrite{};
        outputWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        outputWrite.dstSet = m_descriptorSets[mip];
        outputWrite.dstBinding = 1;
        outputWrite.dstArrayElement = 0;
        outputWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputWrite.descriptorCount = 1;
        outputWrite.pImageInfo = &outputInfos[mip];
        writes.push_back(outputWrite);
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void PrefilterEnvPass::declare(RHI* rhi, RenderGraph* rg) {
    m_rhi = rhi;
    m_renderGraph = rg;

    PrefilterEnvPass* self = this;
    rg->addPass("PrefilterEnvCompute", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.writeStorageImage(self->m_outputHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });

    rg->addPass("PrefilterEnvFinalize", [&](RenderGraph::PassBuilder& b) {
        b.read(self->m_outputHandle);
        b.execute = [](const PassExecuteContext&) {};
    });
}

void PrefilterEnvPass::create(const PassCreateContext& ctx) {
    (void)ctx;
    createPipeline();
    createDescriptorSets();
}

void PrefilterEnvPass::execute(const PassExecuteContext& ctx) {
    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

    for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
        uint32_t size = m_size >> mip;
        float roughness = (m_mipLevels <= 1)
            ? 0.0f
            : static_cast<float>(mip) / static_cast<float>(m_mipLevels - 1);

        vkCmdPushConstants(ctx.cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(float), &roughness);
        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
                                0, 1, &m_descriptorSets[mip], 0, nullptr);
        vkCmdDispatch(ctx.cmd, (size + 7) / 8, (size + 7) / 8, 6);
    }
}

} // namespace kazu
