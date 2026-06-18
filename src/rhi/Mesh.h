// ============================================================================
// KazuEngine - RHI Layer: Mesh
//
// Encapsulates a single geometry: vertex buffer + optional index buffer.
// Includes a minimal OBJ loader (static method).
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../core/Buffer.h"
#include "Bounds.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace kazu {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;

    // Exact Vulkan vertex input layout for this struct.
    // Use these instead of SPIR-V reflection guesswork to ensure
    // C++ memory layout and GPU pipeline state match precisely.
    static VkVertexInputBindingDescription getBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
};

class Mesh {
public:
    Mesh(Context& ctx, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    void draw(VkCommandBuffer cmd) const;

    // Minimal OBJ loader: parses v/vt/vn/f, triangulates quads, deduplicates vertices.
    static Mesh loadObj(Context& ctx, const std::string& path);

    // Local-space axis-aligned bounds computed from vertex positions.
    const Bounds& bounds() const { return m_bounds; }

private:
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    Bounds m_bounds;
};

} // namespace kazu
