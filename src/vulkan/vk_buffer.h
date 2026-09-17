#pragma once

#include "vk_device.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace vkwind {

class Buffer {
public:
  Buffer() = default;
  Buffer(VkDevice* device, VkDeviceSize size, VkBufferUsageFlags usage,
         VkMemoryPropertyFlags properties);
  ~Buffer();

  Buffer(Buffer&& other) noexcept;
  Buffer& operator=(Buffer&& other) noexcept;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  VkBuffer handle() const { return m_buffer; }
  VkDeviceSize size() const { return m_size; }
  bool valid() const { return m_buffer != VK_NULL_HANDLE; }

  void* map();
  void unmap();
  void upload(const void* data, VkDeviceSize dataSize, VkDeviceSize offset = 0);

private:
  void destroy();

  VkDevice* m_device = nullptr;
  VkBuffer m_buffer = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkDeviceSize m_size = 0;
  void* m_mapped = nullptr;
};

} // namespace vkwind
