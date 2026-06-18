// ============================================================================
// KazuEngine - Pass Layer: Lighting Pass (Implementation)
// ============================================================================

#include "pass/LightingPass.h"
#include "core/Utils.h"
#include "core/Path.h"
#include "rhi/RHI.h"
#include "rhi/ShaderEffect.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace kazu {

namespace {

struct LightingPush {
    glm::mat4 invViewProj;
    glm::mat4 lightViewProj;
    glm::vec4 lightDirection;
    glm::vec4 lightColorIntensity;
    glm::vec4 viewPos;
    float     shadowBias;
    float     pcfFilterSize;
    float     lightWidth;
    int       pcfSampleCount;
    int       shadowMode;
    int       debugView;
    int       lightingModel;
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
                                RenderGraph::ResourceHandle depth,
                                RenderGraph::ResourceHandle shadowMap) {
    m_albedoHandle = albedo;
    m_normalHandle = normal;
    m_depthHandle = depth;
    m_shadowMapHandle = shadowMap;
}

void LightingPass::declare(RHI* rhi, RenderGraph* rg) {
    m_sceneColorHandle = rg->addTexture("SceneColorHDR",
        {rhi->extent().width, rhi->extent().height, VK_FORMAT_R16G16B16A16_SFLOAT,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    LightingPass* self = this;
    m_passHandle = rg->addPass("Lighting", [&](RenderGraph::PassBuilder& b) {
        b.read(self->m_albedoHandle);
        b.read(self->m_normalHandle);
        b.read(self->m_depthHandle);
        if(self->m_shadowMapHandle != RenderGraph::InvalidResource)
            b.read(self->m_shadowMapHandle);
        b.writeColor(0, self->m_sceneColorHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void LightingPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_scene = ctx.scene;
    m_renderGraph = ctx.renderGraph;

    VkImageView albedoView = m_renderGraph->getImageView(m_albedoHandle);
    VkImageView normalView = m_renderGraph->getImageView(m_normalHandle);

    // ---- ShaderEffect ----
    {
        ShaderEffect::Key key;
        key.shaderPaths = {
            kazu::Path::resolveShader("lighting.vert.spv"),
            kazu::Path::resolveShader("lighting.frag.spv")
        };
        key.state.renderPass = m_renderGraph->getRenderPass(m_passHandle);
        key.state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        key.state.cullMode = VK_CULL_MODE_NONE;
        key.state.depthTest = false;
        key.state.depthWrite = false;
        m_effect = ShaderEffect::getOrCreate(
            m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache(),
            m_rhi->pipelineCache(), key);
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

        VkDescriptorSetLayoutBinding bindings[4]{};
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
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = 4;
        dslInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(m_rhi->ctx().device(), &dslInfo, nullptr, &m_descriptorSetLayout));

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 4;
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

        VkImageView depthView = m_renderGraph->getImageView(m_depthHandle);
        VkImageView shadowMapView = (m_shadowMapHandle != RenderGraph::InvalidResource)
            ? m_renderGraph->getImageView(m_shadowMapHandle)
            : depthView;

        VkDescriptorImageInfo imageInfos[4]{};
        imageInfos[0].sampler = m_sampler;
        imageInfos[0].imageView = albedoView;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].sampler = m_sampler;
        imageInfos[1].imageView = normalView;
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].sampler = m_sampler;
        imageInfos[2].imageView = depthView;
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfos[3].sampler = m_sampler;
        imageInfos[3].imageView = shadowMapView;
        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[4]{};
        for (int i = 0; i < 4; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_descriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(m_rhi->ctx().device(), 4, writes, 0, nullptr);
    }
}

void LightingPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderGraph->getRenderPass(m_passHandle);
    rpInfo.framebuffer = m_renderGraph->getFramebuffer(m_passHandle, ctx.imageIndex);
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_rhi->extent();
    std::array<VkClearValue, 1> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_effect->pipeline());

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
        m_effect->pipelineLayout(), 0, 1, &m_descriptorSet, 0, nullptr);

    LightingPush push{};
    float aspect = static_cast<float>(m_rhi->extent().width) / m_rhi->extent().height;
    glm::mat4 view = ctx.camera->getViewMatrix();
    glm::mat4 proj = ctx.camera->getProjectionMatrix(aspect);
    push.invViewProj = glm::inverse(proj * view);

    // Use the same point-light-to-scene-center direction for both shadow and lighting.
    glm::vec3 lightDir = m_scene->directionalLight().direction;
    glm::vec3 lightPos = m_scene->directionalLight().direction * -3.0f;
    if (!m_scene->pointLights().empty()) {
        const auto& point = m_scene->pointLights()[0];
        lightPos = point.position;
        lightDir = glm::normalize(m_scene->bounds().center() - point.position);
    }
    push.lightDirection = glm::vec4(lightDir, 0.0f);
    push.lightColorIntensity = glm::vec4(m_scene->directionalLight().color,
                                         m_scene->directionalLight().intensity);

    // Build light view/proj to match ShadowMapPass.
    float sceneRadius = m_scene->bounds().isValid() ? m_scene->bounds().radius() : 10.0f;
    float lightDist = glm::length(lightPos - m_scene->bounds().center());
    float zNear = glm::max(0.01f, 0.1f * lightDist);
    float zFar = glm::max(lightDist + sceneRadius, 2.0f * lightDist);
    glm::mat4 lightView = glm::lookAt(lightPos, m_scene->bounds().center(), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 lightProj = glm::perspective(glm::radians(90.0f), 1.0f, zNear, zFar);
    push.lightViewProj = lightProj * lightView;

    push.viewPos = glm::vec4(ctx.camera->position(), 0.0f);
    push.shadowBias = m_settings.shadowBias;
    push.pcfFilterSize = m_settings.pcfFilterSize;
    push.lightWidth = m_settings.lightWidth;
    push.pcfSampleCount = m_settings.pcfSampleCount;
    push.shadowMode = m_settings.shadowMode;
    push.debugView = m_settings.debugView;
    push.lightingModel = m_settings.lightingModel;
    vkCmdPushConstants(cmd, m_effect->pipelineLayout(),
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(LightingPush), &push);

    vkCmdDraw(cmd, 4, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace kazu
