// ============================================================================
// KazuEngine - RHI Layer: ShaderReflection
//
// Simplified reflection data extracted from SPIR-V via SPIRV-Reflect.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace kazu {

struct ShaderDescriptorBinding {
    uint32_t set = 0;
    uint32_t binding = 0;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t count = 1;
    VkShaderStageFlags stageFlags = 0;
    std::string name;
};

inline uint32_t formatSize(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R32_SFLOAT: return 4;
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32_SFLOAT: return 12;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return 4;
        case VK_FORMAT_R8G8_UNORM: return 2;
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R16_SFLOAT: return 2;
        case VK_FORMAT_R16G16_SFLOAT: return 4;
        case VK_FORMAT_R16G16B16_SFLOAT: return 6;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
            return 4;
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
            return 8;
        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32_SINT:
            return 12;
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
            return 16;
        default: return 4;
    }
}

struct ShaderPushConstantRange {
    uint32_t offset = 0;
    uint32_t size = 0;
    VkShaderStageFlags stageFlags = 0;
};

struct ShaderVertexInputAttribute {
    uint32_t location = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t offset = 0;
    std::string name;
};

struct ShaderReflection {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_ALL;
    std::vector<ShaderDescriptorBinding> descriptorBindings;
    std::vector<ShaderPushConstantRange> pushConstantRanges;
    std::vector<ShaderVertexInputAttribute> vertexInputs;
    std::string entryPoint = "main";
    uint32_t outputAttachmentCount = 0; // fragment shader MRT outputs (non-built-in)
};

} // namespace kazu
