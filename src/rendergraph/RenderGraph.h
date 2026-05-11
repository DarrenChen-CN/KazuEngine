// ============================================================================
// KazuEngine - RenderGraph: Core
//
// Declarative rendering framework: describe Pass read/write resources,
// let the framework compute execution order and (future) barriers.
//
// Week 4.1: addPass / compile (topological sort) / execute
// Week 4.2+: Barrier derivation, transient resource allocation
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace kazu {

class RenderGraph {
public:
    using ResourceHandle = uint32_t;
    using PassHandle     = uint32_t;
    static constexpr ResourceHandle InvalidResource = ~0u;

    // ------------------------------------------------------------------------
    // Resource declaration (logical handles only; no GPU allocation in 4.1)
    // ------------------------------------------------------------------------
    ResourceHandle addTexture(const char* name, VkFormat format);
    ResourceHandle addBuffer (const char* name);

    // ------------------------------------------------------------------------
    // Pass declaration
    // ------------------------------------------------------------------------
    struct PassBuilder {
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
    // Compilation: topological sort based on resource dependencies
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

    void clear();

private:
    struct ResourceNode {
        std::string name;
    };

    struct PassNode {
        std::string name;
        std::function<void(VkCommandBuffer)> execute;
        std::vector<ResourceHandle> reads;
        std::vector<ResourceHandle> writes; // flattened: colors + depth
    };

    std::vector<ResourceNode> m_resources;
    std::vector<PassNode>     m_passes;
    std::vector<uint32_t>     m_sortedIndices; // topological order of pass indices

    bool hasDependency(uint32_t writerIdx, uint32_t readerIdx) const;
};

} // namespace kazu
