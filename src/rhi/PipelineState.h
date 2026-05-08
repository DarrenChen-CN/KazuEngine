// ============================================================================
// KazuEngine - RHI Layer: PipelineState
//
// Immutable description of a complete graphics pipeline configuration.
// Used as the hash key for PipelineCache.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <functional>

namespace kazu {

struct PipelineState {
    // Shader paths (used to derive modules and reflection)
    std::vector<std::string> shaderPaths;

    // Render target
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;

    // Fixed-function states
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    float lineWidth = 1.0f;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

    // Vertex input (auto-generated from vertex shader reflection)
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    // Layouts (created externally and referenced)
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    bool operator==(const PipelineState& other) const {
        if (shaderPaths != other.shaderPaths) return false;
        if (renderPass != other.renderPass || subpass != other.subpass) return false;
        if (topology != other.topology || cullMode != other.cullMode) return false;
        if (frontFace != other.frontFace || polygonMode != other.polygonMode) return false;
        if (lineWidth != other.lineWidth || samples != other.samples) return false;

        // Manual vector comparison because Vk* structs have no operator==
        if (vertexBindings.size() != other.vertexBindings.size()) return false;
        for (size_t i = 0; i < vertexBindings.size(); ++i) {
            const auto& a = vertexBindings[i];
            const auto& b = other.vertexBindings[i];
            if (a.binding != b.binding || a.stride != b.stride || a.inputRate != b.inputRate) return false;
        }

        if (vertexAttributes.size() != other.vertexAttributes.size()) return false;
        for (size_t i = 0; i < vertexAttributes.size(); ++i) {
            const auto& a = vertexAttributes[i];
            const auto& b = other.vertexAttributes[i];
            if (a.location != b.location || a.binding != b.binding ||
                a.format != b.format || a.offset != b.offset) return false;
        }

        if (descriptorSetLayout != other.descriptorSetLayout) return false;
        if (pipelineLayout != other.pipelineLayout) return false;
        return true;
    }
};

struct PipelineStateHash {
    size_t operator()(const PipelineState& s) const {
        size_t h = s.shaderPaths.size();
        for (const auto& p : s.shaderPaths) {
            h ^= std::hash<std::string>{}(p) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        h ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(s.renderPass)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(s.subpass) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(s.topology)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(s.cullMode) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(s.frontFace)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(s.polygonMode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(*reinterpret_cast<const uint32_t*>(&s.lineWidth)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(s.samples)) + 0x9e3779b9 + (h << 6) + (h >> 2);

        for (const auto& b : s.vertexBindings) {
            h ^= std::hash<uint32_t>{}(b.binding) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(b.stride) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(b.inputRate)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        for (const auto& a : s.vertexAttributes) {
            h ^= std::hash<uint32_t>{}(a.location) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(a.binding) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(a.format)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(a.offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }

        h ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(s.descriptorSetLayout)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(s.pipelineLayout)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace kazu
