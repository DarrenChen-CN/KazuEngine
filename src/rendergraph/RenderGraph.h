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
#include "pass/Pass.h"

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
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0;
        VkImageCreateFlags flags = 0; // e.g. VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    };

    ResourceHandle addTexture(const char* name, const TextureDesc& desc);
    ResourceHandle addBuffer (const char* name);

    // Import an externally-managed image (e.g. Swapchain image).
    // The RenderGraph will not allocate memory for it, but it participates
    // in barrier derivation and can be queried like a regular texture.
    ResourceHandle addImportedTexture(const char* name, const TextureDesc& desc,
                                       VkImage image, VkImageView view,
                                       VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED);

    // Update the expected initial layout of an imported texture (e.g. for
    // ping-pong history buffers that persist across frames).
    void setImportedTextureLayout(ResourceHandle handle, VkImageLayout layout);

    // Re-bind the external image handles for an imported resource.
    // Called each frame when the Swapchain image index changes.
    void bindImportedTexture(ResourceHandle handle, VkImage image, VkImageView view);
    void setImportedTextureViews(ResourceHandle handle, const std::vector<VkImageView>& views);

    // ------------------------------------------------------------------------
    // Pass declaration
    // ------------------------------------------------------------------------
    enum class PassType { Graphics, Compute };

    enum class ResourceUsage {
        SampledRead,
        ColorAttachmentWrite,
        DepthAttachmentWrite,
        DepthSampledRead,
        StorageImageRead,
        StorageImageWrite,
        StorageBufferRead,
        StorageBufferWrite,
        Present
    };

    struct ResourceUse {
        ResourceHandle resource = InvalidResource;
        ResourceUsage usage = ResourceUsage::SampledRead;
        uint32_t slot = ~0u; // color attachment slot when applicable
    };

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
        void readStorageImage(ResourceHandle resource) {
            readStorageImages.push_back(resource);
        }
        void writeStorageImage(ResourceHandle resource) {
            writeStorageImages.push_back(resource);
        }

        std::function<void(const PassExecuteContext&)> execute;
        std::vector<ResourceHandle> reads;
        std::vector<std::pair<uint32_t, ResourceHandle>> writeColors;
        ResourceHandle writeDepth_ = InvalidResource;
        std::vector<ResourceHandle> readStorageImages;
        std::vector<ResourceHandle> writeStorageImages;
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
    void execute(const PassExecuteContext& ctx) const;

    // ------------------------------------------------------------------------
    // Queries
    // ------------------------------------------------------------------------
    const char* getResourceName(ResourceHandle handle) const;
    const char* getPassName(PassHandle handle) const;
    size_t      getPassCount() const { return m_passes.size(); }
    bool        isCompiled()   const { return !m_sortedIndices.empty(); }
    void        dumpDebugInfo() const;

    // Resource queries (valid after compile())
    Image*      getImage(ResourceHandle handle) const;
    VkImage     getImageHandle(ResourceHandle handle) const;
    VkImageView getImageView(ResourceHandle handle) const;
    VkExtent2D  getImageExtent(ResourceHandle handle) const;
    VkFormat    getImageFormat(ResourceHandle handle) const;
    VkRenderPass getRenderPass(PassHandle handle) const;
    VkFramebuffer getFramebuffer(PassHandle handle, uint32_t imageIndex = 0) const;

    void clear();

private:
    enum class ResourceOwnership {
        Transient,
        Imported
    };

    struct ImportedTexture {
        VkImage currentImage = VK_NULL_HANDLE;
        VkImageView currentView = VK_NULL_HANDLE;
        std::vector<VkImageView> allViews;
        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct ResourceNode {
        std::string name;
        TextureDesc desc;              // valid only for textures (format != UNDEFINED)
        ResourceOwnership ownership = ResourceOwnership::Transient;
        std::unique_ptr<Image> ownedImage; // allocated after compile() for transient textures
        ImportedTexture imported;          // valid only when ownership == Imported
    };

    // Barrier description (image handle is resolved at execute time)
    struct BarrierDesc {
        ResourceHandle resource;
        VkImageLayout oldLayout;
        VkImageLayout newLayout;
        VkAccessFlags srcAccess;
        VkAccessFlags dstAccess;
        VkPipelineStageFlags srcStage;
        VkPipelineStageFlags dstStage;
    };

    struct BarrierBatch {
        VkPipelineStageFlags srcStage = 0;
        VkPipelineStageFlags dstStage = 0;
        std::vector<BarrierDesc> descs;
    };

    struct PassNode {
        std::string name;
        PassType type = PassType::Graphics;
        std::function<void(const PassExecuteContext&)> execute;
        std::vector<ResourceUse> usages;
        BarrierBatch preBarrier;            // filled by deriveBarriers()
        std::vector<ResourceHandle> framebufferResources;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> framebuffers;
    };

    Context* m_ctx = nullptr;
    std::vector<ResourceNode> m_resources;
    std::vector<PassNode>     m_passes;
    std::vector<uint32_t>     m_sortedIndices; // topological order of pass indices

    bool hasDependency(uint32_t writerIdx, uint32_t readerIdx) const;
    bool passReadsResource(const PassNode& pass, ResourceHandle resource) const;
    bool passWritesResource(const PassNode& pass, ResourceHandle resource) const;
    void allocateResources();
    void deriveBarriers();
    void createPassRenderTargets();
    void cleanupPassRenderTargets();
};

} // namespace kazu
