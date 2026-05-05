// ============================================================================
// KazuEngine - Core Layer: ShaderModule
//
// RAII wrapper for VkShaderModule.
// ============================================================================

#pragma once

#include "Context.h"
#include <vector>

namespace kazu {

class ShaderModule {
public:
    ShaderModule(Context& ctx, const std::vector<char>& code);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&& other) noexcept;
    ShaderModule& operator=(ShaderModule&& other) noexcept;

    VkShaderModule handle() const { return m_module; }

private:
    Context* m_ctx = nullptr;
    VkShaderModule m_module = VK_NULL_HANDLE;
};

} // namespace kazu
