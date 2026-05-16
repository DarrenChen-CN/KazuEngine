// ============================================================================
// KazuEngine - RenderGraph: Core Implementation
// ============================================================================

#include "rendergraph/RenderGraph.h"
#include "core/Context.h"
#include <spdlog/spdlog.h>
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

// ============================================================================
// Construction
// ============================================================================

RenderGraph::RenderGraph(Context& ctx) : m_ctx(&ctx) {}

RenderGraph::~RenderGraph() = default;

// ============================================================================
// Resource declaration
// ============================================================================

RenderGraph::ResourceHandle RenderGraph::addTexture(const char* name, const TextureDesc& desc) {
    ResourceHandle handle = static_cast<ResourceHandle>(m_resources.size());
    ResourceNode node;
    node.name = name ? name : "";
    node.desc = desc;
    m_resources.push_back(std::move(node));
    return handle;
}

RenderGraph::ResourceHandle RenderGraph::addBuffer(const char* name) {
    ResourceHandle handle = static_cast<ResourceHandle>(m_resources.size());
    ResourceNode node;
    node.name = name ? name : "";
    // desc defaults keep format=UNDEFINED, marking this as a buffer
    m_resources.push_back(std::move(node));
    return handle;
}

// ============================================================================
// Pass declaration
// ============================================================================

RenderGraph::PassHandle RenderGraph::addPass(const char* name, PassSetupFn setup) {
    PassBuilder builder;
    setup(builder);

    PassNode node;
    node.name    = name ? name : "";
    node.execute = builder.execute;
    node.reads   = std::move(builder.reads);

    // Flatten writes: color attachments + depth
    for (auto& [slot, res] : builder.writeColors) {
        (void)slot; // slot index preserved in builder for future subpass merging
        node.writes.push_back(res);
    }
    if (builder.writeDepth_ != InvalidResource) {
        node.writes.push_back(builder.writeDepth_);
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
    // Release any previously allocated transient images
    for (auto& res : m_resources) {
        res.image.reset();
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
        for (ResourceHandle r : pass.reads) touch(r);
        for (ResourceHandle w : pass.writes) touch(w);
    }

    for (uint32_t i = 0; i < resCount; ++i) {
        auto& res = m_resources[i];
        if (res.desc.format == VK_FORMAT_UNDEFINED) continue; // buffer, not texture
        if (firstPass[i] == -1) continue; // unused, don't allocate

        res.image = std::make_unique<Image>(*m_ctx,
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

static UsageInfo getWriteUsageInfo(bool isDepth) {
    if (isDepth) {
        return {
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
    } else {
        return {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
    }
}

static UsageInfo getReadUsageInfo() {
    return {
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
}

void RenderGraph::deriveBarriers() {
    if (m_sortedIndices.empty()) return;

    std::vector<ResourceState> states(m_resources.size());

    for (uint32_t sortedIdx : m_sortedIndices) {
        auto& pass = m_passes[sortedIdx];
        pass.preBarrier = {}; // clear any previous barriers

        auto emitBarrier = [&](ResourceHandle rh, const ResourceState& oldState, const UsageInfo& newUsage) {
            const auto& res = m_resources[rh];
            if (!res.image) return; // buffer or unallocated

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldState.layout;
            barrier.newLayout = newUsage.layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = res.image->handle();
            barrier.subresourceRange.aspectMask = aspectMaskFromFormat(res.desc.format);
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = oldState.access;
            barrier.dstAccessMask = newUsage.access;

            pass.preBarrier.barriers.push_back(barrier);
            pass.preBarrier.srcStage |= oldState.stage;
            pass.preBarrier.dstStage |= newUsage.stage;
        };

        // Process reads first.
        // Note: in the current simplified model, a resource is never both read
        // and written in the same pass. If that changes, writes should take
        // precedence (they represent the final state of the pass).
        for (ResourceHandle rh : pass.reads) {
            if (rh >= m_resources.size()) continue;
            const auto& res = m_resources[rh];
            if (res.desc.format == VK_FORMAT_UNDEFINED) continue; // buffer

            const auto& oldState = states[rh];
            UsageInfo readInfo = getReadUsageInfo();
            bool isDepth = (res.desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            if (isDepth) readInfo.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            emitBarrier(rh, oldState, readInfo);
            states[rh] = {readInfo.layout, readInfo.stage, readInfo.access};
        }

        // Process writes
        for (ResourceHandle rh : pass.writes) {
            if (rh >= m_resources.size()) continue;
            const auto& res = m_resources[rh];
            if (res.desc.format == VK_FORMAT_UNDEFINED) continue; // buffer

            bool isDepth = (res.desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            const auto& oldState = states[rh];
            UsageInfo writeInfo = getWriteUsageInfo(isDepth);
            emitBarrier(rh, oldState, writeInfo);
            states[rh] = {writeInfo.layout, writeInfo.stage, writeInfo.access};
        }

        spdlog::debug("[RenderGraph] Pass '{}' pre-barriers: {} (srcStage=0x{:x}, dstStage=0x{:x})",
                      pass.name, pass.preBarrier.barriers.size(),
                      pass.preBarrier.srcStage, pass.preBarrier.dstStage);
    }
}

// Returns true if pass 'reader' must execute after pass 'writer'
// because writer produces a resource that reader consumes.
bool RenderGraph::hasDependency(uint32_t writerIdx, uint32_t readerIdx) const {
    const auto& writer = m_passes[writerIdx];
    const auto& reader = m_passes[readerIdx];

    for (ResourceHandle w : writer.writes) {
        for (ResourceHandle r : reader.reads) {
            if (w == r) return true;
        }
        // WAW (write-after-write): second writer depends on first
        // This ensures deterministic ordering for shared outputs.
        for (ResourceHandle rw : reader.writes) {
            if (w == rw) return true;
        }
    }
    return false;
}

// ============================================================================
// Execution
// ============================================================================

void RenderGraph::execute(VkCommandBuffer cmd) const {
    if (!isCompiled()) return;

    for (uint32_t idx : m_sortedIndices) {
        const auto& pass = m_passes[idx];

        if (!pass.preBarrier.barriers.empty()) {
            vkCmdPipelineBarrier(cmd,
                pass.preBarrier.srcStage,
                pass.preBarrier.dstStage,
                0,
                0, nullptr,
                0, nullptr,
                static_cast<uint32_t>(pass.preBarrier.barriers.size()),
                pass.preBarrier.barriers.data());
        }

        if (pass.execute) {
            pass.execute(cmd);
        }
    }
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

VkImageView RenderGraph::getImageView(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return VK_NULL_HANDLE;
    const auto& res = m_resources[handle];
    if (!res.image) return VK_NULL_HANDLE;
    return res.image->view();
}

VkExtent2D RenderGraph::getImageExtent(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return {};
    const auto& res = m_resources[handle];
    if (!res.image) return {};
    return res.image->extent();
}

VkFormat RenderGraph::getImageFormat(ResourceHandle handle) const {
    if (handle >= m_resources.size()) return VK_FORMAT_UNDEFINED;
    const auto& res = m_resources[handle];
    if (!res.image) return VK_FORMAT_UNDEFINED;
    return res.image->format();
}

void RenderGraph::clear() {
    m_resources.clear();
    m_passes.clear();
    m_sortedIndices.clear();
}

} // namespace kazu
