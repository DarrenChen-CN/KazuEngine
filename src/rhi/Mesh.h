// ============================================================================
// KazuEngine - RHI Layer: Mesh
//
// Encapsulates a single geometry: vertex buffer + optional index buffer.
// Includes a minimal OBJ loader (static method).
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../core/Buffer.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace kazu {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

class Mesh {
public:
    Mesh(Context& ctx, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    void draw(VkCommandBuffer cmd) const;

    // Minimal OBJ loader: parses v/vt/vn/f, triangulates quads, deduplicates vertices.
    static Mesh loadObj(Context& ctx, const std::string& path);

private:
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
};

} // namespace kazu
