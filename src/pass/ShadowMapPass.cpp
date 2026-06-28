#include "pass/ShadowMapPass.h"
#include "rhi/RHI.h"
#include "rhi/ShaderEffect.h"
#include "core/Utils.h"
#include "core/Path.h"
#include "rhi/Mesh.h"
#include "scene/Light.h"
#include "scene/Scene.h"
#include "scene/ShadowCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include "rhi/Camera.h"

namespace kazu{

namespace {

struct ShadowMapPush {
    glm::mat4 mvp;
};

} // anonymous namespace

void ShadowMapPass::declare(RHI* rhi, RenderGraph* rg) {
    m_shadowMapHandle = rg->addTexture("ShadowMap", {
        .width = shadowMapSize,
        .height = shadowMapSize,
        .format = VK_FORMAT_D32_SFLOAT,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    });

    m_passHandle = rg -> addPass("ShadowMap", [&](RenderGraph::PassBuilder& b){
        b.writeDepth(m_shadowMapHandle);
        b.execute = [this](const PassExecuteContext& ctx){
            this -> execute(ctx);
        };
    });
}

void ShadowMapPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_scene = ctx.scene;
    m_renderGraph = ctx.renderGraph;

    // Shader Effect
    {
        ShaderEffect::Key key;
        key.shaderPaths = {
            kazu::Path::resolveShader("shadowmap.vert.spv"),
            kazu::Path::resolveShader("shadowmap.frag.spv")
        };
        key.state.renderPass = m_renderGraph->getRenderPass(m_passHandle);
        key.state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        key.state.cullMode = VK_CULL_MODE_NONE;
        key.state.depthTest = true;
        key.state.depthWrite = true;
        key.state.depthCompareOp = VK_COMPARE_OP_LESS;
        key.state.vertexBindings = {Vertex::getBindingDescription()};
        key.state.vertexAttributes = Vertex::getAttributeDescriptions();
        m_effect = ShaderEffect::getOrCreate(ctx.rhi->ctx(), ctx.rhi->shaderLib(),
                                            ctx.rhi->dslCache(), ctx.rhi->pipelineCache(), key);
    }
     
}

void ShadowMapPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderGraph->getRenderPass(m_passHandle);
    rpInfo.framebuffer = m_renderGraph->getFramebuffer(m_passHandle, ctx.imageIndex);
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {shadowMapSize, shadowMapSize};
    VkClearValue clear{};
    clear.depthStencil.depth = 1.0f;
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_effect->pipeline());

    VkViewport viewport{};
    viewport.width = shadowMapSize;
    viewport.height = shadowMapSize;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {shadowMapSize, shadowMapSize};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    ShadowCamera shadowCamera = selectShadowCamera(
        m_scene->directionalLight(),
        m_scene->pointLights(),
        m_scene->areaLights(),
        m_scene->bounds());
    if (!shadowCamera.valid) {
        spdlog::warn("ShadowMapPass: No shadow-casting light found, skipping shadow map rendering.");
        vkCmdEndRenderPass(cmd);
        return;
    }

    // ShadowMapPass only needs geometry; it does not sample material textures.
    for (const auto& inst : m_scene->instances()) {
        if (!inst.mesh || inst.unlit) continue;

        ShadowMapPush push{};
        push.mvp = shadowCamera.viewProj * inst.transform;
        vkCmdPushConstants(cmd, m_effect->pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(ShadowMapPush), &push);
        inst.mesh->draw(cmd);
    }
    vkCmdEndRenderPass(cmd);
}

}
