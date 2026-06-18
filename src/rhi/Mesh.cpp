// ============================================================================
// KazuEngine - RHI Layer: Mesh (Implementation)
// ============================================================================

#include "Mesh.h"
#include "../core/Utils.h"
#include "../core/CommandBuffer.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace kazu {

// ============================================================================
// Vertex Layout Description
// ============================================================================

VkVertexInputBindingDescription Vertex::getBindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attrs(3);
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, position);

    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, normal);

    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(Vertex, texCoord);
    return attrs;
}

// ============================================================================
// Mesh
// ============================================================================

Mesh::Mesh(Context& ctx, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : m_vertexCount(static_cast<uint32_t>(vertices.size()))
    , m_indexCount(static_cast<uint32_t>(indices.size()))
{
    // Compute local-space bounds from source vertices.
    for (const auto& v : vertices) {
        m_bounds.expand(v.position);
    }

    VkDeviceSize vertexSize = sizeof(Vertex) * vertices.size();
    VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

    // Staging upload for vertices
    Buffer stagingVertex(ctx, vertexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingVertex.upload(vertices.data(), vertexSize);

    m_vertexBuffer = std::make_unique<Buffer>(ctx, vertexSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Staging upload for indices
    Buffer stagingIndex(ctx, indexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingIndex.upload(indices.data(), indexSize);

    m_indexBuffer = std::make_unique<Buffer>(ctx, indexSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Copy via transient command buffer
    CommandBuffer cmd(ctx, ctx.transientPool());
    cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkBufferCopy vertexCopy{};
    vertexCopy.size = vertexSize;
    vkCmdCopyBuffer(cmd.handle(), stagingVertex.handle(), m_vertexBuffer->handle(), 1, &vertexCopy);

    VkBufferCopy indexCopy{};
    indexCopy.size = indexSize;
    vkCmdCopyBuffer(cmd.handle(), stagingIndex.handle(), m_indexBuffer->handle(), 1, &indexCopy);

    cmd.end();
    cmd.submit(ctx.graphicsQueue());
    vkQueueWaitIdle(ctx.graphicsQueue());
}

void Mesh::draw(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = { m_vertexBuffer->handle() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

Mesh Mesh::loadObj(Context& ctx, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        fatalError("Failed to open OBJ file: " + path);
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<glm::vec3> normals;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Deduplication: map from "v/vt/vn" index triple to output vertex index
    struct IndexTriple {
        uint32_t v, vt, vn;
        bool operator==(const IndexTriple& other) const {
            return v == other.v && vt == other.vt && vn == other.vn;
        }
    };
    struct IndexTripleHash {
        size_t operator()(const IndexTriple& t) const {
            return std::hash<uint32_t>()(t.v) ^ (std::hash<uint32_t>()(t.vt) << 1) ^ (std::hash<uint32_t>()(t.vn) << 2);
        }
    };
    std::unordered_map<IndexTriple, uint32_t, IndexTripleHash> indexMap;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            glm::vec3 p;
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (prefix == "vt") {
            glm::vec2 t;
            iss >> t.x >> t.y;
            texCoords.push_back(t);
        } else if (prefix == "vn") {
            glm::vec3 n;
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (prefix == "f") {
            // Parse face: "f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3 ..."
            std::vector<IndexTriple> faceIndices;
            std::string token;
            while (iss >> token) {
                IndexTriple tri{0, 0, 0};
                size_t firstSlash = token.find('/');
                if (firstSlash == std::string::npos) {
                    // v only
                    tri.v = static_cast<uint32_t>(std::stoul(token));
                } else {
                    tri.v = static_cast<uint32_t>(std::stoul(token.substr(0, firstSlash)));
                    size_t secondSlash = token.find('/', firstSlash + 1);
                    if (secondSlash == std::string::npos) {
                        // v/vt
                        if (firstSlash + 1 < token.size()) {
                            tri.vt = static_cast<uint32_t>(std::stoul(token.substr(firstSlash + 1)));
                        }
                    } else {
                        // v/vt/vn
                        if (firstSlash + 1 < secondSlash) {
                            tri.vt = static_cast<uint32_t>(std::stoul(token.substr(firstSlash + 1, secondSlash - firstSlash - 1)));
                        }
                        if (secondSlash + 1 < token.size()) {
                            tri.vn = static_cast<uint32_t>(std::stoul(token.substr(secondSlash + 1)));
                        }
                    }
                }
                faceIndices.push_back(tri);
            }

            // Triangulate if needed (fan triangulation)
            for (size_t i = 2; i < faceIndices.size(); ++i) {
                for (size_t corner : {0ULL, i - 1, i}) {
                    const auto& tri = faceIndices[corner];
                    auto it = indexMap.find(tri);
                    if (it != indexMap.end()) {
                        indices.push_back(it->second);
                    } else {
                        Vertex vert{};
                        if (tri.v > 0 && tri.v <= positions.size()) {
                            vert.position = positions[tri.v - 1];
                        }
                        if (tri.vt > 0 && tri.vt <= texCoords.size()) {
                            vert.texCoord = texCoords[tri.vt - 1];
                        }
                        if (tri.vn > 0 && tri.vn <= normals.size()) {
                            vert.normal = normals[tri.vn - 1];
                        }
                        uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                        vertices.push_back(vert);
                        indexMap[tri] = newIndex;
                        indices.push_back(newIndex);
                    }
                }
            }
        }
    }

    spdlog::info("[Mesh] Loaded OBJ: {} ({} vertices, {} indices)", path, vertices.size(), indices.size());
    return Mesh(ctx, vertices, indices);
}

} // namespace kazu
