// ============================================================================
// KazuEngine - Pass Layer: TAA Pass (Implementation)
// ============================================================================

#include "pass/TAAPass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rendergraph/RenderGraph.h"
#include <array>

namespace kazu {

struct TAAPush {
    glm::mat4 invViewProj;
    glm::mat4 prevViewProj;
    int       enabled;
    int       _pad[3];
};

TAAPass::TAAPass() = default;

TAAPass::~TAAPass() {
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

void TAAPass::setMatrices(const glm::mat4& invViewProj, const glm::mat4& prevViewProj) {
    m_invViewProj = invViewProj;
    m_prevViewProj = prevViewProj;
}

void TAAPass::declare(RHI* rhi, RenderGraph* rg) {
    // Output is the persistent history image imported by the technique.
    TAAPass* self = this;
    m_passHandle = rg->addPass("TAA", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.read(self->m_inputHDRHandle);
        b.read(self->m_inputDepthHandle);
        b.read(self->m_historyReadHandle);
        b.writeStorageImage(self->m_historyWriteHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void TAAPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_renderGraph = ctx.renderGraph;

    createPipeline();
    createDescriptorSet();
}

void TAAPass::createPipeline() {
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

    std::vector<VkDescriptorSetLayoutBinding> bindings(4);
    for (uint32_t i = 0; i < 4; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate(bindings);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(TAAPush);
    // TAAPush uses 144 bytes; modern desktop GPUs provide at least 256.
    // On min-spec 128-byte hardware this would need to be split into a UBO.

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout));

    VkShaderModule cs = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("taa.comp.spv"));

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = cs;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline));
}

void TAAPass::createDescriptorSet() {
    VkDevice device = m_rhi->ctx().device();

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet));

    VkImageView outputView  = m_renderGraph->getImageView(m_historyWriteHandle);
    VkImageView hdrView     = m_renderGraph->getImageView(m_inputHDRHandle);
    VkImageView depthView   = m_renderGraph->getImageView(m_inputDepthHandle);
    VkImageView historyView = m_renderGraph->getImageView(m_historyReadHandle);

    std::array<VkDescriptorImageInfo, 4> infos{};
    infos[0].imageView = outputView;
    infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    infos[0].sampler = VK_NULL_HANDLE;

    infos[1].sampler = m_sampler;
    infos[1].imageView = hdrView;
    infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[2].sampler = m_sampler;
    infos[2].imageView = depthView;
    infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    infos[3].sampler = m_sampler;
    infos[3].imageView = historyView;
    infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 4> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &infos[i];
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void TAAPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    TAAPush push{};
    push.invViewProj = m_invViewProj;
    push.prevViewProj = m_prevViewProj;
    push.enabled = m_enabled ? 1 : 0;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(TAAPush), &push);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    VkExtent2D extent = m_rhi->extent();
    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

} // namespace kazu
