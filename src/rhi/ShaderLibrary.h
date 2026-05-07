// ============================================================================
// KazuEngine - RHI Layer: ShaderLibrary
//
// Loads SPIR-V shaders, caches ShaderModules, and provides reflection data.
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../core/ShaderModule.h"
#include "ShaderReflection.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace kazu {

class ShaderLibrary {
public:
    explicit ShaderLibrary(Context& ctx);
    ~ShaderLibrary() = default;

    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;
    ShaderLibrary(ShaderLibrary&&) = default;
    ShaderLibrary& operator=(ShaderLibrary&&) = default;

    // Load SPIR-V from file. Returns raw handle for convenience.
    // Caches both ShaderModule and reflection data.
    VkShaderModule load(const std::string& path);

    // Query cached data
    bool has(const std::string& path) const;
    VkShaderModule getModule(const std::string& path) const;
    VkShaderStageFlagBits getStage(const std::string& path) const;
    const ShaderReflection& getReflection(const std::string& path) const;

    // Debug: log reflection info for all loaded shaders
    void logReflections() const;

private:
    Context* m_ctx = nullptr;
    std::unordered_map<std::string, std::unique_ptr<ShaderModule>> m_modules;
    std::unordered_map<std::string, ShaderReflection> m_reflections;

    ShaderReflection reflectSPIRV(const std::vector<char>& code);
};

} // namespace kazu
