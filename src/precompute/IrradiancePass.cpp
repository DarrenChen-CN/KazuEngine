// ============================================================================
// KazuEngine - Precompute Layer: Irradiance Convolution Pass (Implementation)
// ============================================================================

#include "precompute/IrradiancePass.h"
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

IrradiancePass::IrradiancePass() = default;

IrradiancePass::~IrradiancePass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);
    // m_outputArrayView 由 m_outputImage->createView() 创建，归 Image 自身管理，不要在这里销毁。
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
}

PrecomputePass::OutputDesc IrradiancePass::outputDesc() const {
    return {
        "Irradiance",
        64, 64,
        1, 6,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    };
}

void IrradiancePass::setOutputResource(RenderGraph::ResourceHandle handle, Image* image) {
    m_outputHandle = handle;
    m_outputImage = image;
}

void IrradiancePass::resolveInputs(PrecomputeManager* mgr) {
    m_environmentCube = mgr->getTexture("EnvironmentCube");
    if (!m_environmentCube) {
        fatalError("IrradiancePass: EnvironmentCube not found in PrecomputeManager");
    }
}

void IrradiancePass::createPipeline() {
    VkDevice device = m_rhi->ctx().device();

    VkShaderModule shaderModule = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("irradiance.comp.spv"));

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

void IrradiancePass::createDescriptorSet() {
    VkDevice device = m_rhi->ctx().device();

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet));

    m_outputArrayView = m_outputImage->createView({
        VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        0, 1,
        0, 6});

    VkDescriptorImageInfo envInfo{};
    envInfo.imageView = m_environmentCube->view();
    envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    envInfo.sampler = m_environmentCube->sampler();

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageView = m_outputArrayView;
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.sampler = VK_NULL_HANDLE;

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &envInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &outputInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void IrradiancePass::declare(RHI* rhi, RenderGraph* rg) {
    m_rhi = rhi;
    m_renderGraph = rg;

    IrradiancePass* self = this;
    rg->addPass("IrradianceCompute", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.writeStorageImage(self->m_outputHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });

    rg->addPass("IrradianceFinalize", [&](RenderGraph::PassBuilder& b) {
        b.read(self->m_outputHandle);
        b.execute = [](const PassExecuteContext&) {};
    });
}

void IrradiancePass::create(const PassCreateContext& ctx) {
    (void)ctx;
    createPipeline();
    createDescriptorSet();
}

void IrradiancePass::execute(const PassExecuteContext& ctx) {
    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdDispatch(ctx.cmd, (64 + 7) / 8, (64 + 7) / 8, 6);
}

} // namespace kazu
