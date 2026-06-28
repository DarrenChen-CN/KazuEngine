// ============================================================================
// KazuEngine - Pass Layer: Screen Space Reflection (Implementation)
// ============================================================================

#include "pass/SSRPass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rendergraph/RenderGraph.h"
#include <glm/glm.hpp>
#include <array>

namespace kazu {

namespace {

struct SSRPush {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 cameraPos;   // xyz = world camera position
    glm::vec4 params;      // x = maxSteps, y = stepSize, z = thickness, w = roughnessScale
    glm::vec2 screenSize;
    int32_t   displayMode;
    int32_t   traceMode;   // 0 = basic, 1 = binary, 2 = Hi-Z
    int32_t   hizMaxMip;
    int32_t   _pad;
};

} // anonymous namespace

SSRPass::SSRPass() = default;

SSRPass::~SSRPass() {
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

void SSRPass::setInputs(RenderGraph::ResourceHandle sceneColor,
                        RenderGraph::ResourceHandle depth,
                        RenderGraph::ResourceHandle normal,
                        RenderGraph::ResourceHandle material,
                        RenderGraph::ResourceHandle hiz) {
    m_sceneColorHandle = sceneColor;
    m_depthHandle      = depth;
    m_normalHandle     = normal;
    m_materialHandle   = material;
    m_hizHandle        = hiz;
}

void SSRPass::declare(RHI* rhi, RenderGraph* rg) {
    m_outputHandle = rg->addTexture("SceneColorWithSSR",
        {.width  = rhi->extent().width,
         .height = rhi->extent().height,
         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    SSRPass* self = this;
    m_passHandle = rg->addPass("SSR", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.read(self->m_sceneColorHandle);
        b.read(self->m_depthHandle);
        b.read(self->m_normalHandle);
        b.read(self->m_materialHandle);
        if (self->m_hizHandle != RenderGraph::InvalidResource)
            b.read(self->m_hizHandle);
        b.writeStorageImage(self->m_outputHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void SSRPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_renderGraph = ctx.renderGraph;

    createPipeline();
    createDescriptorSet();
}

void SSRPass::createPipeline() {
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

    // Binding 0 = output storage image
    // Binding 1 = scene color sampler
    // Binding 2 = depth sampler
    // Binding 3 = normal sampler
    // Binding 4 = material sampler
    // Binding 5 = Hi-Z pyramid sampler
    std::vector<VkDescriptorSetLayoutBinding> bindings(6);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    for (uint32_t i = 1; i < 6; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate(bindings);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(SSRPush);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    VK_CHECK(vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout));

    VkShaderModule cs = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("ssr.comp.spv"));

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = cs;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline));
}

void SSRPass::createDescriptorSet() {
    VkDevice device = m_rhi->ctx().device();

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 5;

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

    VkImageView outputView     = m_renderGraph->getImageView(m_outputHandle);
    VkImageView sceneColorView = m_renderGraph->getImageView(m_sceneColorHandle);
    VkImageView depthView      = m_renderGraph->getImageView(m_depthHandle);
    VkImageView normalView     = m_renderGraph->getImageView(m_normalHandle);
    VkImageView materialView   = m_renderGraph->getImageView(m_materialHandle);
    VkImageView hizView        = (m_hizHandle != RenderGraph::InvalidResource)
                                   ? m_renderGraph->getImageView(m_hizHandle)
                                   : depthView; // fallback, won't be sampled in basic/binary modes

    std::array<VkDescriptorImageInfo, 6> infos{};
    infos[0].imageView = outputView;
    infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    infos[0].sampler = VK_NULL_HANDLE;

    infos[1].sampler = m_sampler;
    infos[1].imageView = sceneColorView;
    infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[2].sampler = m_sampler;
    infos[2].imageView = depthView;
    infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    infos[3].sampler = m_sampler;
    infos[3].imageView = normalView;
    infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[4].sampler = m_sampler;
    infos[4].imageView = materialView;
    infos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[5].sampler = m_sampler;
    infos[5].imageView = hizView;
    infos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 6> writes{};
    for (uint32_t i = 0; i < 6; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &infos[i];
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    for (uint32_t i = 1; i < 6; ++i) {
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void SSRPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    VkExtent2D extent = m_rhi->extent();
    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 view = ctx.camera->getViewMatrix();
    glm::mat4 proj = ctx.camera->getJitteredProjectionMatrix(aspect);

    SSRPush push{};
    push.viewProj    = proj * view;
    push.invViewProj = glm::inverse(push.viewProj);
    push.cameraPos   = glm::vec4(ctx.camera->position(), 1.0f);
    push.params.x    = 64.0f;   // maxSteps
    push.params.y    = 0.05f;   // stepSize (world space)
    push.params.z    = 0.05f;   // thickness
    push.params.w    = 1.0f;    // roughnessScale
    push.screenSize  = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
    push.displayMode = m_enabled ? m_displayMode : 0;
    push.traceMode   = m_enabled ? m_traceMode : 1;
    push.hizMaxMip   = (m_hizHandle != RenderGraph::InvalidResource) ? 10 : 0;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(SSRPush), &push);

    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

} // namespace kazu
