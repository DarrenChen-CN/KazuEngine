// ============================================================================
// KazuEngine - RenderGraph: Core
//
// Declarative rendering framework: describe Pass read/write resources,
// let the framework compute execution order and (future) barriers.
//
// Week 4.1: addPass / compile (topological sort) / execute
// Week 4.2: Transient Resource allocation (simplified, no aliasing)
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

#include "core/Image.h"

namespace kazu {

class Context;

class RenderGraph {
public:
    using ResourceHandle = uint32_t;
    using PassHandle     = uint32_t;
    static constexpr ResourceHandle InvalidResource = ~0u;

    explicit RenderGraph(Context& ctx);
    ~RenderGraph();

    // ------------------------------------------------------------------------
    // Resource declaration (logical handles; GPU allocation happens at compile)
    // ------------------------------------------------------------------------
    struct TextureDesc {
        uint32_t width  = 0;
        uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0;
    };

    ResourceHandle addTexture(const char* name, const TextureDesc& desc);
    ResourceHandle addBuffer (const char* name);

    // ------------------------------------------------------------------------
    // Pass declaration
    // ------------------------------------------------------------------------
    enum class PassType { Graphics, Compute };

    struct PassBuilder {
        PassType type = PassType::Graphics;

        void read(ResourceHandle resource) {
            reads.push_back(resource);
        }
        void writeColor(uint32_t slot, ResourceHandle resource) {
            writeColors.push_back({slot, resource});
        }
        void writeDepth(ResourceHandle resource) {
            writeDepth_ = resource;
        }

        std::function<void(VkCommandBuffer)> execute;
        std::vector<ResourceHandle> reads;
        std::vector<std::pair<uint32_t, ResourceHandle>> writeColors;
        ResourceHandle writeDepth_ = InvalidResource;
    };

    using PassSetupFn = std::function<void(PassBuilder&)>;

    PassHandle addPass(const char* name, PassSetupFn setup);

    // ------------------------------------------------------------------------
    // Compilation: topological sort + transient resource allocation
    // Returns false if a cycle is detected.
    // ------------------------------------------------------------------------
    bool compile();

    // ------------------------------------------------------------------------
    // Execution: iterate passes in topological order, invoke user lambdas
    // ------------------------------------------------------------------------
    void execute(VkCommandBuffer cmd) const;

    // ------------------------------------------------------------------------
    // Queries
    // ------------------------------------------------------------------------
    const char* getResourceName(ResourceHandle handle) const;
    const char* getPassName(PassHandle handle) const;
    size_t      getPassCount() const { return m_passes.size(); }
    bool        isCompiled()   const { return !m_sortedIndices.empty(); }

    // Transient resource queries (valid after compile())
    VkImageView getImageView(ResourceHandle handle) const;
    VkExtent2D  getImageExtent(ResourceHandle handle) const;
    VkFormat    getImageFormat(ResourceHandle handle) const;

    void clear();

private:
    struct ResourceNode {
        std::string name;
        TextureDesc desc;              // valid only for textures (format != UNDEFINED)
        std::unique_ptr<Image> image;  // allocated after compile()
    };

    struct BarrierBatch {
        VkPipelineStageFlags srcStage = 0;
        VkPipelineStageFlags dstStage = 0;
        std::vector<VkImageMemoryBarrier> barriers;
    };

    struct PassNode {
        std::string name;
        std::function<void(VkCommandBuffer)> execute;
        std::vector<ResourceHandle> reads;
        std::vector<ResourceHandle> writes; // flattened: colors + depth
        BarrierBatch preBarrier;            // filled by deriveBarriers()
    };

    Context* m_ctx = nullptr;
    std::vector<ResourceNode> m_resources;
    std::vector<PassNode>     m_passes;
    std::vector<uint32_t>     m_sortedIndices; // topological order of pass indices

    bool hasDependency(uint32_t writerIdx, uint32_t readerIdx) const;
    void allocateResources();
    void deriveBarriers();
};

} // namespace kazu
