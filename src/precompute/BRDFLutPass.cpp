// ============================================================================
// KazuEngine - Pass Layer: BRDF LUT Precompute Pass (Implementation)
// ============================================================================

#include "precompute/BRDFLutPass.h"
#include "core/Utils.h"
#include "core/Path.h"
#include "core/CommandBuffer.h"
#include "rendergraph/RenderGraph.h"
#include "rhi/RHI.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include <spdlog/spdlog.h>

namespace kazu {

BRDFLutPass::BRDFLutPass() = default;

BRDFLutPass::~BRDFLutPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    // m_descriptorSetLayout is borrowed from DescriptorSetLayoutCache.
}

PrecomputePass::OutputDesc BRDFLutPass::outputDesc() const {
    return {
        "BRDFLut",
        m_size, m_size,
        1, 1,
        VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        0
    };
}

void BRDFLutPass::setOutputResource(RenderGraph::ResourceHandle handle, Image* image) {
    m_lutHandle = handle;
    m_outputImage = image;
}

void BRDFLutPass::createPipeline() {
    VkDevice device = m_rhi->ctx().device();

    VkShaderModule shaderModule = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("brdf_lut.comp.spv"));

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate({binding});

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
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

void BRDFLutPass::createDescriptorSet() {
    VkDevice device = m_rhi->ctx().device();

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = m_outputImage->view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void BRDFLutPass::declare(RHI* rhi, RenderGraph* rg) {
    m_rhi = rhi;
    m_renderGraph = rg;

    BRDFLutPass* self = this;
    rg->addPass("BRDFLutCompute", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.writeStorageImage(self->m_lutHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });

    rg->addPass("BRDFLutFinalize", [&](RenderGraph::PassBuilder& b) {
        b.read(self->m_lutHandle);
        b.execute = [](const PassExecuteContext&) {};
    });
}

void BRDFLutPass::create(const PassCreateContext& ctx) {
    (void)ctx;
    createPipeline();
    createDescriptorSet();
}

void BRDFLutPass::execute(const PassExecuteContext& ctx) {
    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdDispatch(ctx.cmd, (m_size + 15) / 16, (m_size + 15) / 16, 1);
}

} // namespace kazu
