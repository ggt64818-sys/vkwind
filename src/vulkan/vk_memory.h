#pragma once

#include "vk_device.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace vkwind {

// Memory heap types used by D3D9
enum class MemoryType {
  Default,    // GPU local
  Managed,    // CPU accessible, cached
  SystemMem,  // System memory
  Scratch,    // Temporary staging
};

struct MemoryAllocation {
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize offset = 0;
  VkDeviceSize size = 0;
  void* mapped = nullptr;
  MemoryType type = MemoryType::Default;
};

class VkMemory {
public:
  VkMemory(VkDevice* device);
  ~VkMemory();

  // Allocate a block of memory
  MemoryAllocation allocate(VkDeviceSize size, VkDeviceSize alignment,
                             uint32_t memoryTypeBits, MemoryType type);

  // Free a block
  void free(MemoryAllocation& alloc);

  // Map/unmap for CPU access
  bool map(MemoryAllocation& alloc);
  void unmap(MemoryAllocation& alloc);

  // Upload data to GPU
  void upload(MemoryAllocation& alloc, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

private:
  VkDevice* m_device = nullptr;
};

// Ring buffer for frame-synchronized uploads
class StagingBuffer {
public:
  StagingBuffer(VkDevice* device, VkDeviceSize size);
  ~StagingBuffer();

  // Allocate space in the ring buffer
  VkDeviceSize alloc(VkDeviceSize size, VkDeviceSize alignment = 256);

  // Get mapped pointer
  void* mapped_ptr() const { return m_mapped; }
  VkBuffer buffer() const { return m_buffer; }

  // Reset for new frame
  void reset();

private:
  VkDevice* m_device = nullptr;
  VkBuffer m_buffer = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  void* m_mapped = nullptr;
  VkDeviceSize m_size = 0;
  VkDeviceSize m_offset = 0;
};

} // namespace vkwind
