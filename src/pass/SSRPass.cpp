// ============================================================================
// KazuEngine - Pass Layer: Screen Space Reflection (Implementation)
// ============================================================================

#include "pass/SSRPass.h"
#include "core/Path.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rhi/Camera.h"
#include "rhi/ShaderLibrary.h"
#include "rhi/DescriptorSetLayoutCache.h"
#include "rendergraph/RenderGraph.h"
#include <glm/glm.hpp>
#include <array>
#include <cstring>
#include <algorithm>

namespace kazu {

namespace {

struct SSRPush {
    glm::mat4 proj;
    glm::mat4 invProj;
    glm::mat4 view;
    glm::vec4 params;      // x = maxDistance, y = stridePixels, z = thickness, w = stepCount
    glm::vec2 screenSize;
    int32_t   displayMode;
    int32_t   traceMode;   // 0 = basic, 1 = DDA, 2 = Hi-Z
    int32_t   hizMaxMip;
    int32_t   enabled;     // 0 = bypass SSR, 1 = run SSR
    int32_t   binarySearchSteps; // outer binary iterations
    int32_t   jitterEnabled;
    int32_t   hizVisMip;
};

} // anonymous namespace

SSRPass::SSRPass() = default;

SSRPass::~SSRPass() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    m_timer.reset();
    for (auto& b : m_stepBuffers) b.reset();

    if (m_descriptorPool)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_pipeline)
        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout)
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_sampler)
        vkDestroySampler(device, m_sampler, nullptr);
    if (m_depthSampler)
        vkDestroySampler(device, m_depthSampler, nullptr);
}

void SSRPass::setInputs(RenderGraph::ResourceHandle sceneColor,
                        RenderGraph::ResourceHandle depth,
                        RenderGraph::ResourceHandle normal,
                        RenderGraph::ResourceHandle material,
                        RenderGraph::ResourceHandle albedo,
                        RenderGraph::ResourceHandle hiz) {
    m_sceneColorHandle = sceneColor;
    m_depthHandle      = depth;
    m_normalHandle     = normal;
    m_materialHandle   = material;
    m_albedoHandle     = albedo;
    m_hizHandle        = hiz;
}

void SSRPass::declare(RHI* rhi, RenderGraph* rg) {
    m_outputHandle = rg->addTexture("SSRReflect",
        {.width  = rhi->extent().width,
         .height = rhi->extent().height,
         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .usage  = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});

    SSRPass* self = this;
    m_passHandle = rg->addPass("SSR", [&](RenderGraph::PassBuilder& b) {
        b.type = RenderGraph::PassType::Compute;
        b.read(self->m_sceneColorHandle);
        b.read(self->m_depthHandle);
        b.read(self->m_normalHandle);
        b.read(self->m_materialHandle);
        b.read(self->m_albedoHandle);
        if (self->m_hizHandle != RenderGraph::InvalidResource)
            b.read(self->m_hizHandle);
        b.writeStorageImage(self->m_outputHandle);
        b.execute = [self](const PassExecuteContext& ctx) {
            self->execute(ctx);
        };
    });
}

void SSRPass::create(const PassCreateContext& ctx) {
    m_rhi = ctx.rhi;
    m_renderGraph = ctx.renderGraph;

    VkExtent2D extent = m_rhi->extent();
    m_hizMaxMip = static_cast<int>(computeMipLevels(extent.width, extent.height)) - 1;

    m_timer = std::make_unique<GPUTimer>(
        m_rhi->ctx().device(),
        m_rhi->ctx().physicalDevice(),
        kRingSize);

    VkDeviceSize statsSize = sizeof(uint32_t) * 2;
    for (uint32_t i = 0; i < kRingSize; ++i) {
        m_stepBuffers[i] = std::make_unique<Buffer>(m_rhi->ctx(),
            statsSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memset(m_stepBuffers[i]->mapped(), 0, static_cast<size_t>(statsSize));
        m_slotTraceMode[i] = -1;
    }

    createPipeline();
    createDescriptorSets();
}

void SSRPass::createPipeline() {
    VkDevice device = m_rhi->ctx().device();

    // Linear sampler for scene color / normal / material / albedo.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 32.0f;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler));

    // Nearest sampler for depth / Hi-Z: depth values must not be interpolated
    // when sampled at explicit mip levels, otherwise the min-pyramid is no
    // longer conservative.
    VkSamplerCreateInfo depthSamplerInfo{};
    depthSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    depthSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.maxLod = 32.0f;
    VK_CHECK(vkCreateSampler(device, &depthSamplerInfo, nullptr, &m_depthSampler));

    // Binding 0 = output reflection contribution storage image
    // Binding 1 = scene color sampler (for reflected color lookup)
    // Binding 2 = depth sampler
    // Binding 3 = normal sampler
    // Binding 4 = material sampler
    // Binding 5 = albedo sampler
    // Binding 6 = Hi-Z pyramid sampler
    // Binding 7 = per-pixel step statistics (storage buffer)
    std::vector<VkDescriptorSetLayoutBinding> bindings(8);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    for (uint32_t i = 1; i < 7; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    m_descriptorSetLayout = m_rhi->dslCache().getOrCreate(bindings);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(SSRPush);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    VK_CHECK(vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout));

    VkShaderModule cs = m_rhi->shaderLib().load(
        kazu::Path::resolveShader("ssr.comp.spv"));

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = cs;
    pipeInfo.stage.pName = "main";
    pipeInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pipeline));
}

