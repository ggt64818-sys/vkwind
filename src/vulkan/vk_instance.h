#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace vkwind {

struct GpuInfo {
  VkPhysicalDevice device;
  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceMemoryProperties memory;
  VkPhysicalDeviceFeatures features = {};
  std::vector<VkQueueFamilyProperties> queueFamilies;
  bool hasGraphicsQueue = false;
  bool hasComputeQueue = false;
  bool hasPresentQueue = false;
  uint32_t graphicsQueueFamily = 0;
  uint32_t computeQueueFamily = 0;
  uint32_t presentQueueFamily = 0;
  uint32_t apiVersion = VK_MAKE_VERSION(1, 0, 0);
  bool hasDynamicRendering = false;
  bool hasSynchronization2 = false;
  bool hasSubgroupSizeControl = false;
};

class VkInstance {
public:
  VkInstance();
  ~VkInstance();

  // Non-copyable
  VkInstance(const VkInstance&) = delete;
  VkInstance& operator=(const VkInstance&) = delete;

  ::VkInstance handle() const { return m_instance; }
  ::VkInstance raw() const { return m_instance; }

  const std::vector<GpuInfo>& enumerate_gpus() const { return m_gpus; }
  const GpuInfo* select_gpu(int index = -1) const;

  // Surface creation (platform-specific)
  VkResult create_surface(void* hwnd, VkSurfaceKHR* surface);

private:
  void create_instance();
  void enumerate_physical_devices();

  ::VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
  std::vector<GpuInfo> m_gpus;

  static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data);
};

} // namespace vkwind
