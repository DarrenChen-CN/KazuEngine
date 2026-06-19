// ============================================================================
// KazuEngine - RenderGraph: Core Implementation
// ============================================================================

#include "rendergraph/RenderGraph.h"
#include "core/Context.h"
#include "core/Utils.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <queue>
#include <unordered_set>

namespace kazu {

// ============================================================================
// Helpers
// ============================================================================

static VkImageAspectFlags aspectMaskFromFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static bool isReadUsage(RenderGraph::ResourceUsage usage) {
    switch (usage) {
    case RenderGraph::ResourceUsage::SampledRead:
    case RenderGraph::ResourceUsage::DepthSampledRead:
    case RenderGraph::ResourceUsage::StorageImageRead:
    case RenderGraph::ResourceUsage::StorageBufferRead:
        return true;
    default:
        return false;
    }
}

static bool isWriteUsage(RenderGraph::ResourceUsage usage) {
    switch (usage) {
    case RenderGraph::ResourceUsage::ColorAttachmentWrite:
    case RenderGraph::ResourceUsage::DepthAttachmentWrite:
    case RenderGraph::ResourceUsage::StorageImageWrite:
    case RenderGraph::ResourceUsage::StorageBufferWrite:
        return true;
    default:
        return false;
    }
}

static const char* resourceUsageName(RenderGraph::ResourceUsage usage) {
    switch (usage) {
    case RenderGraph::ResourceUsage::SampledRead: return "SampledRead";
    case RenderGraph::ResourceUsage::ColorAttachmentWrite: return "ColorAttachmentWrite";
    case RenderGraph::ResourceUsage::DepthAttachmentWrite: return "DepthAttachmentWrite";
    case RenderGraph::ResourceUsage::DepthSampledRead: return "DepthSampledRead";
    case RenderGraph::ResourceUsage::StorageImageRead: return "StorageImageRead";
    case RenderGraph::ResourceUsage::StorageImageWrite: return "StorageImageWrite";
    case RenderGraph::ResourceUsage::StorageBufferRead: return "StorageBufferRead";
    case RenderGraph::ResourceUsage::StorageBufferWrite: return "StorageBufferWrite";
    case RenderGraph::ResourceUsage::Present: return "Present";
    default: return "Unknown";
    }
}

static const char* imageLayoutName(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
    case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC_KHR";
    default: return "OTHER";
    }
}

static const char* passTypeName(RenderGraph::PassType type) {
    switch (type) {
    case RenderGraph::PassType::Graphics: return "Graphics";
    case RenderGraph::PassType::Compute: return "Compute";
    default: return "Unknown";
    }
}

// ============================================================================
// Construction
// ============================================================================

RenderGraph::RenderGraph(Context& ctx) : m_ctx(&ctx) {}

RenderGraph::~RenderGraph() {
    cleanupPassRenderTargets();
}

// ============================================================================
// Resource declaration
// ============================================================================

RenderGraph::ResourceHandle RenderGraph::addTexture(const char* name, const TextureDesc& desc) {
    ResourceHandle handle = static_cast<ResourceHandle>(m_resources.size());
    ResourceNode node;
    node.name = name ? name : "";
    node.desc = desc;
    node.ownership = ResourceOwnership::Transient;
    m_resources.push_back(std::move(node));
    return handle;
}

RenderGraph::ResourceHandle RenderGraph::addBuffer(const char* name) {
    ResourceHandle handle = static_cast<ResourceHandle>(m_resources.size());
    ResourceNode node;
    node.name = name ? name : "";
    // desc defaults keep format=UNDEFINED, marking this as a buffer
    node.ownership = ResourceOwnership::Transient;
    m_resources.push_back(std::move(node));
    return handle;
}

RenderGraph::ResourceHandle RenderGraph::addImportedTexture(const char* name, const TextureDesc& desc,
                                                             VkImage image, VkImageView view) {
    ResourceHandle handle = static_cast<ResourceHandle>(m_resources.size());
    ResourceNode node;
    node.name = name ? name : "";
    node.desc = desc;
    node.ownership = ResourceOwnership::Imported;
    node.imported.currentImage = image;
    node.imported.currentView = view;
    m_resources.push_back(std::move(node));
    return handle;
}

