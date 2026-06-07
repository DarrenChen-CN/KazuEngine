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

    destroyRenderPassAndFramebuffers();

    if (m_descriptorPool)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout)
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    if (m_sampler)
        vkDestroySampler(device, m_sampler, nullptr);
}

void LightingPass::destroyRenderPassAndFramebuffers() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    for (auto fb : m_framebuffers) {
        if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    }
    m_framebuffers.clear();
    if (m_renderPass) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
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
        b.writeColor(0, self->m_swapchainHandle);
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

    // ---- RenderPass & Framebuffers ----
    createRenderPassAndFramebuffers();

    // ---- ShaderEffect ----
    {
        ShaderEffect::Key key;
        key.shaderPaths = {
            kazu::Path::resolveShader("lighting.vert.spv"),
            kazu::Path::resolveShader("lighting.frag.spv")
        };
        key.state.renderPass = m_renderPass;
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

void LightingPass::createRenderPassAndFramebuffers() {
    VkDevice device = m_rhi->ctx().device();
    VkExtent2D extent = m_rhi->extent();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_rhi->swapchainFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass));

    // Create per-swapchain-image framebuffers
    uint32_t imageCount = m_rhi->swapchainImageCount();
    m_framebuffers.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageView attachment = m_rhi->swapchainImageView(i);
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &m_framebuffers[i]));
    }
}

void LightingPass::execute(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderPass;
    rpInfo.framebuffer = m_framebuffers[m_currentImageIndex];
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
    glm::mat4 view = m_camera->getViewMatrix();
    glm::mat4 proj = m_camera->getProjectionMatrix(aspect);
    push.invViewProj = glm::inverse(proj * view);
    push.lightPos = glm::vec4(m_scene->config().lightPos, 0.0f);
    push.viewPos = glm::vec4(m_camera->position(), 0.0f);
    push.displayMode = m_displayMode;
    vkCmdPushConstants(cmd, m_effect->pipelineLayout(),
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(LightingPush), &push);

    vkCmdDraw(cmd, 4, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

} // namespace kazu
