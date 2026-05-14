// ============================================================================
// KazuEngine - RenderGraph: Core Implementation
// ============================================================================

#include "rendergraph/RenderGraph.h"
#include "core/Context.h"
#include <queue>
#include <unordered_set>

namespace kazu {

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