void RenderGraph::bindImportedTexture(ResourceHandle handle, VkImage image, VkImageView view) {
    if (handle >= m_resources.size()) return;
    auto& res = m_resources[handle];
    if (res.ownership != ResourceOwnership::Imported) return;
    res.imported.currentImage = image;
    res.imported.currentView = view;
}

void RenderGraph::setImportedTextureViews(ResourceHandle handle, const std::vector<VkImageView>& views) {
    if (handle >= m_resources.size()) return;
    auto& res = m_resources[handle];
    if (res.ownership != ResourceOwnership::Imported) return;
    res.imported.allViews = views;
}

// ============================================================================
// Pass declaration
// ============================================================================

RenderGraph::PassHandle RenderGraph::addPass(const char* name, PassSetupFn setup) {
    PassBuilder builder;
    setup(builder);

    PassNode node;
    node.name    = name ? name : "";
    node.type    = builder.type;
    node.execute = builder.execute;

    // Convert the legacy builder API into explicit resource usage records.
    for (ResourceHandle res : builder.reads) {
        ResourceUsage usage = ResourceUsage::SampledRead;
        if (res < m_resources.size()) {
            const auto& resource = m_resources[res];
            if ((resource.desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                usage = ResourceUsage::DepthSampledRead;
            }
        }
        node.usages.push_back({res, usage, ~0u});
    }
    for (auto& [slot, res] : builder.writeColors) {
        node.usages.push_back({res, ResourceUsage::ColorAttachmentWrite, slot});
    }
    if (builder.writeDepth_ != InvalidResource) {
        node.usages.push_back({builder.writeDepth_, ResourceUsage::DepthAttachmentWrite, ~0u});
    }

    PassHandle handle = static_cast<PassHandle>(m_passes.size());
    m_passes.push_back(std::move(node));
    return handle;
}

// ============================================================================
// Compilation: topological sort + transient resource allocation
// ============================================================================

bool RenderGraph::compile() {
    m_sortedIndices.clear();
    cleanupPassRenderTargets();
    // Release any previously allocated transient images (keep imported resources intact)
    for (auto& res : m_resources) {
        if (res.ownership == ResourceOwnership::Transient) {
            res.ownedImage.reset();
        }
    }

    size_t n = m_passes.size();
    if (n == 0) return true;

    // Build adjacency list and in-degree
    std::vector<std::vector<uint32_t>> dependents(n); // pass i -> passes that depend on i
    std::vector<int> inDegree(n, 0);

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (hasDependency(i, j)) {
                dependents[i].push_back(j);
                inDegree[j]++;
            }
        }
    }

    // Kahn's algorithm
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < n; ++i) {
        if (inDegree[i] == 0) q.push(i);
    }

    while (!q.empty()) {
        uint32_t u = q.front(); q.pop();
        m_sortedIndices.push_back(u);
        for (uint32_t v : dependents[u]) {
            if (--inDegree[v] == 0) q.push(v);
        }
    }

    if (m_sortedIndices.size() != n) {
        // Cycle detected — reset and report failure
        m_sortedIndices.clear();
        return false;
    }

    // Allocate transient resources after successful topological sort
    allocateResources();
    deriveBarriers();
    createPassRenderTargets();
    dumpDebugInfo();
    return true;
}

