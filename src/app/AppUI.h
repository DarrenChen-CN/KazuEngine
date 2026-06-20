// ============================================================================
// KazuEngine - App Layer: ImGui Integration
//
// Design: docs/knowledge/04-04-imgui-integration.md
// Only this module includes <imgui.h>. Technique layer remains ImGui-free.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <string>
#include <vector>

namespace kazu {

class RHI;

// ---------------------------------------------------------------------------
// PanelDesc: Technique describes its panel content via structured data.
// Zero ImGui dependency — can be defined in technique headers.
// ---------------------------------------------------------------------------
struct PanelItem {
    enum Type { Enum, Bool, Float, Int, Separator };
    Type type;
    std::string label;
    union {
        struct { int* value; const char** names; int count; } e;
        struct { bool* value; } b;
        struct { float* value; float min; float max; } f;
        struct { int* value; int min; int max; } i;
    };
};

struct PanelDesc {
    std::string name;
    std::vector<PanelItem> items;
};

// ---------------------------------------------------------------------------
// AppUI: owns ImGui Vulkan backend and renders panels.
// ---------------------------------------------------------------------------
class AppUI {
public:
    ~AppUI();
    void init(RHI* rhi, GLFWwindow* window);
    void shutdown();
    void onResize();

    void beginFrame();
    void endFrame(VkCommandBuffer cmd, uint32_t imageIndex);

    void drawPanel(const PanelDesc& desc);

    // Input capture queries: use these to avoid forwarding mouse events
    // to the camera controller while interacting with ImGui widgets.
    static bool wantsMouseInput();
    static bool wantsKeyboardInput();

private:
    void createRenderPass();
    void createFramebuffers();
    void createDescriptorPool();

    RHI* m_rhi = nullptr;
    GLFWwindow* m_window = nullptr;

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkRenderPass     m_renderPass     = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;


};

} // namespace kazu
