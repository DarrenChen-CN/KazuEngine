// ============================================================================
// KazuEngine - Application Layer: Precompute Manager (Implementation)
// ============================================================================

#include "precompute/PrecomputeManager.h"
#include "core/CommandBuffer.h"
#include "core/Image.h"
#include "core/Utils.h"
#include "rhi/RHI.h"
#include "rendergraph/RenderGraph.h"
#include <spdlog/spdlog.h>

namespace kazu {

PrecomputeManager::PrecomputeManager() = default;

PrecomputeManager::~PrecomputeManager() = default;

void PrecomputeManager::init(RHI* rhi, Scene* scene) {
    m_rhi = rhi;
    m_scene = scene;
    m_initialized = true;
}

void PrecomputeManager::registerPass(std::unique_ptr<PrecomputePass> pass) {
    m_passes.push_back({std::move(pass), nullptr});
}

Texture* PrecomputeManager::getTexture(const std::string& name) const {
    auto it = m_outputMap.find(name);
    return (it != m_outputMap.end()) ? it->second : nullptr;
}

void PrecomputeManager::executePass(PassSlot& slot) {
    if (!slot.pass || !m_rhi) return;

    auto desc = slot.pass->outputDesc();

    // Create persistent image + texture.
    ImageDesc imageDesc{};
    imageDesc.type = VK_IMAGE_TYPE_2D;
    imageDesc.extent = {desc.width, desc.height, 1};
    imageDesc.mipLevels = desc.mipLevels;
    imageDesc.arrayLayers = desc.arrayLayers;
    imageDesc.format = desc.format;
    imageDesc.usage = desc.usage;
    imageDesc.flags = desc.flags;

    auto image = std::make_unique<Image>(m_rhi->ctx(), imageDesc);
    Image* imagePtr = image.get();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (desc.mipLevels > 1) {
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    samplerInfo.maxLod = static_cast<float>(desc.mipLevels);
    slot.output = std::make_unique<Texture>(m_rhi->ctx(), std::move(image), samplerInfo);

    // One-shot RenderGraph: import the persistent image and let the pass write into it.
    RenderGraph rg(m_rhi->ctx());
    auto handle = rg.addImportedTexture(
        desc.name.c_str(),
        {.width = desc.width,
         .height = desc.height,
         .mipLevels = desc.mipLevels,
         .arrayLayers = desc.arrayLayers,
         .format = desc.format,
         .usage = desc.usage,
         .flags = desc.flags},
        imagePtr->handle(),
        imagePtr->view());

    slot.pass->setOutputResource(handle, imagePtr);
    slot.pass->declare(m_rhi, &rg);

    if (!rg.compile()) {
        fatalError("PrecomputeManager: RenderGraph compile failed for pass '" + desc.name + "'");
    }

    PassCreateContext createCtx{m_rhi, &rg, m_scene};
    slot.pass->create(createCtx);

    CommandBuffer cmd(m_rhi->ctx(), m_rhi->ctx().transientPool());
    cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    PassExecuteContext execCtx{};
    execCtx.cmd = cmd.handle();
    rg.execute(execCtx);
    cmd.end();
    cmd.submit(m_rhi->ctx().graphicsQueue());
    vkQueueWaitIdle(m_rhi->ctx().graphicsQueue());

    spdlog::info("[PrecomputeManager] Pass '{}' finished", desc.name);
}

void PrecomputeManager::run() {
    if (!m_initialized || !m_rhi) {
        spdlog::warn("[PrecomputeManager] run() called before init()");
        return;
    }

    m_outputMap.clear();
    for (auto& slot : m_passes) {
        if (slot.pass) {
            slot.pass->resolveInputs(this);
        }
        executePass(slot);
        if (slot.output && slot.pass) {
            m_outputMap[slot.pass->outputDesc().name] = slot.output.get();
        }
    }

    spdlog::info("[PrecomputeManager] All precompute passes finished");
}

} // namespace kazu