// Analyze resource lifetime and create Images for textures.
// Simplified: no aliasing, each texture gets its own VkDeviceMemory.
void RenderGraph::allocateResources() {
    size_t resCount = m_resources.size();
    if (resCount == 0 || !m_ctx) return;

    std::vector<int> firstPass(resCount, -1);
    std::vector<int> lastPass(resCount, -1);

    for (uint32_t passIdx = 0; passIdx < m_passes.size(); ++passIdx) {
        const auto& pass = m_passes[passIdx];
        auto touch = [&](ResourceHandle rh) {
            if (rh >= resCount) return;
            if (firstPass[rh] == -1) firstPass[rh] = static_cast<int>(passIdx);
            lastPass[rh] = static_cast<int>(passIdx);
        };
        for (const auto& use : pass.usages) touch(use.resource);
    }

    for (uint32_t i = 0; i < resCount; ++i) {
        auto& res = m_resources[i];
        if (res.ownership == ResourceOwnership::Imported) continue; // externally managed
        if (res.desc.format == VK_FORMAT_UNDEFINED) continue; // buffer, not texture
        if (firstPass[i] == -1) continue; // unused, don't allocate

        res.ownedImage = std::make_unique<Image>(*m_ctx,
                                                 res.desc.width,
                                                 res.desc.height,
                                                 res.desc.format,
                                                 res.desc.usage,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
}

// ============================================================================
// Barrier Derivation
// ============================================================================

// Track the current Vulkan state of each transient image.
struct ResourceState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags access = 0;
};

struct UsageInfo {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    VkImageLayout layout;
};

static UsageInfo getUsageInfo(RenderGraph::ResourceUsage usage) {
    switch (usage) {
    case RenderGraph::ResourceUsage::ColorAttachmentWrite:
        return {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
    case RenderGraph::ResourceUsage::DepthAttachmentWrite:
        return {
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
    case RenderGraph::ResourceUsage::DepthSampledRead:
        return {
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        };
    case RenderGraph::ResourceUsage::Present:
        return {
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        };
    case RenderGraph::ResourceUsage::StorageImageRead:
        return {
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL
        };
    case RenderGraph::ResourceUsage::StorageImageWrite:
        return {
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL
        };
    case RenderGraph::ResourceUsage::SampledRead:
    case RenderGraph::ResourceUsage::StorageBufferRead:
    case RenderGraph::ResourceUsage::StorageBufferWrite:
    default:
        return {
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }
}

void RenderGraph::deriveBarriers() {
    if (m_sortedIndices.empty()) return;

    std::vector<ResourceState> states(m_resources.size());

    for (uint32_t sortedIdx : m_sortedIndices) {
        auto& pass = m_passes[sortedIdx];
        pass.preBarrier = {}; // clear any previous barriers

        auto emitBarrier = [&](ResourceHandle rh, const ResourceState& oldState, const UsageInfo& newUsage) {
            const auto& res = m_resources[rh];
            if (res.desc.format == VK_FORMAT_UNDEFINED) return; // buffer or unallocated
            // Skip barrier if layout/stage/access haven't changed
            if (oldState.layout == newUsage.layout &&
                oldState.stage == newUsage.stage &&
                oldState.access == newUsage.access) {
                return;
            }

            BarrierDesc desc{};
            desc.resource = rh;
            desc.oldLayout = oldState.layout;
            desc.newLayout = newUsage.layout;
            desc.srcAccess = oldState.access;
            desc.dstAccess = newUsage.access;
            desc.srcStage = oldState.stage;
            desc.dstStage = newUsage.stage;

            pass.preBarrier.descs.push_back(desc);
            pass.preBarrier.srcStage |= oldState.stage;
            pass.preBarrier.dstStage |= newUsage.stage;
        };

        // Process reads first.
        // Note: in the current simplified model, a resource is never both read
        // and written in the same pass. If that changes, writes should take
        // precedence (they represent the final state of the pass).
        for (const auto& use : pass.usages) {
            if (!isReadUsage(use.usage)) continue;
            ResourceHandle rh = use.resource;
            if (rh >= m_resources.size()) continue;
            const auto& res = m_resources[rh];
            if (res.desc.format == VK_FORMAT_UNDEFINED) continue; // buffer

            const auto& oldState = states[rh];
            UsageInfo readInfo = getUsageInfo(use.usage);
            emitBarrier(rh, oldState, readInfo);
            states[rh] = {readInfo.layout, readInfo.stage, readInfo.access};
        }

        // Process writes
        for (const auto& use : pass.usages) {
            if (!isWriteUsage(use.usage)) continue;
            ResourceHandle rh = use.resource;
            if (rh >= m_resources.size()) continue;
            const auto& res = m_resources[rh];
            if (res.desc.format == VK_FORMAT_UNDEFINED) continue; // buffer

            const auto& oldState = states[rh];
            UsageInfo writeInfo = getUsageInfo(use.usage);
            emitBarrier(rh, oldState, writeInfo);
            states[rh] = {writeInfo.layout, writeInfo.stage, writeInfo.access};
        }

        spdlog::debug("[RenderGraph] Pass '{}' pre-barriers: {} (srcStage=0x{:x}, dstStage=0x{:x})",
                      pass.name, pass.preBarrier.descs.size(),
                      pass.preBarrier.srcStage, pass.preBarrier.dstStage);
    }
}

// Returns true if pass 'reader' must execute after pass 'writer'
// because writer produces a resource that reader consumes.
bool RenderGraph::hasDependency(uint32_t writerIdx, uint32_t readerIdx) const {
    const auto& writer = m_passes[writerIdx];
    const auto& reader = m_passes[readerIdx];

    for (const auto& write : writer.usages) {
        if (!isWriteUsage(write.usage)) continue;
        ResourceHandle w = write.resource;
        if (passReadsResource(reader, w)) return true;
        // WAW (write-after-write): keep declaration order for passes that
        // update the same attachment, e.g. Lighting -> LightVisualize on
        // SceneColorHDR. Adding this dependency in both directions creates a
        // false cycle, so only earlier writers constrain later writers.
        if (writerIdx < readerIdx && passWritesResource(reader, w)) return true;
    }
    return false;
}

bool RenderGraph::passReadsResource(const PassNode& pass, ResourceHandle resource) const {
    for (const auto& use : pass.usages) {
        if (use.resource == resource && isReadUsage(use.usage)) return true;
    }
    return false;
}

bool RenderGraph::passWritesResource(const PassNode& pass, ResourceHandle resource) const {
    for (const auto& use : pass.usages) {
        if (use.resource == resource && isWriteUsage(use.usage)) return true;
    }
    return false;
}

void RenderGraph::cleanupPassRenderTargets() {
    if (!m_ctx) return;
    VkDevice device = m_ctx->device();
    for (auto& pass : m_passes) {
        for (VkFramebuffer fb : pass.framebuffers) {
            if (fb) vkDestroyFramebuffer(device, fb, nullptr);
        }
        pass.framebuffers.clear();
        if (pass.renderPass) {
            vkDestroyRenderPass(device, pass.renderPass, nullptr);
            pass.renderPass = VK_NULL_HANDLE;
        }
        pass.framebufferResources.clear();
    }
}

void RenderGraph::createPassRenderTargets() {
    if (!m_ctx) return;
    VkDevice device = m_ctx->device();

    for (auto& pass : m_passes) {
        if (pass.type != PassType::Graphics) continue;

        std::vector<ResourceUse> colorUses;
        ResourceUse depthUse;
        bool hasDepth = false;
        for (const auto& use : pass.usages) {
            if (use.usage == ResourceUsage::ColorAttachmentWrite) {
                colorUses.push_back(use);
            } else if (use.usage == ResourceUsage::DepthAttachmentWrite) {
                depthUse = use;
                hasDepth = true;
            }
        }
        if (colorUses.empty() && !hasDepth) continue;

        std::sort(colorUses.begin(), colorUses.end(),
            [](const ResourceUse& a, const ResourceUse& b) { return a.slot < b.slot; });

        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> colorRefs;
        VkAttachmentReference depthRef{};
        pass.framebufferResources.clear();

        auto appendAttachment = [&](const ResourceUse& use, VkImageLayout layout) {
            if (use.resource >= m_resources.size()) return;
            const auto& res = m_resources[use.resource];
            if (res.desc.format == VK_FORMAT_UNDEFINED) return;

            VkAttachmentDescription attachment{};
            attachment.format = res.desc.format;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = passReadsResource(pass, use.resource)
                ? VK_ATTACHMENT_LOAD_OP_LOAD
                : VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = layout;
            attachment.finalLayout = layout;

            uint32_t attachmentIndex = static_cast<uint32_t>(attachments.size());
            attachments.push_back(attachment);
            pass.framebufferResources.push_back(use.resource);

            if (use.usage == ResourceUsage::ColorAttachmentWrite) {
                colorRefs.push_back({attachmentIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            } else if (use.usage == ResourceUsage::DepthAttachmentWrite) {
                depthRef = {attachmentIndex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            }
        };

        for (const auto& use : colorUses) {
            appendAttachment(use, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        if (hasDepth) {
            appendAttachment(depthUse, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }
        if (attachments.empty()) continue;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
        subpass.pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
        subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = dependency.srcStageMask;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        rpInfo.pAttachments = attachments.data();
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;
        VK_CHECK(vkCreateRenderPass(device, &rpInfo, nullptr, &pass.renderPass));

        uint32_t framebufferCount = 1;
        for (ResourceHandle rh : pass.framebufferResources) {
            const auto& res = m_resources[rh];
            if (res.ownership == ResourceOwnership::Imported && !res.imported.allViews.empty()) {
                framebufferCount = std::max(framebufferCount,
                                            static_cast<uint32_t>(res.imported.allViews.size()));
            }
        }
        pass.framebuffers.resize(framebufferCount, VK_NULL_HANDLE);

        VkExtent2D extent{};
        for (ResourceHandle rh : pass.framebufferResources) {
            extent = getImageExtent(rh);
            if (extent.width != 0 && extent.height != 0) break;
        }

        for (uint32_t framebufferIndex = 0; framebufferIndex < framebufferCount; ++framebufferIndex) {
            std::vector<VkImageView> views;
            views.reserve(pass.framebufferResources.size());
            for (ResourceHandle rh : pass.framebufferResources) {
                const auto& res = m_resources[rh];
                if (res.ownership == ResourceOwnership::Imported &&
                    framebufferIndex < res.imported.allViews.size()) {
                    views.push_back(res.imported.allViews[framebufferIndex]);
                } else {
                    views.push_back(getImageView(rh));
                }
            }

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = pass.renderPass;
            fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
            fbInfo.pAttachments = views.data();
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &pass.framebuffers[framebufferIndex]));
        }
    }
}

// ============================================================================
// Execution
// ============================================================================

void RenderGraph::execute(const PassExecuteContext& passCtx) const {
    if (!isCompiled()) return;

    for (uint32_t idx : m_sortedIndices) {
        const auto& pass = m_passes[idx];

        if (!pass.preBarrier.descs.empty()) {
            // Build VkImageMemoryBarriers from descriptors (resolve image handles at execute time)
            std::vector<VkImageMemoryBarrier> barriers;
            barriers.reserve(pass.preBarrier.descs.size());
            for (const auto& desc : pass.preBarrier.descs) {
                const auto& res = m_resources[desc.resource];
                VkImage image = VK_NULL_HANDLE;
                if (res.ownership == ResourceOwnership::Imported) {
                    image = res.imported.currentImage;
                } else if (res.ownedImage) {
                    image = res.ownedImage->handle();
                }
                if (image == VK_NULL_HANDLE) continue;

                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = desc.oldLayout;
                barrier.newLayout = desc.newLayout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = image;
                barrier.subresourceRange.aspectMask = aspectMaskFromFormat(res.desc.format);
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = desc.srcAccess;
                barrier.dstAccessMask = desc.dstAccess;
                barriers.push_back(barrier);
            }

            if (!barriers.empty()) {
                vkCmdPipelineBarrier(passCtx.cmd,
                    pass.preBarrier.srcStage,
                    pass.preBarrier.dstStage,
                    0,
                    0, nullptr,
                    0, nullptr,
                    static_cast<uint32_t>(barriers.size()),
                    barriers.data());
            }
        }

        if (pass.execute) {
            pass.execute(passCtx);
        }
    }
}

void RenderGraph::dumpDebugInfo() const {
    spdlog::info("[RenderGraph] ===== Debug Dump =====");
    spdlog::info("[RenderGraph] Resources: {}", m_resources.size());
    for (uint32_t i = 0; i < m_resources.size(); ++i) {
        const auto& res = m_resources[i];
        const char* kind = res.desc.format == VK_FORMAT_UNDEFINED
            ? "buffer"
            : (res.ownership == ResourceOwnership::Imported ? "imported-texture" : "transient-texture");
        spdlog::info("[RenderGraph]   #{} '{}' kind={} extent={}x{} format={} usage=0x{:x} allocated={}",
                     i,
                     res.name,
                     kind,
                     res.desc.width,
                     res.desc.height,
                     static_cast<int>(res.desc.format),
                     static_cast<uint32_t>(res.desc.usage),
                     (res.ownership == ResourceOwnership::Imported || res.ownedImage) ? "yes" : "no");
    }

    spdlog::info("[RenderGraph] Pass order: {}", m_sortedIndices.size());
    for (uint32_t order = 0; order < m_sortedIndices.size(); ++order) {
        uint32_t passIdx = m_sortedIndices[order];
        const auto& pass = m_passes[passIdx];
        spdlog::info("[RenderGraph]   [{}] pass #{} '{}' type={} uses={} preBarriers={} renderPass={} framebuffers={}",
                     order,
                     passIdx,
                     pass.name,
                     passTypeName(pass.type),
                     pass.usages.size(),
                     pass.preBarrier.descs.size(),
                     pass.renderPass ? "yes" : "no",
                     pass.framebuffers.size());

        for (const auto& use : pass.usages) {
            const char* resName = getResourceName(use.resource);
            if (use.slot != ~0u) {
                spdlog::info("[RenderGraph]       use {} resource #{} '{}' slot={}",
                             resourceUsageName(use.usage),
                             use.resource,
                             resName ? resName : "<invalid>",
                             use.slot);
            } else {
                spdlog::info("[RenderGraph]       use {} resource #{} '{}'",
                             resourceUsageName(use.usage),
                             use.resource,
                             resName ? resName : "<invalid>");
            }
        }

        for (const auto& barrier : pass.preBarrier.descs) {
            const char* resName = getResourceName(barrier.resource);
            spdlog::info("[RenderGraph]       barrier #{} '{}' {} -> {} stage 0x{:x}->0x{:x} access 0x{:x}->0x{:x}",
                         barrier.resource,
                         resName ? resName : "<invalid>",
                         imageLayoutName(barrier.oldLayout),
                         imageLayoutName(barrier.newLayout),
                         static_cast<uint32_t>(barrier.srcStage),
                         static_cast<uint32_t>(barrier.dstStage),
                         static_cast<uint32_t>(barrier.srcAccess),
                         static_cast<uint32_t>(barrier.dstAccess));
        }
    }
    spdlog::info("[RenderGraph] ======================");
}

// ============================================================================
// Queries
// ============================================================================

const char* RenderGraph::getResourceName(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return nullptr;
    return m_resources[handle].name.c_str();
}

const char* RenderGraph::getPassName(PassHandle handle) const {
    if (handle >= m_passes.size()) return nullptr;
    return m_passes[handle].name.c_str();
}

VkImage RenderGraph::getImageHandle(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return VK_NULL_HANDLE;
    const auto& res = m_resources[handle];
    if (res.ownership == ResourceOwnership::Imported) return res.imported.currentImage;
    if (!res.ownedImage) return VK_NULL_HANDLE;
    return res.ownedImage->handle();
}

VkImageView RenderGraph::getImageView(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return VK_NULL_HANDLE;
    const auto& res = m_resources[handle];
    if (res.ownership == ResourceOwnership::Imported) return res.imported.currentView;
    if (!res.ownedImage) return VK_NULL_HANDLE;
    return res.ownedImage->view();
}

VkExtent2D RenderGraph::getImageExtent(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return {};
    const auto& res = m_resources[handle];
    if (res.ownership == ResourceOwnership::Imported) return {res.desc.width, res.desc.height};
    if (!res.ownedImage) return {};
    return res.ownedImage->extent();
}

VkFormat RenderGraph::getImageFormat(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return VK_FORMAT_UNDEFINED;
    const auto& res = m_resources[handle];
    return res.desc.format;
}

VkRenderPass RenderGraph::getRenderPass(PassHandle handle) const {
    if (handle >= m_passes.size()) return VK_NULL_HANDLE;
    return m_passes[handle].renderPass;
}

VkFramebuffer RenderGraph::getFramebuffer(PassHandle handle, uint32_t imageIndex) const {
    if (handle >= m_passes.size()) return VK_NULL_HANDLE;
    const auto& framebuffers = m_passes[handle].framebuffers;
    if (framebuffers.empty()) return VK_NULL_HANDLE;
    uint32_t index = imageIndex < framebuffers.size() ? imageIndex : 0;
    return framebuffers[index];
}

void RenderGraph::clear() {
    cleanupPassRenderTargets();
    m_resources.clear();
    m_passes.clear();
    m_sortedIndices.clear();
}

} // namespace kazu
