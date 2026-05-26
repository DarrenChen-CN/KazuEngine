// ============================================================================
// KazuEngine - RHI Layer: PipelineState
//
// Immutable description of a complete graphics pipeline configuration.
// Used as the hash key for PipelineCache.
//
// NOTE: Every field in VkGraphicsPipelineCreateInfo that varies between
// pipelines MUST be present here, otherwise PipelineCache may return
// the wrong cached pipeline.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <functional>

namespace kazu {

// Per-color-attachment blend configuration
struct ColorBlendAttachment {
    bool blendEnable = false;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;
    VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    bool operator==(const ColorBlendAttachment& other) const {
        return blendEnable == other.blendEnable
            && srcColorBlendFactor == other.srcColorBlendFactor
            && dstColorBlendFactor == other.dstColorBlendFactor
            && colorBlendOp == other.colorBlendOp
            && srcAlphaBlendFactor == other.srcAlphaBlendFactor
            && dstAlphaBlendFactor == other.dstAlphaBlendFactor
            && alphaBlendOp == other.alphaBlendOp
            && colorWriteMask == other.colorWriteMask;
    }
};

struct PipelineState {
    // Shader paths (used to derive modules and reflection)
    std::vector<std::string> shaderPaths;

    // Render target
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;

    // Fixed-function: Input Assembly
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Fixed-function: Rasterization
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    float lineWidth = 1.0f;
    bool depthClampEnable = false;
    bool rasterizerDiscardEnable = false;
    bool depthBiasEnable = false;

    // Fixed-function: Multisample
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool sampleShadingEnable = false;

    // Fixed-function: Depth/Stencil
    bool depthTest = true;
    bool depthWrite = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

    // Fixed-function: Color Blend (per attachment)
    std::vector<ColorBlendAttachment> colorBlendAttachments;
    bool logicOpEnable = false;
    VkLogicOp logicOp = VK_LOGIC_OP_COPY;

    // Fixed-function: Dynamic State
    std::vector<VkDynamicState> dynamicStates;

    // Vertex input (auto-generated from vertex shader reflection)
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    // Layouts (created externally and referenced)
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    bool operator==(const PipelineState& other) const {
        if (shaderPaths != other.shaderPaths) return false;
        if (renderPass != other.renderPass || subpass != other.subpass) return false;
        if (topology != other.topology) return false;
        if (cullMode != other.cullMode || frontFace != other.frontFace || polygonMode != other.polygonMode) return false;
        if (lineWidth != other.lineWidth) return false;
        if (depthClampEnable != other.depthClampEnable) return false;
        if (rasterizerDiscardEnable != other.rasterizerDiscardEnable) return false;
        if (depthBiasEnable != other.depthBiasEnable) return false;
        if (samples != other.samples || sampleShadingEnable != other.sampleShadingEnable) return false;
        if (depthTest != other.depthTest || depthWrite != other.depthWrite || depthCompareOp != other.depthCompareOp) return false;
        if (colorBlendAttachments != other.colorBlendAttachments) return false;
        if (logicOpEnable != other.logicOpEnable || logicOp != other.logicOp) return false;
        if (dynamicStates != other.dynamicStates) return false;

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
        h ^= std::hash<bool>{}(s.depthClampEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(s.rasterizerDiscardEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(s.depthBiasEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(s.samples)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(s.sampleShadingEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(s.depthTest) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(s.depthWrite) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(s.depthCompareOp)) + 0x9e3779b9 + (h << 6) + (h >> 2);

        for (const auto& c : s.colorBlendAttachments) {
            h ^= std::hash<bool>{}(c.blendEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(c.srcColorBlendFactor)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(c.dstColorBlendFactor)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(c.colorBlendOp)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(c.srcAlphaBlendFactor)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(c.dstAlphaBlendFactor)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(c.alphaBlendOp)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(c.colorWriteMask) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        h ^= std::hash<bool>{}(s.logicOpEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(s.logicOp)) + 0x9e3779b9 + (h << 6) + (h >> 2);

        for (const auto& d : s.dynamicStates) {
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(d)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }

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
