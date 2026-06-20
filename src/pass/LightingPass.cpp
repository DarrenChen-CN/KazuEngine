// ============================================================================
// KazuEngine - Pass Layer: Lighting Pass (Implementation)
// ============================================================================

#include "pass/LightingPass.h"
#include "core/Utils.h"
#include "core/Path.h"
#include "core/Image.h"
#include "core/CommandBuffer.h"
#include "rhi/RHI.h"
#include "rhi/Texture.h"
#include "rhi/ShaderEffect.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "scene/ShadowCamera.h"
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
    int       iblEnabled;
    int       envEnabled;
    int       ssaoEnabled;
};

std::unique_ptr<Texture> createBlackTextureCube(Context& ctx, VkFormat format) {
    ImageDesc desc{};
    desc.type = VK_IMAGE_TYPE_2D;
    desc.extent = {1, 1, 1};
    desc.mipLevels = 1;
    desc.arrayLayers = 6;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    auto image = std::make_unique<Image>(ctx, desc);

    CommandBuffer cmd(ctx, ctx.transientPool());
    cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue clear{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
    vkCmdClearColorImage(cmd.handle(), image->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cmd.end();
    cmd.submit(ctx.graphicsQueue());
    vkQueueWaitIdle(ctx.graphicsQueue());

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return std::make_unique<Texture>(ctx, std::move(image), samplerInfo);
}

std::unique_ptr<Texture> createBlackTexture2D(Context& ctx, VkFormat format) {
    ImageDesc desc{};
    desc.type = VK_IMAGE_TYPE_2D;
    desc.extent = {1, 1, 1};
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    auto image = std::make_unique<Image>(ctx, desc);

    CommandBuffer cmd(ctx, ctx.transientPool());
    cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue clear{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd.handle(), image->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cmd.end();
    cmd.submit(ctx.graphicsQueue());
    vkQueueWaitIdle(ctx.graphicsQueue());

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return std::make_unique<Texture>(ctx, std::move(image), samplerInfo);
}

std::unique_ptr<Texture> createWhiteTexture2D(Context& ctx, VkFormat format) {
    ImageDesc desc{};
    desc.type = VK_IMAGE_TYPE_2D;
    desc.extent = {1, 1, 1};
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    auto image = std::make_unique<Image>(ctx, desc);

    CommandBuffer cmd(ctx, ctx.transientPool());
    cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue clear{};
    clear.float32[0] = 1.0f;
    clear.float32[1] = 1.0f;
    clear.float32[2] = 1.0f;
    clear.float32[3] = 1.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd.handle(), image->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    image->transitionLayout(cmd.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cmd.end();
    cmd.submit(ctx.graphicsQueue());
    vkQueueWaitIdle(ctx.graphicsQueue());

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return std::make_unique<Texture>(ctx, std::move(image), samplerInfo);
}

} // anonymous namespace

LightingPass::LightingPass() = default;

LightingPass::~LightingPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    if (m_descriptorPool)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    // m_descriptorSetLayout is borrowed from ShaderEffect's cache; do not destroy.
    if (m_sampler)
        vkDestroySampler(device, m_sampler, nullptr);
}

void LightingPass::setInputs(RenderGraph::ResourceHandle albedo,
                                RenderGraph::ResourceHandle normal,
                                RenderGraph::ResourceHandle depth,
                                RenderGraph::ResourceHandle material,
                                RenderGraph::ResourceHandle shadowMap,
                                RenderGraph::ResourceHandle ssao) {
    m_albedoHandle = albedo;
    m_normalHandle = normal;
    m_depthHandle = depth;
    m_materialHandle = material;
    m_shadowMapHandle = shadowMap;
    m_ssaoHandle = ssao;
}

void LightingPass::setIBL(Texture* irradiance, Texture* prefilter, Texture* brdfLut) {
    m_irradianceMap = irradiance;
    m_prefilterMap = prefilter;
    m_brdfLut = brdfLut;
    m_iblEnabled = (irradiance != nullptr && prefilter != nullptr && brdfLut != nullptr);
}

void LightingPass::setEnvironment(Texture* environmentCube) {
    m_environmentMap = environmentCube;
    m_envEnabled = (environmentCube != nullptr);
}

void LightingPass::declare(RHI* rhi, RenderGraph* rg) {
    m_sceneColorHandle = rg->addTexture("SceneColorHDR",
        {.width = rhi->extent().width,
         .height = rhi->extent().height,
         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    LightingPass* self = this;
    m_passHandle = rg->addPass("Lighting", [&](RenderGraph::PassBuilder& b) {
        b.read(self->m_albedoHandle);
        b.read(self->m_normalHandle);
        b.read(self->m_depthHandle);
        b.read(self->m_materialHandle);
        if(self->m_shadowMapHandle != RenderGraph::InvalidResource)
            b.read(self->m_shadowMapHandle);
        if(self->m_ssaoHandle != RenderGraph::InvalidResource)
            b.read(self->m_ssaoHandle);
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
    VkImageView materialView = m_renderGraph->getImageView(m_materialHandle);

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
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 32.0f;
        VK_CHECK(vkCreateSampler(m_rhi->ctx().device(), &samplerInfo, nullptr, &m_sampler));

        // Use real IBL textures if provided; otherwise create tiny black placeholders
        // so the descriptor set is still valid (they won't be sampled when IBL is off).
        if (!m_irradianceMap) m_dummyIrradiance = createBlackTextureCube(m_rhi->ctx(), VK_FORMAT_R16G16B16A16_SFLOAT);
        if (!m_prefilterMap)  m_dummyPrefilter  = createBlackTextureCube(m_rhi->ctx(), VK_FORMAT_R16G16B16A16_SFLOAT);
        if (!m_brdfLut)       m_dummyLut        = createBlackTexture2D(m_rhi->ctx(), VK_FORMAT_R8G8B8A8_UNORM);
        if (!m_environmentMap) m_dummyEnv       = createBlackTextureCube(m_rhi->ctx(), VK_FORMAT_R16G16B16A16_SFLOAT);
        if (!m_ssaoHandle)     m_dummySSAO      = createWhiteTexture2D(m_rhi->ctx(), VK_FORMAT_R8_UNORM);

        Texture* irradianceTex = m_irradianceMap ? m_irradianceMap : m_dummyIrradiance.get();
        Texture* prefilterTex  = m_prefilterMap  ? m_prefilterMap  : m_dummyPrefilter.get();
        Texture* lutTex        = m_brdfLut       ? m_brdfLut       : m_dummyLut.get();
        Texture* envTex        = m_environmentMap ? m_environmentMap : m_dummyEnv.get();

        m_descriptorSetLayout = m_effect->descriptorSetLayout();

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 10;
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
        VkImageView ssaoView = (m_ssaoHandle != RenderGraph::InvalidResource)
            ? m_renderGraph->getImageView(m_ssaoHandle)
            : m_dummySSAO->image()->view();

        VkDescriptorImageInfo imageInfos[10]{};
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
        imageInfos[4].sampler = m_sampler;
        imageInfos[4].imageView = materialView;
        imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[5].sampler = m_sampler;
        imageInfos[5].imageView = irradianceTex->view();
        imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[6].sampler = m_sampler;
        imageInfos[6].imageView = prefilterTex->view();
        imageInfos[6].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[7].sampler = m_sampler;
        imageInfos[7].imageView = lutTex->view();
        imageInfos[7].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[8].sampler = m_sampler;
        imageInfos[8].imageView = envTex->view();
        imageInfos[8].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[9].sampler = m_sampler;
        imageInfos[9].imageView = ssaoView;
        imageInfos[9].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[10]{};
        for (int i = 0; i < 10; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_descriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &imageInfos[i];
        }
        vkUpdateDescriptorSets(m_rhi->ctx().device(), 10, writes, 0, nullptr);
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

    ShadowCamera shadowCamera = selectShadowCamera(
        m_scene->directionalLight(),
        m_scene->pointLights(),
        m_scene->bounds());
    if (!shadowCamera.valid) {
        shadowCamera = buildDirectionalShadowCamera(m_scene->directionalLight(), m_scene->bounds());
        shadowCamera.valid = true;
    }
    push.lightDirection = glm::vec4(shadowCamera.lightDirection, 0.0f);
    push.lightColorIntensity = glm::vec4(shadowCamera.color, shadowCamera.intensity);
    push.lightViewProj = shadowCamera.viewProj;

    push.viewPos = glm::vec4(ctx.camera->position(), 0.0f);
    push.shadowBias = m_settings.shadowBias;
    push.pcfFilterSize = m_settings.pcfFilterSize;
    push.lightWidth = m_settings.lightWidth;
    push.pcfSampleCount = m_settings.pcfSampleCount;
    push.shadowMode = m_settings.shadowMode;
    push.debugView = m_settings.debugView;
    push.lightingModel = m_settings.lightingModel;
    push.iblEnabled = m_iblEnabled ? 1 : 0;
    push.envEnabled = m_envEnabled ? 1 : 0;
    push.ssaoEnabled = m_settings.enableSSAO ? 1 : 0;
    vkCmdPushConstants(cmd, m_effect->pipelineLayout(),
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(LightingPush), &push);

    vkCmdDraw(cmd, 4, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace kazu
