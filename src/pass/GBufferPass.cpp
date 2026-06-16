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

namespace {

struct GBufferPush {
    glm::mat4 mvp;
};

} // anonymous namespace

GBufferPass::GBufferPass() = default;

GBufferPass::~GBufferPass() = default;

void GBufferPass::declare(RHI* rhi, RenderGraph* rg) {
    m_rhi = rhi;
    m_renderGraph = rg;

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
    m_passHandle = rg->addPass("GBuffer", [&](RenderGraph::PassBuilder& b) {
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
    m_renderGraph = rg;

    // ---- ShaderEffect (replaces PipelineBuilder) ----
    {
        ShaderEffect::Key key;
        key.shaderPaths = {
            kazu::Path::resolveShader("triangle.vert.spv"),
            kazu::Path::resolveShader("gbuffer.frag.spv")
        };
        key.state.renderPass = m_renderGraph->getRenderPass(m_passHandle);
        key.state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        // Current OBJ assets may contain mixed winding; back-face culling drops valid triangles.
        key.state.cullMode = VK_CULL_MODE_NONE;
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
    rpInfo.renderPass = m_renderGraph->getRenderPass(m_passHandle);
    rpInfo.framebuffer = m_renderGraph->getFramebuffer(m_passHandle);
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_rhi->extent();
    std::array<VkClearValue, 4> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].color = {{0.5f, 0.5f, 1.0f, 1.0f}}; // Normal buffer default to flat blue (0,0,1)
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

    m_scene->draw(cmd, m_effect->pipelineLayout(),
        [&](VkCommandBuffer drawCmd, VkPipelineLayout layout, const ModelInstance& inst) {
            GBufferPush push{};
            push.mvp = viewProj * inst.transform;
            vkCmdPushConstants(drawCmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(GBufferPush), &push);
        });
    vkCmdEndRenderPass(cmd);
}

} // namespace kazu
