// ============================================================================
// KazuEngine - Core Layer: ShaderModule (Implementation)
// ============================================================================

#include "ShaderModule.h"
#include "Utils.h"

namespace kazu {

ShaderModule::ShaderModule(Context& ctx, const std::vector<char>& code)
    : m_ctx(&ctx)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VK_CHECK(vkCreateShaderModule(m_ctx->device(), &createInfo, nullptr, &m_module));
}

ShaderModule::~ShaderModule() {
    if (m_module != VK_NULL_HANDLE && m_ctx) {
        vkDestroyShaderModule(m_ctx->device(), m_module, nullptr);
        m_module = VK_NULL_HANDLE;
    }
}

ShaderModule::ShaderModule(ShaderModule&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_module(other.m_module)
{
    other.m_module = VK_NULL_HANDLE;
}

ShaderModule& ShaderModule::operator=(ShaderModule&& other) noexcept {
    if (this != &other) {
        this->~ShaderModule();
        m_ctx = other.m_ctx;
        m_module = other.m_module;
        other.m_module = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace kazu
