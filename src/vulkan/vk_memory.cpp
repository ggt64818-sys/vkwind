#include "vk_memory.h"
#include "../util/util_log.h"

namespace vkwind {

static const char* kTag = "VkMemory";

VkMemory::VkMemory(VkDevice* device) : m_device(device) {}

VkMemory::~VkMemory() {
  // All allocations should be freed before device destruction
}

MemoryAllocation VkMemory::allocate(VkDeviceSize size, VkDeviceSize alignment,
                                     uint32_t memoryTypeBits, MemoryType type) {
  MemoryAllocation alloc;
  alloc.size = size;
  alloc.type = type;

  // Choose memory properties based on type
  VkMemoryPropertyFlags flags;
  switch (type) {
    case MemoryType::Default:
      flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
    case MemoryType::Managed:
      // Mali-G71 usually lacks HOST_CACHED — fall back to HOST_COHERENT
      flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      break;
    case MemoryType::SystemMem:
      flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      break;
    case MemoryType::Scratch:
      flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      break;
  }

  // Find memory type
  auto memProps = m_device->memory_properties();
  uint32_t memTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((memoryTypeBits & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & flags) == flags) {
      memTypeIndex = i;
      break;
    }
  }

  // Fallback for Managed: drop HOST_CACHED requirement (Mali compatibility)
  if (memTypeIndex == UINT32_MAX && type == MemoryType::Managed) {
    VKWIND_WARN(kTag, "HOST_CACHED not available, falling back to HOST_COHERENT");
    flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
      if ((memoryTypeBits & (1 << i)) &&
          (memProps.memoryTypes[i].propertyFlags & flags) == flags) {
        memTypeIndex = i;
        break;
      }
    }
  }

  // Fallback for Default: try HOST_VISIBLE|DEVICE_LOCAL for UMA (Mali)
  if (memTypeIndex == UINT32_MAX && type == MemoryType::Default) {
    VKWIND_WARN(kTag, "DEVICE_LOCAL not available, falling back to HOST_VISIBLE|COHERENT");
    flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
      if ((memoryTypeBits & (1 << i)) &&
          (memProps.memoryTypes[i].propertyFlags & flags) == flags) {
        memTypeIndex = i;
        break;
      }
    }
  }

  if (memTypeIndex == UINT32_MAX) {
    VKWIND_ERR(kTag, "No suitable memory type found (bits=0x%x, flags=0x%x)",
      memoryTypeBits, flags);
    return alloc;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = size;
  allocInfo.memoryTypeIndex = memTypeIndex;

  VkResult result = vkAllocateMemory(m_device->raw(), &allocInfo, nullptr, &alloc.memory);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to allocate memory: %d (size=%llu)", result, (unsigned long long)size);
    alloc.memory = VK_NULL_HANDLE;
    return alloc;
  }

  VKWIND_DBG(kTag, "Allocated %llu bytes (type=%d)", (unsigned long long)size, (int)type);
  return alloc;
}

void VkMemory::free(MemoryAllocation& alloc) {
  if (alloc.mapped) {
    vkUnmapMemory(m_device->raw(), alloc.memory);
    alloc.mapped = nullptr;
  }
  if (alloc.memory) {
    vkFreeMemory(m_device->raw(), alloc.memory, nullptr);
    alloc.memory = VK_NULL_HANDLE;
  }
  alloc.offset = 0;
  alloc.size = 0;
}

bool VkMemory::map(MemoryAllocation& alloc) {
  if (alloc.mapped) return true;
  if (!alloc.memory) return false;

  VkResult result = vkMapMemory(m_device->raw(), alloc.memory, 0, alloc.size, 0, &alloc.mapped);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to map memory: %d", result);
    return false;
  }
  return true;
}

void VkMemory::unmap(MemoryAllocation& alloc) {
  if (alloc.mapped) {
    vkUnmapMemory(m_device->raw(), alloc.memory);
    alloc.mapped = nullptr;
  }
}

void VkMemory::upload(MemoryAllocation& alloc, const void* data, VkDeviceSize size, VkDeviceSize offset) {
  if (!alloc.mapped && !map(alloc)) return;

  char* dst = static_cast<char*>(alloc.mapped) + offset;
  memcpy(dst, data, static_cast<size_t>(size));
}

// StagingBuffer implementation

StagingBuffer::StagingBuffer(VkDevice* device, VkDeviceSize size)
  : m_device(device), m_size(size), m_offset(0) {
  VkBufferCreateInfo bufInfo = {};
  bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufInfo.size = size;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateBuffer(device->raw(), &bufInfo, nullptr, &m_buffer);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create staging buffer: %d", result);
    return;
  }

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device->raw(), m_buffer, &memReqs);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;

  // Find host visible memory
  auto memProps = device->memory_properties();
  allocInfo.memoryTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((memReqs.memoryTypeBits & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
      allocInfo.memoryTypeIndex = i;
      break;
    }
  }

  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    VKWIND_ERR(kTag, "No host-visible memory found for staging buffer");
    return;
  }

  result = vkAllocateMemory(device->raw(), &allocInfo, nullptr, &m_memory);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to allocate staging memory: %d", result);
    return;
  }

  vkBindBufferMemory(device->raw(), m_buffer, m_memory, 0);

  result = vkMapMemory(device->raw(), m_memory, 0, size, 0, &m_mapped);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to map staging memory: %d", result);
  }

  VKWIND_INFO(kTag, "Staging buffer created: %llu MB", (unsigned long long)(size / (1024 * 1024)));
}

StagingBuffer::~StagingBuffer() {
  if (m_mapped) vkUnmapMemory(m_device->raw(), m_memory);
  if (m_buffer) vkDestroyBuffer(m_device->raw(), m_buffer, nullptr);
  if (m_memory) vkFreeMemory(m_device->raw(), m_memory, nullptr);
}

VkDeviceSize StagingBuffer::alloc(VkDeviceSize size, VkDeviceSize alignment) {
  VkDeviceSize aligned = (m_offset + alignment - 1) & ~(alignment - 1);
  if (aligned + size > m_size) {
    VKWIND_WARN(kTag, "Staging buffer overflow, wrapping around");
    aligned = 0;
  }
  VkDeviceSize result = aligned;
  m_offset = aligned + size;
  return result;
}

void StagingBuffer::reset() {
  m_offset = 0;
}

} // namespace vkwind
