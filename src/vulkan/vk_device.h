#pragma once

#include "vk_instance.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <memory>

namespace vkwind {

class VkDevice {
public:
  VkDevice(VkInstance* instance, const GpuInfo* gpu);
  ~VkDevice();

  VkDevice(const VkDevice&) = delete;
  VkDevice& operator=(const VkDevice&) = delete;

  ::VkDevice handle() const { return m_device; }
  ::VkDevice raw() const { return m_device; }
  VkPhysicalDevice physical() const { return m_gpu->device; }
  const GpuInfo* gpu_info() const { return m_gpu; }

  // Queue access
  VkQueue graphics_queue() const { return m_graphicsQueue; }
  VkQueue compute_queue() const { return m_computeQueue; }
  VkQueue present_queue() const { return m_presentQueue; }
  uint32_t graphics_queue_family() const { return m_gpu->graphicsQueueFamily; }
  uint32_t compute_queue_family() const { return m_gpu->computeQueueFamily; }
  uint32_t present_queue_family() const { return m_gpu->presentQueueFamily; }

  // Command pool
  VkCommandPool create_command_pool(uint32_t queueFamilyIndex);
  void destroy_command_pool(VkCommandPool pool);

  // Memory allocation
  uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
  bool allocate_memory(VkMemoryRequirements& reqs, VkMemoryPropertyFlags flags,
                       VkDeviceMemory* memory, VkDeviceSize* offset = nullptr);

  // Synchronization
  VkSemaphore create_semaphore();
  VkFence create_fence(bool signaled = true);
  void destroy_semaphore(VkSemaphore sem);
  void destroy_fence(VkFence fence);

  // Device properties
  VkPhysicalDeviceProperties properties() const { return m_gpu->properties; }
  VkPhysicalDeviceFeatures features() const { return m_gpu->features; }
  VkPhysicalDeviceMemoryProperties memory_properties() const { return m_gpu->memory; }
  bool has_dynamic_rendering() const { return m_hasDynamicRendering; }
  bool has_sync2() const { return m_hasSync2; }
  bool has_subgroup_size_control() const { return m_hasSubgroupSizeControl; }

  // Wait for device to be idle
  void wait_idle();

private:
  void create_logical_device();
  void get_queues();

  VkInstance* m_instance = nullptr;
  const GpuInfo* m_gpu = nullptr;
  ::VkDevice m_device = VK_NULL_HANDLE;
  VkQueue m_graphicsQueue = VK_NULL_HANDLE;
  VkQueue m_computeQueue = VK_NULL_HANDLE;
  VkQueue m_presentQueue = VK_NULL_HANDLE;
  bool m_hasDynamicRendering = false;
  bool m_hasSync2 = false;
  bool m_hasSubgroupSizeControl = false;
};

} // namespace vkwind
