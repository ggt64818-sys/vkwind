#include "vk_device.h"
#include "../util/util_log.h"

#include <cstring>
#include <set>
#include <algorithm>

namespace vkwind {

static const char* kTag = "VkDevice";

static const std::vector<const char*> kCoreDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

VkDevice::VkDevice(VkInstance* instance, const GpuInfo* gpu)
  : m_instance(instance), m_gpu(gpu) {
  create_logical_device();
  get_queues();
}

VkDevice::~VkDevice() {
  if (m_device) {
    vkDeviceWaitIdle(m_device);
    vkDestroyDevice(m_device, nullptr);
  }
}

void VkDevice::create_logical_device() {
  // Collect unique queue family indices
  std::set<uint32_t> uniqueQueueFamilies = {
    m_gpu->graphicsQueueFamily,
    m_gpu->computeQueueFamily,
    m_gpu->presentQueueFamily,
  };

  std::vector<VkDeviceQueueCreateInfo> queueInfos;
  float queuePriority = 1.0f;

  for (uint32_t family : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;
    queueInfos.push_back(queueInfo);
  }

  // --- Query features: only enable what hardware actually supports ---
  VkPhysicalDeviceFeatures available = m_gpu->features;

  VkPhysicalDeviceFeatures enabledFeatures = {};
  if (available.samplerAnisotropy)              enabledFeatures.samplerAnisotropy = VK_TRUE;
  if (available.fillModeNonSolid)               enabledFeatures.fillModeNonSolid = VK_TRUE;
  if (available.wideLines)                      enabledFeatures.wideLines = VK_TRUE;
  if (available.largePoints)                    enabledFeatures.largePoints = VK_TRUE;
  if (available.shaderSampledImageArrayDynamicIndexing)
    enabledFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
  if (available.multiDrawIndirect)              enabledFeatures.multiDrawIndirect = VK_TRUE;
  if (available.drawIndirectFirstInstance)      enabledFeatures.drawIndirectFirstInstance = VK_TRUE;

  // --- Build pNext chain for optional extensions ---
  VkPhysicalDeviceSynchronization2Features sync2 = {};
  sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;

  VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {};
  dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

  // Mali G57 Valhall: subgroup size control (subgroupSize=16, maxSubgroupSize=16)
  VkPhysicalDeviceSubgroupSizeControlFeatures subgroupSizeControl = {};
  subgroupSizeControl.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;

  VkPhysicalDeviceFeatures2 features2 = {};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.features = enabledFeatures;

  // Chain optional extensions only if GPU supports them
  void** tail = &features2.pNext;
  if (m_gpu->hasSynchronization2) {
    sync2.synchronization2 = VK_TRUE;
    *tail = &sync2;
    tail = &sync2.pNext;
    m_hasSync2 = true;
  }
  if (m_gpu->hasDynamicRendering) {
    dynamicRendering.dynamicRendering = VK_TRUE;
    *tail = &dynamicRendering;
    tail = &dynamicRendering.pNext;
    m_hasDynamicRendering = true;
  }
  if (m_gpu->hasSubgroupSizeControl) {
    subgroupSizeControl.subgroupSizeControl = VK_TRUE;
    subgroupSizeControl.computeFullSubgroups = VK_TRUE;
    *tail = &subgroupSizeControl;
    tail = &subgroupSizeControl.pNext;
    m_hasSubgroupSizeControl = true;
  }

  // --- Build extension list: core + available optional ---
  std::vector<const char*> extensions = kCoreDeviceExtensions;
  if (m_gpu->hasSynchronization2)  extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
  if (m_gpu->hasDynamicRendering)  extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
  if (m_gpu->hasSubgroupSizeControl) extensions.push_back(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);

  VkDeviceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
  createInfo.pQueueCreateInfos = queueInfos.data();
  createInfo.pNext = &features2;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkResult result = vkCreateDevice(m_gpu->device, &createInfo, nullptr, &m_device);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create logical device: %d", result);
    return;
  }

  VKWIND_INFO(kTag, "Logical device created for %s (dynamicRendering=%d, sync2=%d, subgroupSC=%d)",
    m_gpu->properties.deviceName, m_hasDynamicRendering, m_hasSync2, m_hasSubgroupSizeControl);
}

void VkDevice::get_queues() {
  vkGetDeviceQueue(m_device, m_gpu->graphicsQueueFamily, 0, &m_graphicsQueue);
  vkGetDeviceQueue(m_device, m_gpu->computeQueueFamily, 0, &m_computeQueue);
  vkGetDeviceQueue(m_device, m_gpu->presentQueueFamily, 0, &m_presentQueue);

  VKWIND_DBG(kTag, "Queues acquired: graphics=%p, compute=%p, present=%p",
    (void*)m_graphicsQueue, (void*)m_computeQueue, (void*)m_presentQueue);
}

VkCommandPool VkDevice::create_command_pool(uint32_t queueFamilyIndex) {
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndex;

  VkCommandPool pool;
  VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &pool);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create command pool: %d", result);
    return VK_NULL_HANDLE;
  }
  return pool;
}

void VkDevice::destroy_command_pool(VkCommandPool pool) {
  if (pool) vkDestroyCommandPool(m_device, pool, nullptr);
}

uint32_t VkDevice::find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
  auto& memProps = m_gpu->memory;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  VKWIND_ERR(kTag, "Failed to find suitable memory type (filter=0x%x, props=0x%x)",
    typeFilter, properties);
  return UINT32_MAX;
}

bool VkDevice::allocate_memory(VkMemoryRequirements& reqs, VkMemoryPropertyFlags flags,
                                VkDeviceMemory* memory, VkDeviceSize* offset) {
  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = reqs.size;
  allocInfo.memoryTypeIndex = find_memory_type(reqs.memoryTypeBits, flags);

  if (allocInfo.memoryTypeIndex == UINT32_MAX) return false;

  VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, memory);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to allocate device memory: %d (size=%llu)",
      result, (unsigned long long)reqs.size);
    return false;
  }

  if (offset) *offset = 0;
  return true;
}

VkSemaphore VkDevice::create_semaphore() {
  VkSemaphoreCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkSemaphore sem;
  vkCreateSemaphore(m_device, &info, nullptr, &sem);
  return sem;
}

VkFence VkDevice::create_fence(bool signaled) {
  VkFenceCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (signaled) info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkFence fence;
  vkCreateFence(m_device, &info, nullptr, &fence);
  return fence;
}

void VkDevice::destroy_semaphore(VkSemaphore sem) {
  if (sem) vkDestroySemaphore(m_device, sem, nullptr);
}

void VkDevice::destroy_fence(VkFence fence) {
  if (fence) vkDestroyFence(m_device, fence, nullptr);
}

void VkDevice::wait_idle() {
  vkDeviceWaitIdle(m_device);
}

} // namespace vkwind
