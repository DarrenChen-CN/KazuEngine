// ============================================================================
// KazuEngine - RHI Layer: ShaderLibrary (Implementation)
// ============================================================================

#include "ShaderLibrary.h"
#include "../core/Utils.h"
#include <spirv_reflect.h>
#include <spdlog/spdlog.h>
#include <fstream>

namespace kazu {

ShaderLibrary::ShaderLibrary(Context& ctx) : m_ctx(&ctx) {}

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        fatalError("Failed to open shader file: " + filename);
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule ShaderLibrary::load(const std::string& path) {
    if (m_modules.count(path)) {
        return m_modules[path]->handle();
    }

    auto code = readFile(path);
    auto module = std::make_unique<ShaderModule>(*m_ctx, code);
    VkShaderModule handle = module->handle();
    m_modules[path] = std::move(module);
    m_reflections[path] = reflectSPIRV(code);

    spdlog::info("[ShaderLibrary] Loaded: {} (stage: {}, bindings: {}, pushConstants: {}, vertexInputs: {})",
                 path,
                 static_cast<int>(m_reflections[path].stage),
                 m_reflections[path].descriptorBindings.size(),
                 m_reflections[path].pushConstantRanges.size(),
                 m_reflections[path].vertexInputs.size());

    return handle;
}

bool ShaderLibrary::has(const std::string& path) const {
    return m_modules.count(path) > 0;
}

VkShaderModule ShaderLibrary::getModule(const std::string& path) const {
    auto it = m_modules.find(path);
    if (it == m_modules.end()) {
        fatalError("Shader not loaded: " + path);
    }
    return it->second->handle();
}

VkShaderStageFlagBits ShaderLibrary::getStage(const std::string& path) const {
    auto it = m_reflections.find(path);
    if (it == m_reflections.end()) {
        fatalError("Shader not loaded: " + path);
    }
    return it->second.stage;
}

const ShaderReflection& ShaderLibrary::getReflection(const std::string& path) const {
    auto it = m_reflections.find(path);
    if (it == m_reflections.end()) {
        fatalError("Shader not loaded: " + path);
    }
    return it->second;
}

void ShaderLibrary::logReflections() const {
    for (const auto& [path, refl] : m_reflections) {
        spdlog::info("[ShaderReflection] {} ({}) - entry: '{}'", path, static_cast<int>(refl.stage), refl.entryPoint);
        for (const auto& b : refl.descriptorBindings) {
            spdlog::info("  binding: set={}, binding={}, type={}, count={}, name='{}'",
                         b.set, b.binding, static_cast<int>(b.descriptorType), b.count, b.name);
        }
        for (const auto& p : refl.pushConstantRanges) {
            spdlog::info("  pushConstant: offset={}, size={}, stage={}",
                         p.offset, p.size, static_cast<int>(p.stageFlags));
        }
        for (const auto& v : refl.vertexInputs) {
            spdlog::info("  vertexInput: location={}, format={}, offset={}, name='{}'",
                         v.location, static_cast<int>(v.format), v.offset, v.name);
        }
    }
}

ShaderReflection ShaderLibrary::reflectSPIRV(const std::vector<char>& code) {
    SpvReflectShaderModule spvModule;
    SpvReflectResult result = spvReflectCreateShaderModule(
        code.size(), code.data(), &spvModule);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        fatalError("SPIRV-Reflect failed to parse shader module");
    }

    ShaderReflection refl;
    refl.stage = static_cast<VkShaderStageFlagBits>(spvModule.shader_stage);
    refl.entryPoint = spvModule.entry_point_name ? spvModule.entry_point_name : "main";

    // Descriptor bindings
    uint32_t bindingCount = 0;
    spvReflectEnumerateDescriptorBindings(&spvModule, &bindingCount, nullptr);
    if (bindingCount > 0) {
        std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
        spvReflectEnumerateDescriptorBindings(&spvModule, &bindingCount, bindings.data());
        for (uint32_t i = 0; i < bindingCount; ++i) {
            const auto* b = bindings[i];
            ShaderDescriptorBinding db{};
            db.set = b->set;
            db.binding = b->binding;
            db.descriptorType = static_cast<VkDescriptorType>(b->descriptor_type);
            db.count = b->count;
            db.stageFlags = static_cast<VkShaderStageFlags>(spvModule.shader_stage);
            db.name = b->name ? b->name : "";
            refl.descriptorBindings.push_back(db);
        }
    }

    // Push constants
    uint32_t pcCount = 0;
    spvReflectEnumeratePushConstantBlocks(&spvModule, &pcCount, nullptr);
    if (pcCount > 0) {
        std::vector<SpvReflectBlockVariable*> pcs(pcCount);
        spvReflectEnumeratePushConstantBlocks(&spvModule, &pcCount, pcs.data());
        for (uint32_t i = 0; i < pcCount; ++i) {
            const auto* pc = pcs[i];
            ShaderPushConstantRange range{};
            range.offset = pc->offset;
            range.size = pc->size;
            range.stageFlags = static_cast<VkShaderStageFlags>(spvModule.shader_stage);
            refl.pushConstantRanges.push_back(range);
        }
    }

    // Output variables (for fragment shaders: determines MRT attachment count)
    if (spvModule.shader_stage == SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT) {
        uint32_t outputCount = 0;
        spvReflectEnumerateOutputVariables(&spvModule, &outputCount, nullptr);
        if (outputCount > 0) {
            std::vector<SpvReflectInterfaceVariable*> outputs(outputCount);
            spvReflectEnumerateOutputVariables(&spvModule, &outputCount, outputs.data());
            for (uint32_t i = 0; i < outputCount; ++i) {
                if (outputs[i]->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
                    continue;
                }
                refl.outputAttachmentCount++;
            }
        }
    }

    // Vertex input variables (only for vertex shaders)
    if (spvModule.shader_stage == SPV_REFLECT_SHADER_STAGE_VERTEX_BIT) {
        uint32_t inputCount = 0;
        spvReflectEnumerateInputVariables(&spvModule, &inputCount, nullptr);
        if (inputCount > 0) {
            std::vector<SpvReflectInterfaceVariable*> inputs(inputCount);
            spvReflectEnumerateInputVariables(&spvModule, &inputCount, inputs.data());
            for (uint32_t i = 0; i < inputCount; ++i) {
                const auto* in = inputs[i];
                // Skip built-in variables (e.g. gl_VertexIndex)
                if (in->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
                    continue;
                }
                ShaderVertexInputAttribute attr{};
                attr.location = in->location;
                attr.format = static_cast<VkFormat>(in->format);
                attr.offset = 0; // SPIRV-Reflect doesn't provide offset directly for interface vars
                attr.name = in->name ? in->name : "";
                refl.vertexInputs.push_back(attr);
            }
        }
    }

    spvReflectDestroyShaderModule(&spvModule);
    return refl;
}

} // namespace kazu
