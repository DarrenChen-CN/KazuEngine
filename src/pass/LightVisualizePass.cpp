// ============================================================================
// KazuEngine - Pass Layer: Light Visualize Pass (Implementation)
// ============================================================================

#include "pass/LightVisualizePass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/ShaderEffect.h"
#include "rhi/Mesh.h"
#include "rhi/Camera.h"
#include "scene/Scene.h"
#include "rendergraph/RenderGraph.h"
#include <glm/gtc/matrix_transform.hpp>

namespace kazu {

namespace {

struct LightVisualizePush {
    glm::mat4 mvp;
    glm::vec4 color;
};

} // anonymous namespace

LightVisualizePass::~LightVisualizePass() {
    if (!m_rhi) return;
    vkDeviceWaitIdle(m_rhi->ctx().device());
}

void LightVisualizePass::declare(RHI* rhi, RenderGraph* rg) {
    LightVisualizePass* self = this;
    m_passHandle = rg->addPass("LightVisualize", [&](RenderGraph::PassBuilder& b) {
        if (self->m_sceneColorHandle != RenderGraph::InvalidResource) {
            b.read(self->m_sceneColorHandle);
            b.writeColor(0, self->m_sceneColorHandle);
        }
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void LightVisualizePass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_scene = ctx.scene;
    m_renderGraph = ctx.renderGraph;

    ShaderEffect::Key key;
    key.shaderPaths = {
        kazu::Path::resolveShader("lightvisualize.vert.spv"),
        kazu::Path::resolveShader("lightvisualize.frag.spv")
    };
    key.state.renderPass = m_renderGraph->getRenderPass(m_passHandle);
    key.state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.state.cullMode = VK_CULL_MODE_NONE;
    key.state.depthTest = false;
    key.state.depthWrite = false;
    ColorBlendAttachment blend{};
    blend.blendEnable = true;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    key.state.colorBlendAttachments.push_back(blend);
    key.state.vertexBindings = {Vertex::getBindingDescription()};
    key.state.vertexAttributes = Vertex::getAttributeDescriptions();
    m_effect = ShaderEffect::getOrCreate(
        m_rhi->ctx(), m_rhi->shaderLib(), m_rhi->dslCache(),
        m_rhi->pipelineCache(), key);
}

void LightVisualizePass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderGraph->getRenderPass(m_passHandle);
    rpInfo.framebuffer = m_renderGraph->getFramebuffer(m_passHandle, ctx.imageIndex);
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_rhi->extent();
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

    float aspect = static_cast<float>(m_rhi->extent().width) / m_rhi->extent().height;
    glm::mat4 viewProj = ctx.camera->getProjectionMatrix(aspect) * ctx.camera->getViewMatrix();

    for (const auto& inst : m_scene->instances()) {
        if (!inst.unlit || !inst.mesh) continue;

        LightVisualizePush push{};
        push.mvp = viewProj * inst.transform;
        push.color = inst.pendingBaseColorFactor;
        vkCmdPushConstants(cmd, m_effect->pipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(LightVisualizePush), &push);
        inst.mesh->draw(cmd);
    }

    vkCmdEndRenderPass(cmd);
}

} // namespace kazu
