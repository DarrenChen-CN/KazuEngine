// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Composer)
// ============================================================================

#include "technique/DeferredShading.h"
#include "pass/GBufferPass.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/PipelineBuilder.h"
#include "rhi/PipelineCache.h"
#include "core/PipelineLayout.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"
#include <glm/glm.hpp>
#include <array>

namespace kazu {

namespace {

struct LightingPush {
    glm::vec4 lightPos;
    glm::vec4 viewPos;
    int displayMode;
};

} // anonymous namespace

DeferredShading::DeferredShading() = default;

DeferredShading::~DeferredShading() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    if (m_lightingDescriptorPool)
        vkDestroyDescriptorPool(device, m_lightingDescriptorPool, nullptr);
    if (m_lightingDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(device, m_lightingDescriptorSetLayout, nullptr);
    if (m_lightingSampler)
        vkDestroySampler(device, m_lightingSampler, nullptr);
}

void DeferredShading::init(RHI* rhi, Scene* scene, Camera* camera) {
    m_rhi = rhi;
    m_scene = scene;
    m_camera = camera;

    // ---- Phase 1: Declare ----
    m_renderGraph = std::make_unique<RenderGraph>(m_rhi->ctx());

    m_gbufferPass = std::make_unique<GBufferPass>();
    m_gbufferPass->declare(m_rhi, m_renderGraph.get());

    auto albedoHandle = m_gbufferPass->albedoHandle();
    auto normalHandle = m_gbufferPass->normalHandle();

    DeferredShading* self = this;
    m_renderGraph->addPass("Lighting", [&](RenderGraph::PassBuilder& b) {
        b.read(albedoHandle);
        b.read(normalHandle);
        b.execute = [self](VkCommandBuffer cmd) {
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = self->m_rhi->renderPass();
            rpInfo.framebuffer = self->m_rhi->framebuffer(self->m_currentImageIndex);
            rpInfo.renderArea.offset = {0, 0};
            rpInfo.renderArea.extent = self->m_rhi->extent();
            std::array<VkClearValue, 2> clears{};
            clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clears[1].depthStencil = {1.0f, 0};
            rpInfo.clearValueCount = 2;
            rpInfo.pClearValues = clears.data();
            vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, self->m_lightingPipeline);

            VkViewport viewport{};
            viewport.width = static_cast<float>(self->m_rhi->extent().width);
            viewport.height = static_cast<float>(self->m_rhi->extent().height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = self->m_rhi->extent();
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                self->m_lightingPipelineLayout, 0, 1, &self->m_lightingDescriptorSet, 0, nullptr);

            LightingPush push{};
            push.lightPos = glm::vec4(self->m_scene->config().lightPos, 0.0f);
            push.viewPos = glm::vec4(self->m_camera->position(), 0.0f);
            push.displayMode = self->m_displayMode;
            vkCmdPushConstants(cmd, self->m_lightingPipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(LightingPush), &push);

            vkCmdDraw(cmd, 4, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
        };
    });

    // ---- Phase 2: Compile ----
    if (!m_renderGraph->compile()) {
        fatalError("RenderGraph compile failed");
    }

    // ---- Phase 3: Create VK objects ----
    m_gbufferPass->create(m_scene, m_camera, m_renderGraph.get());
    buildLightingPipelineAndDescriptors();

    spdlog::info("DeferredShading initialized (GBufferPass extracted)");
}

RenderGraph::ResourceHandle DeferredShading::albedoHandle() const {
    return m_gbufferPass ? m_gbufferPass->albedoHandle() : RenderGraph::InvalidResource;
}
RenderGraph::ResourceHandle DeferredShading::normalHandle() const {
    return m_gbufferPass ? m_gbufferPass->normalHandle() : RenderGraph::InvalidResource;
}
RenderGraph::ResourceHandle DeferredShading::materialHandle() const {
    return m_gbufferPass ? m_gbufferPass->materialHandle() : RenderGraph::InvalidResource;
}
RenderGraph::ResourceHandle DeferredShading::depthHandle() const {
    return m_gbufferPass ? m_gbufferPass->depthHandle() : RenderGraph::InvalidResource;
}

void DeferredShading::buildLightingPipelineAndDescriptors() {
    VkImageView albedoView = m_renderGraph->getImageView(m_gbufferPass->albedoHandle());
    VkImageView normalView = m_renderGraph->getImageView(m_gbufferPass->normalHandle());

    // Lighting Pipeline
    {
        m_lightingPipelineCache = std::make_unique<PipelineCache>(m_rhi->ctx());
        PipelineBuilder builder(m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache());
        builder.shader("shaders/lighting.frag.spv")
               .shader("shaders/lighting.vert.spv")
               .renderPass(m_rhi->renderPass())
               .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
               .cullMode(VK_CULL_MODE_NONE)
               .depthTest(false)
               .depthWrite(false);
        auto result = builder.build(*m_lightingPipelineCache);
        m_lightingPipeline = result.pipeline->handle();
        m_lightingPipelineLayoutObj = std::move(result.layout);
        m_lightingPipelineLayout = m_lightingPipelineLayoutObj->handle();
    }

    // Lighting Descriptor Set
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(m_rhi->ctx().device(), &samplerInfo, nullptr, &m_lightingSampler));

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = 2;
        dslInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(m_rhi->ctx().device(), &dslInfo, nullptr, &m_lightingDescriptorSetLayout));

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 2;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(m_rhi->ctx().device(), &poolInfo, nullptr, &m_lightingDescriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_lightingDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_lightingDescriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_rhi->ctx().device(), &allocInfo, &m_lightingDescriptorSet));

        VkDescriptorImageInfo imageInfos[2]{};
        imageInfos[0].sampler = m_lightingSampler;
        imageInfos[0].imageView = albedoView;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].sampler = m_lightingSampler;
        imageInfos[1].imageView = normalView;
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2]{};
        for (int i = 0; i < 2; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_lightingDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(m_rhi->ctx().device(), 2, writes, 0, nullptr);
    }
}

} // namespace kazu
