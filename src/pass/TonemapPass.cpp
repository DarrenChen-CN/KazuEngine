// ============================================================================
// KazuEngine - Pass Layer: Tone Mapping Pass (Implementation)
// ============================================================================

#include "pass/TonemapPass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rendergraph/RenderGraph.h"
#include <array>

namespace kazu {

struct TonemapPush {
    float exposure;
    float gamma;
    int   mode;
    float bloomIntensity;
};

TonemapPass::TonemapPass() = default;

TonemapPass::~TonemapPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    if (m_descriptorPool)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_pipeline)
        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout)
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    // m_descriptorSetLayout is owned by DSL cache.
    if (m_sampler)
        vkDestroySampler(device, m_sampler, nullptr);
}

void TonemapPass::declare(RHI* rhi, RenderGraph* rg) {
    m_outputLDRHandle = rg->addTexture("SceneColorLDR",
        {.width  = rhi->extent().width,
         .height = rhi->extent().height,
         .format = VK_FORMAT_R8G8B8A8_UNORM,
         .usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    TonemapPass* self = this;
    m_passHandle = rg->addPass("Tonemap", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.read(self->m_inputHDRHandle);
        b.read(self->m_bloomHandle);
        b.writeStorageImage(self->m_outputLDRHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void TonemapPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_renderGraph = ctx.renderGraph;

    createPipeline();
    createDescriptorSet();
}

void TonemapPass::createPipeline() {
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
    pushRange.size = sizeof(TonemapPush);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout));

    VkShaderModule cs = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("tonemap.comp.spv"));

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = cs;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline));
}

void TonemapPass::createDescriptorSet() {
    VkDevice device = m_rhi->ctx().device();

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2;

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

    VkImageView ldrView   = m_renderGraph->getImageView(m_outputLDRHandle);
    VkImageView inputView = m_renderGraph->getImageView(m_inputHDRHandle);
    VkImageView bloomView = m_renderGraph->getImageView(m_bloomHandle);

    VkDescriptorImageInfo outInfo{};
    outInfo.imageView = ldrView;
    outInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorImageInfo inInfo{};
    inInfo.sampler = m_sampler;
    inInfo.imageView = inputView;
    inInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo bloomInfo{};
    bloomInfo.sampler = m_sampler;
    bloomInfo.imageView = bloomView;
    bloomInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &outInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &inInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &bloomInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void TonemapPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    TonemapPush push{};
    push.exposure = m_exposure;
    push.gamma    = m_gamma;
    push.mode     = m_mode;
    push.bloomIntensity = m_bloomIntensity;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(TonemapPush), &push);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    VkExtent2D extent = m_rhi->extent();
    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

} // namespace kazu
