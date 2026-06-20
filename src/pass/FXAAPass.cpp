// ============================================================================
// KazuEngine - Pass Layer: FXAA Pass (Implementation)
// ============================================================================

#include "pass/FXAAPass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rendergraph/RenderGraph.h"
#include <array>

namespace kazu {

struct FXAAPush {
    int enabled;
    int _pad[3];
};

FXAAPass::FXAAPass() = default;

FXAAPass::~FXAAPass() {
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

void FXAAPass::declare(RHI* rhi, RenderGraph* rg) {
    m_outputHandle = rg->addTexture("SceneColorPost",
        {.width  = rhi->extent().width,
         .height = rhi->extent().height,
         .format = VK_FORMAT_R8G8B8A8_UNORM,
         .usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    FXAAPass* self = this;
    m_passHandle = rg->addPass("FXAA", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.read(self->m_inputLDRHandle);
        b.writeStorageImage(self->m_outputHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void FXAAPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_renderGraph = ctx.renderGraph;

    createPipeline();
    createDescriptorSet();
}

void FXAAPass::createPipeline() {
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

    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate(bindings);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(FXAAPush);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout));

    VkShaderModule cs = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("fxaa.comp.spv"));

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = cs;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline));
}

void FXAAPass::createDescriptorSet() {
    VkDevice device = m_rhi->ctx().device();

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

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

    VkImageView outputView = m_renderGraph->getImageView(m_outputHandle);
    VkImageView inputView  = m_renderGraph->getImageView(m_inputLDRHandle);

    VkDescriptorImageInfo outInfo{};
    outInfo.imageView = outputView;
    outInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorImageInfo inInfo{};
    inInfo.sampler = m_sampler;
    inInfo.imageView = inputView;
    inInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
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

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void FXAAPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    FXAAPush push{};
    push.enabled = m_enabled ? 1 : 0;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(FXAAPush), &push);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    VkExtent2D extent = m_rhi->extent();
    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

} // namespace kazu
