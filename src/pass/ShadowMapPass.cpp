#include "pass/ShadowMapPass.h"
#include "rhi/RHI.h"
#include "rhi/ShaderEffect.h"
#include "core/Utils.h"
#include "core/Path.h"
#include "rhi/Mesh.h"
#include "scene/Light.h"
#include "scene/Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include "rhi/Camera.h"

namespace kazu{

namespace {

struct ShadowMapPush {
    glm::mat4 mvp;
};

} // anonymous namespace

void ShadowMapPass::declare(RHI* rhi, RenderGraph* rg) {
    m_shadowMapHandle = rg -> addTexture("ShadowMap", {
        shadowMapSize, shadowMapSize, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
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

    const PointLight* pointLight = nullptr;
    if (!m_scene->pointLights().empty()) {
        pointLight = &m_scene->pointLights()[0];
    }
    if (!pointLight) {
        spdlog::warn("ShadowMapPass: No point light found, skipping shadow map rendering.");
        return;
    }

    glm::vec3 lightPos = pointLight->position;
    glm::vec3 lightTarget = m_scene->bounds().center();
    glm::mat4 view = glm::lookAt(lightPos, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    // Point light looking at scene center, perspective projection.
    float fov = glm::radians(90.0f);
    float aspect = 1.0f;
    float lightDist = glm::length(lightPos - lightTarget);
    float sceneRadius = m_scene->bounds().isValid() ? m_scene->bounds().radius() : 10.0f;
    float zNear = glm::max(0.01f, 0.1f * lightDist);
    float zFar = glm::max(lightDist + sceneRadius, 2.0f * lightDist);
    glm::mat4 proj = glm::perspective(fov, aspect, zNear, zFar);

    // ShadowMapPass only needs geometry; it does not sample material textures.
    for (const auto& inst : m_scene->instances()) {
        if (!inst.mesh) continue;

        ShadowMapPush push{};
        push.mvp = proj * view * inst.transform;
        vkCmdPushConstants(cmd, m_effect->pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(ShadowMapPush), &push);
        inst.mesh->draw(cmd);
    }
    vkCmdEndRenderPass(cmd);
}

}