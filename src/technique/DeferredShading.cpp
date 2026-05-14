// ============================================================================
// KazuEngine - Technique Layer: Deferred Shading (Implementation)
// ============================================================================

#include "technique/DeferredShading.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/PipelineBuilder.h"
#include "rhi/PipelineCache.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"
#include <glm/glm.hpp>
#include <array>

namespace kazu {

namespace {

struct GBufferPush {
    glm::mat4 mvp;
    glm::vec4 lightPos;
    glm::vec4 viewPos;
    int displayMode;
    int _pad[3];
};

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
    if (m_gbufferFramebuffer)
        vkDestroyFramebuffer(device, m_gbufferFramebuffer, nullptr);
    if (m_gbufferRenderPass)
        vkDestroyRenderPass(device, m_gbufferRenderPass, nullptr);
}

void DeferredShading::init(RHI* rhi, Scene* scene, Camera* camera) {
    m_rhi = rhi;
    m_scene = scene;
    m_camera = camera;

    // ---- Phase 1: Declare RenderGraph ----
    // Execute lambdas capture 'this' and read member vars at execution time.
    m_renderGraph = std::make_unique<RenderGraph>(m_rhi->ctx());

    auto albedoHandle = m_renderGraph->addTexture("Albedo",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    auto normalHandle = m_renderGraph->addTexture("Normal",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    auto materialHandle = m_renderGraph->addTexture("Material",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    auto depthHandle = m_renderGraph->addTexture("Depth",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_D32_SFLOAT,
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});

    DeferredShading* self = this;
    m_renderGraph->addPass("GBuffer", [&](RenderGraph::PassBuilder& b) {
        b.writeColor(0, albedoHandle);
        b.writeColor(1, normalHandle);
        b.writeColor(2, materialHandle);
        b.writeDepth(depthHandle);
        b.execute = [self](VkCommandBuffer cmd) {
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = self->m_gbufferRenderPass;
            rpInfo.framebuffer = self->m_gbufferFramebuffer;
            rpInfo.renderArea.offset = {0, 0};
            rpInfo.renderArea.extent = self->m_rhi->extent();
            std::array<VkClearValue, 4> clears{};
            clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clears[1].color = {{0.5f, 0.5f, 1.0f, 1.0f}};
            clears[2].color = {{0.0f, 0.5f, 1.0f, 1.0f}};
            clears[3].depthStencil = {1.0f, 0};
            rpInfo.clearValueCount = 4;
            rpInfo.pClearValues = clears.data();
            vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, self->m_gbufferPipeline);

            VkViewport viewport{};
            viewport.width = static_cast<float>(self->m_rhi->extent().width);
            viewport.height = static_cast<float>(self->m_rhi->extent().height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = self->m_rhi->extent();
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            GBufferPush push{};
            push.mvp = self->m_camera->getProjectionMatrix(self->m_rhi->aspect())
                     * self->m_camera->getViewMatrix();
            push.lightPos = glm::vec4(self->m_scene->config().lightPos, 0.0f);
            push.viewPos = glm::vec4(self->m_camera->position(), 0.0f);
            push.displayMode = 0;
            vkCmdPushConstants(cmd, self->m_gbufferPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(GBufferPush), &push);

            self->m_scene->draw(cmd, self->m_gbufferPipelineLayout);
            vkCmdEndRenderPass(cmd);
        };
    });

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

    // ---- Phase 2: Compile to allocate transient resources ----
    if (!m_renderGraph->compile()) {
        fatalError("RenderGraph compile failed");
    }

    // ---- Phase 3: Create Framebuffer / Pipelines / DescriptorSet ----
    m_albedoView = m_renderGraph->getImageView(albedoHandle);
    m_normalView = m_renderGraph->getImageView(normalHandle);
    VkImageView materialView = m_renderGraph->getImageView(materialHandle);
    VkImageView depthView    = m_renderGraph->getImageView(depthHandle);

    // GBuffer RenderPass & Framebuffer
    {
        VkAttachmentDescription attachments[4]{};
        for (int i = 0; i < 3; ++i) {
            attachments[i].format = VK_FORMAT_R8G8B8A8_UNORM;
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        attachments[3].format = VK_FORMAT_D32_SFLOAT;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRefs[3] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        };
        VkAttachmentReference depthRef = {3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 3;
        subpass.pColorAttachments = colorRefs;
        subpass.pDepthStencilAttachment = &depthRef;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 4;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        VK_CHECK(vkCreateRenderPass(m_rhi->ctx().device(), &rpInfo, nullptr, &m_gbufferRenderPass));

        VkImageView fbViews[4] = {m_albedoView, m_normalView, materialView, depthView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_gbufferRenderPass;
        fbInfo.attachmentCount = 4;
        fbInfo.pAttachments = fbViews;
        fbInfo.width = m_rhi->extent().width;
        fbInfo.height = m_rhi->extent().height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(m_rhi->ctx().device(), &fbInfo, nullptr, &m_gbufferFramebuffer));
    }

    // GBuffer Pipeline
    {
        static PipelineCache s_cache(m_rhi->ctx());
        PipelineBuilder builder(m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache());
        builder.shader("shaders/gbuffer.frag.spv")
               .shader("shaders/triangle.vert.spv")
               .renderPass(m_gbufferRenderPass);
        auto result = builder.build(s_cache);
        m_gbufferPipeline = result.pipeline->handle();
        m_gbufferPipelineLayout = result.layout->handle();
        (void)result.layout.release();
    }

    // Lighting Pipeline
    {
        static PipelineCache s_cache(m_rhi->ctx());
        PipelineBuilder builder(m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache());
        builder.shader("shaders/lighting.frag.spv")
               .shader("shaders/lighting.vert.spv")
               .renderPass(m_rhi->renderPass());
        auto result = builder.build(s_cache);
        m_lightingPipeline = result.pipeline->handle();
        m_lightingPipelineLayout = result.layout->handle();
        (void)result.layout.release();
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
        imageInfos[0].imageView = m_albedoView;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].sampler = m_lightingSampler;
        imageInfos[1].imageView = m_normalView;
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

    spdlog::info("DeferredShading initialized (GBuffer + Lighting)");
}

} // namespace kazu
