#include "vk_instance.h"
#include "../util/util_log.h"
#include "../util/util_config.h"

#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

namespace vkwind {

static const char* kTag = "VkInstance";

// Required instance extensions
static const std::vector<const char*> kInstanceExtensions = {
  VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
  VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
};

// Core device extensions (always required)
static const std::vector<const char*> kCoreDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// Optional device extensions (enabled if available)
static const std::vector<const char*> kOptionalDeviceExtensions = {
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
};

static const std::vector<const char*> kValidationLayers = {
  "VK_LAYER_KHRONOS_validation",
};

VkInstance::VkInstance() {
  create_instance();
  enumerate_physical_devices();
}

VkInstance::~VkInstance() {
  if (m_debugMessenger) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
      vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func) func(m_instance, m_debugMessenger, nullptr);
  }
  if (m_instance) {
    vkDestroyInstance(m_instance, nullptr);
  }
}

void VkInstance::create_instance() {
  auto& config = Config::instance();

  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "vkwind";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "vkwind";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;  // Safe minimum — Mali-G71 supports 1.1

  // Gather extensions
  std::vector<const char*> extensions = kInstanceExtensions;
  if (config.enable_validation) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  // Validation layers
  if (config.enable_validation) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
    createInfo.ppEnabledLayerNames = kValidationLayers.data();
  }

  VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create Vulkan instance: %d", result);
    return;
  }
  VKWIND_INFO(kTag, "Vulkan instance created (API %d.%d.%d)",
    VK_VERSION_MAJOR(appInfo.apiVersion),
    VK_VERSION_MINOR(appInfo.apiVersion),
    VK_VERSION_PATCH(appInfo.apiVersion));

  // Setup debug messenger
  if (config.enable_validation) {
    VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType =
      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debug_callback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
      vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func) {
      func(m_instance, &debugInfo, nullptr, &m_debugMessenger);
      VKWIND_INFO(kTag, "Debug messenger installed");
    }
  }
}

void VkInstance::enumerate_physical_devices() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
  if (count == 0) {
    VKWIND_ERR(kTag, "No Vulkan-capable GPUs found");
    return;
  }

  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

  m_gpus.reserve(count);
  for (auto& dev : devices) {
    GpuInfo info = {};
    info.device = dev;
    vkGetPhysicalDeviceProperties(dev, &info.properties);
    vkGetPhysicalDeviceFeatures(dev, &info.features);
    vkGetPhysicalDeviceMemoryProperties(dev, &info.memory);
    info.apiVersion = info.properties.apiVersion;

    // Query optional device extensions
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
    for (auto& ext : exts) {
      if (strcmp(ext.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
        info.hasDynamicRendering = true;
      if (strcmp(ext.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0)
        info.hasSynchronization2 = true;
      if (strcmp(ext.extensionName, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) == 0)
        info.hasSubgroupSizeControl = true;
    }

    // Queue families
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, nullptr);
    info.queueFamilies.resize(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, info.queueFamilies.data());

    for (uint32_t i = 0; i < qfCount; i++) {
      auto& qf = info.queueFamilies[i];
      if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        info.graphicsQueueFamily = i;
        info.hasGraphicsQueue = true;
      }
      if (qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
        info.computeQueueFamily = i;
        info.hasComputeQueue = true;
      }
    }

    // Default present queue to graphics queue family (validated after surface creation)
    info.presentQueueFamily = info.graphicsQueueFamily;
    info.hasPresentQueue = info.hasGraphicsQueue;

    VKWIND_INFO(kTag, "GPU %d: %s (API %d.%d.%d, dynRendering=%d, sync2=%d, subgroupSC=%d)",
      (int)m_gpus.size(), info.properties.deviceName,
      VK_VERSION_MAJOR(info.apiVersion),
      VK_VERSION_MINOR(info.apiVersion),
      VK_VERSION_PATCH(info.apiVersion),
      info.hasDynamicRendering, info.hasSynchronization2, info.hasSubgroupSizeControl);

    m_gpus.push_back(info);
  }

  // Present queue family will be determined after surface creation
  // (some drivers crash when calling vkGetPhysicalDeviceSurfaceSupportKHR with VK_NULL_HANDLE)
}

const GpuInfo* VkInstance::select_gpu(int index) const {
  if (m_gpus.empty()) return nullptr;

  if (index >= 0 && index < static_cast<int>(m_gpus.size())) {
    return &m_gpus[index];
  }

  // Auto-select: prefer integrated GPU for UMA (Mali), fallback to discrete
  const GpuInfo* best = &m_gpus[0];
  for (auto& gpu : m_gpus) {
    if (gpu.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      best = &gpu;
      break;
    }
  }
  // If no integrated, try discrete
  if (best->properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
    for (auto& gpu : m_gpus) {
      if (gpu.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        best = &gpu;
        break;
      }
    }
  }
  return best;
}

VkResult VkInstance::create_surface(void* hwnd, VkSurfaceKHR* surface) {
#ifdef _WIN32
  VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceInfo.hwnd = static_cast<HWND>(hwnd);
  surfaceInfo.hinstance = GetModuleHandle(nullptr);

  return vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, surface);
#else
  return VK_ERROR_INCOMPATIBLE_DRIVER;
#endif
}

VKAPI_ATTR VkBool32 VKAPI_CALL VkInstance::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
  (void)type;
  (void)user_data;

  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    VKWIND_ERR("Validation", "%s", callback_data->pMessage);
  } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    VKWIND_WARN("Validation", "%s", callback_data->pMessage);
  } else {
    VKWIND_DBG("Validation", "%s", callback_data->pMessage);
  }

  return VK_FALSE;
}

} // namespace vkwind
