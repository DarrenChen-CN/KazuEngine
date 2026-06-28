// ============================================================================
// KazuEngine - App Layer: ImGui Integration (Implementation)
// ============================================================================

#include "app/AppUI.h"
#include "rhi/RHI.h"
#include "core/Context.h"
#include "core/Utils.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace kazu {

void AppUI::init(RHI* rhi, GLFWwindow* window) {
    m_rhi = rhi;
    m_window = window;

    createDescriptorPool();
    createRenderPass();
    createFramebuffers();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Scale UI for readability on standard DPI displays
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 1.5f;
    io.IniFilename = nullptr;  // Don't save window state to imgui.ini

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = m_rhi->ctx().instance();
    initInfo.PhysicalDevice = m_rhi->ctx().physicalDevice();
    initInfo.Device = m_rhi->ctx().device();
    initInfo.QueueFamily = m_rhi->ctx().graphicsFamily();
    initInfo.Queue = m_rhi->ctx().graphicsQueue();
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.PipelineInfoMain.RenderPass = m_renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = m_rhi->swapchainImageCount();
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);

    // Fonts are uploaded automatically by NewFrame() on first use
}

AppUI::~AppUI() {
    shutdown();
}

void AppUI::shutdown() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();
    if (m_renderPass) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    if (m_descriptorPool) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_rhi = nullptr;
}

void AppUI::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void AppUI::endFrame(VkCommandBuffer cmd, uint32_t imageIndex) {
    ImGui::Render();

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderPass;
    rpInfo.framebuffer = m_framebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_rhi->extent();
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
}

bool AppUI::wantsMouseInput() {
    return ImGui::GetIO().WantCaptureMouse;
}

bool AppUI::wantsKeyboardInput() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void AppUI::drawPanel(const PanelDesc& desc) {
    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_Always);
    ImGui::Begin(desc.name.c_str(), nullptr, ImGuiWindowFlags_NoResize);
    for (const auto& item : desc.items) {
        switch (item.type) {
            case PanelItem::Enum:
                ImGui::PushItemWidth(140);
                ImGui::Combo(item.label.c_str(), item.e.value, item.e.names, item.e.count);
                ImGui::PopItemWidth();
                break;
            case PanelItem::Bool:
                ImGui::Checkbox(item.label.c_str(), item.b.value);
                break;
            case PanelItem::Float:
                ImGui::SliderFloat(item.label.c_str(), item.f.value, item.f.min, item.f.max);
                break;
            case PanelItem::Int:
                ImGui::SliderInt(item.label.c_str(), item.i.value, item.i.min, item.i.max);
                break;
            case PanelItem::Separator:
                ImGui::Separator();
                break;
            case PanelItem::Label:
                ImGui::Text("%s", item.label.c_str());
                break;
        }
    }
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
}

// ---- Private ----

void AppUI::createDescriptorPool() {
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = 1000;
    info.poolSizeCount = sizeof(sizes) / sizeof(sizes[0]);
    info.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(m_rhi->ctx().device(), &info, nullptr, &m_descriptorPool));
}

void AppUI::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = m_rhi->swapchainFormat();
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(m_rhi->ctx().device(), &info, nullptr, &m_renderPass));
}

void AppUI::onResize() {
    if (!m_rhi) return;
    VkDevice device = m_rhi->ctx().device();
    vkDeviceWaitIdle(device);
    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();
    createFramebuffers();
}

void AppUI::createFramebuffers() {
    uint32_t count = m_rhi->swapchainImageCount();
    m_framebuffers.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageView view = m_rhi->swapchainImageView(i);
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = m_renderPass;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = m_rhi->extent().width;
        info.height = m_rhi->extent().height;
        info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(m_rhi->ctx().device(), &info, nullptr, &m_framebuffers[i]));
    }
}

} // namespace kazu
