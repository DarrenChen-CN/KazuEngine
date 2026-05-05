// ============================================================================
// KazuEngine - Core Layer: Context
//
// Encapsulates the Vulkan entry-point objects:
//   - VkInstance + VkDebugUtilsMessengerEXT
//   - VkPhysicalDevice (GPU selection)
//   - VkDevice (logical device)
//   - VkQueue (graphics + present)
//
// RAII: constructor initializes all, destructor cleans up all.
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <optional>

namespace kazu {

class Context {
public:
    Context(const std::string& appName, bool enableValidation);
    ~Context();

    // Non-copyable
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Movable
    Context(Context&& other) noexcept;
    Context& operator=(Context&& other) noexcept;

    // Raw handle access (minimal encapsulation)
    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }
    uint32_t graphicsFamily() const { return m_graphicsFamily; }
    uint32_t presentFamily() const { return m_presentFamily; }
    bool validationEnabled() const { return m_validationEnabled; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsFamily = 0;
    uint32_t m_presentFamily = 0;
    bool m_validationEnabled = false;

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };

    void createInstance(const std::string& appName, bool enableValidation);
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkValidationLayerSupport();
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

} // namespace kazu