void SSRPass::createDescriptorSets() {
    VkDevice device = m_rhi->ctx().device();

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = kRingSize;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 6 * kRingSize;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kRingSize;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kRingSize;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

    std::vector<VkDescriptorSetLayout> layouts(kRingSize, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = kRingSize;
    allocInfo.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()));

    VkImageView outputView     = m_renderGraph->getImageView(m_outputHandle);
    VkImageView sceneColorView = m_renderGraph->getImageView(m_sceneColorHandle);
    VkImageView depthView      = m_renderGraph->getImageView(m_depthHandle);
    VkImageView normalView     = m_renderGraph->getImageView(m_normalHandle);
    VkImageView materialView   = m_renderGraph->getImageView(m_materialHandle);
    VkImageView albedoView     = m_renderGraph->getImageView(m_albedoHandle);
    VkImageView hizView        = (m_hizHandle != RenderGraph::InvalidResource)
                                   ? m_renderGraph->getImageView(m_hizHandle)
                                   : depthView; // fallback, won't be sampled in basic/binary modes

    std::array<VkDescriptorImageInfo, 7> infos{};
    infos[0].imageView = outputView;
    infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    infos[0].sampler = VK_NULL_HANDLE;

    infos[1].sampler = m_sampler;
    infos[1].imageView = sceneColorView;
    infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[2].sampler = m_depthSampler;
    infos[2].imageView = depthView;
    infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    infos[3].sampler = m_sampler;
    infos[3].imageView = normalView;
    infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[4].sampler = m_sampler;
    infos[4].imageView = materialView;
    infos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[5].sampler = m_sampler;
    infos[5].imageView = albedoView;
    infos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    infos[6].sampler = m_depthSampler;
    infos[6].imageView = hizView;
    infos[6].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    for (uint32_t slot = 0; slot < kRingSize; ++slot) {
        VkDescriptorBufferInfo stepBufferInfo{};
        stepBufferInfo.buffer = m_stepBuffers[slot]->handle();
        stepBufferInfo.offset = 0;
        stepBufferInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 8> writes{};
        for (uint32_t i = 0; i < 7; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_descriptorSets[slot];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &infos[i];
        }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        for (uint32_t i = 1; i < 7; ++i) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }

        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_descriptorSets[slot];
        writes[7].dstBinding = 7;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[7].pBufferInfo = &stepBufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void SSRPass::execute(const PassExecuteContext& ctx) {
    VkCommandBuffer cmd = ctx.cmd;

    uint32_t slot = m_frameCounter % kRingSize;
    int prevMode = m_slotTraceMode[slot];

    // Read back the step statistics for this slot (from a frame at least
    // kRingSize frames old, so the GPU work is guaranteed complete).  Attribute
    // them to the trace mode that was active when the slot was dispatched.
    {
        const uint32_t* mapped = static_cast<const uint32_t*>(m_stepBuffers[slot]->mapped());
        uint32_t totalSteps = mapped[0];
        uint32_t totalPixels = mapped[1];
        if (totalPixels > 0) {
            float steps = static_cast<float>(totalSteps) / static_cast<float>(totalPixels);
            m_avgSteps = steps;
            if (prevMode >= 0 && prevMode < kModeCount) {
                constexpr float alpha = 0.05f;
                m_avgStepsByMode[prevMode] = m_avgStepsByMode[prevMode] * (1.0f - alpha) + steps * alpha;
            }
        }
    }

    // Update rolling GPU timer average for the same slot, keyed by mode.
    {
        float ms = 0.0f;
        if (m_timer->fetchMs(slot, ms)) {
            if (prevMode >= 0 && prevMode < kModeCount) {
                m_lastGpuMsByMode[prevMode] = ms;
                constexpr float alpha = 0.05f;
                m_avgGpuMsByMode[prevMode] = m_avgGpuMsByMode[prevMode] * (1.0f - alpha) + ms * alpha;
            }
        }
    }

    // Clear the statistics buffer and bind it for this frame.
    vkCmdFillBuffer(cmd, m_stepBuffers[slot]->handle(), 0, VK_WHOLE_SIZE, 0);

    VkBufferMemoryBarrier clearBarrier{};
    clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.buffer = m_stepBuffers[slot]->handle();
    clearBarrier.offset = 0;
    clearBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &clearBarrier, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSets[slot], 0, nullptr);

    VkExtent2D extent = m_rhi->extent();
    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 view = ctx.camera->getViewMatrix();
    glm::mat4 proj = ctx.camera->getJitteredProjectionMatrix(aspect);

    SSRPush push{};
    push.proj        = proj;
    push.invProj     = glm::inverse(proj);
    push.view        = view;
    push.params.x    = m_maxDistance;
    push.params.y    = m_stride;
    push.params.z    = m_thickness;
    push.params.w    = static_cast<float>(m_stepCount);
    push.screenSize  = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
    push.displayMode = m_enabled ? m_displayMode : 0;
    push.traceMode   = m_enabled ? m_traceMode : 0;
    push.hizMaxMip   = (m_hizHandle != RenderGraph::InvalidResource) ? m_hizMaxMip : 0;
    push.enabled     = m_enabled ? 1 : 0;
    push.binarySearchSteps = m_binarySearchSteps;
    push.jitterEnabled       = m_jitterEnabled ? 1 : 0;
    push.hizVisMip           = m_hizVisMip;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(SSRPush), &push);

    // Remember which mode this slot was dispatched with so the readback on a
    // later frame is attributed to the correct trace mode.
    int currentMode = m_enabled ? m_traceMode : 0;
    m_slotTraceMode[slot] = currentMode;

    m_timer->resetSlot(cmd, slot);
    m_timer->begin(cmd, slot);

    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    m_timer->end(cmd, slot);

    ++m_frameCounter;

    // Periodic console log so headless / automated runs can compare modes.
    if ((m_frameCounter % 120) == 0) {
        spdlog::info("SSR metrics | Basic steps={:.1f} gpu={:.3f}ms | DDA steps={:.1f} gpu={:.3f}ms | Hi-Z steps={:.1f} gpu={:.3f}ms",
                     m_avgStepsByMode[0], m_avgGpuMsByMode[0],
                     m_avgStepsByMode[1], m_avgGpuMsByMode[1],
                     m_avgStepsByMode[2], m_avgGpuMsByMode[2]);
    }
}

uint32_t SSRPass::computeMipLevels(uint32_t width, uint32_t height) {
    uint32_t size = std::max(width, height);
    uint32_t levels = 0;
    while (size > 0) {
        ++levels;
        size >>= 1;
    }
    return levels;
}

float SSRPass::lastGpuTimeMs(int mode) const {
    int m = (mode < 0) ? (m_enabled ? m_traceMode : 0) : mode;
    m = std::clamp(m, 0, kModeCount - 1);
    return m_lastGpuMsByMode[m];
}

float SSRPass::avgGpuTimeMs(int mode) const {
    int m = (mode < 0) ? (m_enabled ? m_traceMode : 0) : mode;
    m = std::clamp(m, 0, kModeCount - 1);
    return m_avgGpuMsByMode[m];
}

float SSRPass::avgSteps(int mode) const {
    int m = (mode < 0) ? (m_enabled ? m_traceMode : 0) : mode;
    m = std::clamp(m, 0, kModeCount - 1);
    return m_avgStepsByMode[m];
}

} // namespace kazu
