// ============================================================================
// KazuEngine - Pass Layer: Lighting Pass (Implementation)
// ============================================================================

#include "pass/LightingPass.h"
#include "core/Utils.h"
#include "core/Path.h"
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
    glm::mat4 invViewProj;
    glm::vec4 lightPos;
    glm::vec4 viewPos;
    int displayMode;
};

} // anonymous namespace

LightingPass::LightingPass() = default;

LightingPass::~LightingPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    if (m_descriptorPool)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout)
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    if (m_sampler)
        vkDestroySampler(device, m_sampler, nullptr);
}

void LightingPass::setInputs(RenderGraph::ResourceHandle albedo,
                                RenderGraph::ResourceHandle normal,
                                RenderGraph::ResourceHandle depth) {
    m_albedoHandle = albedo;
    m_normalHandle = normal;
    m_depthHandle = depth;
}

void LightingPass::declare(RHI* rhi, RenderGraph* rg) {
    m_rhi = rhi;

    LightingPass* self = this;
    rg->addPass("Lighting", [&](RenderGraph::PassBuilder& b) {
        b.read(self->m_albedoHandle);
        b.read(self->m_normalHandle);
        b.read(self->m_depthHandle);
        b.execute = [self](VkCommandBuffer cmd) {
            self->execute(cmd);
        };
    });
}

void LightingPass::create(Scene* scene, Camera* camera, RenderGraph* rg) {
    m_scene = scene;
    m_camera = camera;

    VkImageView albedoView = rg->getImageView(m_albedoHandle);
    VkImageView normalView = rg->getImageView(m_normalHandle);

    // ---- Pipeline ----
    {
        m_pipelineCache = std::make_unique<PipelineCache>(m_rhi->ctx());
        PipelineBuilder builder(m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache());
        builder.shader(kazu::Path::resolveShader("lighting.frag.spv"))
               .shader(kazu::Path::resolveShader("lighting.vert.spv"))
               .renderPass(m_rhi->renderPass())
               .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
               .cullMode(VK_CULL_MODE_NONE)
               .depthTest(false)
               .depthWrite(false);
        auto result = builder.build(*m_pipelineCache);
        m_pipeline = result.pipeline->handle();
        m_pipelineLayoutObj = std::move(result.layout);
        m_pipelineLayout = m_pipelineLayoutObj->handle();
    }

    // ---- Descriptor Set ----
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(m_rhi->ctx().device(), &samplerInfo, nullptr, &m_sampler));

        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = 3;
        dslInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(m_rhi->ctx().device(), &dslInfo, nullptr, &m_descriptorSetLayout));

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 3;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(m_rhi->ctx().device(), &poolInfo, nullptr, &m_descriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_rhi->ctx().device(), &allocInfo, &m_descriptorSet));

        VkImageView depthView = rg->getImageView(m_depthHandle);

        VkDescriptorImageInfo imageInfos[3]{};
        imageInfos[0].sampler = m_sampler;
        imageInfos[0].imageView = albedoView;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].sampler = m_sampler;
        imageInfos[1].imageView = normalView;
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].sampler = m_sampler;
        imageInfos[2].imageView = depthView;
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[3]{};
        for (int i = 0; i < 3; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_descriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(m_rhi->ctx().device(), 3, writes, 0, nullptr);
    }
}

void LightingPass::execute(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_rhi->renderPass();
    rpInfo.framebuffer = m_rhi->framebuffer(m_currentImageIndex);
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_rhi->extent();
    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_rhi->extent().width);
    viewport.height = static_cast<float>(m_rhi->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = m_rhi->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    LightingPush push{};
    float aspect = static_cast<float>(m_rhi->extent().width) / m_rhi->extent().height;
    glm::mat4 view = m_camera->getViewMatrix();
    glm::mat4 proj = m_camera->getProjectionMatrix(aspect);
    push.invViewProj = glm::inverse(proj * view);
    push.lightPos = glm::vec4(m_scene->config().lightPos, 0.0f);
    push.viewPos = glm::vec4(m_camera->position(), 0.0f);
    push.displayMode = m_displayMode;
    vkCmdPushConstants(cmd, m_pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(LightingPush), &push);

    vkCmdDraw(cmd, 4, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace kazu
