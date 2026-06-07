// ============================================================================
// KazuEngine - Pass Layer: GBuffer Pass (Implementation)
// ============================================================================

#include "pass/GBufferPass.h"
#include "core/Utils.h"
#include "core/Path.h"
#include "rhi/RHI.h"
#include "rhi/ShaderEffect.h"
#include "rhi/Camera.h"
#include "rhi/Mesh.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"
#include <glm/glm.hpp>
#include <array>

namespace kazu {

GBufferPass::GBufferPass() = default;

GBufferPass::~GBufferPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    if (m_framebuffer)
        vkDestroyFramebuffer(device, m_framebuffer, nullptr);
    if (m_renderPass)
        vkDestroyRenderPass(device, m_renderPass, nullptr);
}

void GBufferPass::declare(RHI* rhi, RenderGraph* rg) {
    m_rhi = rhi;

    m_albedoHandle = rg->addTexture("Albedo",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    m_normalHandle = rg->addTexture("Normal",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    m_materialHandle = rg->addTexture("Material",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    m_depthHandle = rg->addTexture("Depth",
        {m_rhi->extent().width, m_rhi->extent().height, VK_FORMAT_D32_SFLOAT,
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    GBufferPass* self = this;
    rg->addPass("GBuffer", [&](RenderGraph::PassBuilder& b) {
        b.writeColor(0, self->m_albedoHandle);
        b.writeColor(1, self->m_normalHandle);
        b.writeColor(2, self->m_materialHandle);
        b.writeDepth(self->m_depthHandle);
        b.execute = [self](VkCommandBuffer cmd) {
            self->execute(cmd);
        };
    });
}

void GBufferPass::create(Scene* scene, Camera* camera, RenderGraph* rg) {
    m_scene = scene;
    m_camera = camera;

    VkImageView albedoView   = rg->getImageView(m_albedoHandle);
    VkImageView normalView   = rg->getImageView(m_normalHandle);
    VkImageView materialView = rg->getImageView(m_materialHandle);
    VkImageView depthView    = rg->getImageView(m_depthHandle);

    // ---- RenderPass & Framebuffer ----
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
        VK_CHECK(vkCreateRenderPass(m_rhi->ctx().device(), &rpInfo, nullptr, &m_renderPass));

        VkImageView fbViews[4] = {albedoView, normalView, materialView, depthView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = 4;
        fbInfo.pAttachments = fbViews;
        fbInfo.width = m_rhi->extent().width;
        fbInfo.height = m_rhi->extent().height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(m_rhi->ctx().device(), &fbInfo, nullptr, &m_framebuffer));
    }

    // ---- ShaderEffect (replaces PipelineBuilder) ----
    {
        ShaderEffect::Key key;
        key.shaderPaths = {
            kazu::Path::resolveShader("triangle.vert.spv"),
            kazu::Path::resolveShader("gbuffer.frag.spv")
        };
        key.state.renderPass = m_renderPass;
        key.state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        key.state.cullMode = VK_CULL_MODE_BACK_BIT;
        key.state.depthTest = true;
        key.state.depthWrite = true;
        key.state.depthCompareOp = VK_COMPARE_OP_LESS;
        key.state.vertexBindings = {Vertex::getBindingDescription()};
        key.state.vertexAttributes = Vertex::getAttributeDescriptions();

        m_effect = ShaderEffect::getOrCreate(
            m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache(),
            m_rhi->pipelineCache(), key);
    }
}

void GBufferPass::execute(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderPass;
    rpInfo.framebuffer = m_framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_rhi->extent();
    std::array<VkClearValue, 4> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].color = {{0.5f, 0.5f, 1.0f, 1.0f}};
    clears[2].color = {{0.0f, 0.5f, 1.0f, 1.0f}};
    clears[3].depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = 4;
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

    glm::mat4 viewProj = m_camera->getProjectionMatrix(m_rhi->aspect())
                       * m_camera->getViewMatrix();
    glm::vec4 lightPos = glm::vec4(m_scene->config().lightPos, 0.0f);
    glm::vec4 viewPos = glm::vec4(m_camera->position(), 0.0f);

    m_scene->draw(cmd, m_effect->pipelineLayout(),
                  viewProj, lightPos, viewPos, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace kazu
