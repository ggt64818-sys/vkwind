#include "vk_buffer.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "Buffer";

Buffer::Buffer(VkDevice* device, VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties)
    : m_device(device), m_size(size) {
  VkBufferCreateInfo bufInfo = {};
  bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufInfo.size = size;
  bufInfo.usage = usage;
  bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateBuffer(m_device->raw(), &bufInfo, nullptr, &m_buffer);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "vkCreateBuffer failed: %d", result);
    return;
  }

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(m_device->raw(), m_buffer, &memReqs);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(memReqs.memoryTypeBits, properties);

  result = vkAllocateMemory(m_device->raw(), &allocInfo, nullptr, &m_memory);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "vkAllocateMemory failed: %d", result);
    vkDestroyBuffer(m_device->raw(), m_buffer, nullptr);
    m_buffer = VK_NULL_HANDLE;
    return;
  }

  vkBindBufferMemory(m_device->raw(), m_buffer, m_memory, 0);

  // Map if host visible
  if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    vkMapMemory(m_device->raw(), m_memory, 0, size, 0, &m_mapped);
  }

  VKWIND_DBG(kTag, "Created buffer: %u bytes", static_cast<uint32_t>(size));
}

Buffer::~Buffer() { destroy(); }

Buffer::Buffer(Buffer&& other) noexcept
    : m_device(other.m_device), m_buffer(other.m_buffer), m_memory(other.m_memory),
      m_size(other.m_size), m_mapped(other.m_mapped) {
  other.m_buffer = VK_NULL_HANDLE;
  other.m_memory = VK_NULL_HANDLE;
  other.m_size = 0;
  other.m_mapped = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
  if (this != &other) {
    destroy();
    m_device = other.m_device;
    m_buffer = other.m_buffer;
    m_memory = other.m_memory;
    m_size = other.m_size;
    m_mapped = other.m_mapped;
    other.m_buffer = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_size = 0;
    other.m_mapped = nullptr;
  }
  return *this;
}

void* Buffer::map() {
  if (m_mapped) return m_mapped;
  if (!m_buffer || !m_device) return nullptr;
  VkResult result = vkMapMemory(m_device->raw(), m_memory, 0, m_size, 0, &m_mapped);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "vkMapMemory failed: %d", result);
    return nullptr;
  }
  return m_mapped;
}

void Buffer::unmap() {
  if (m_mapped && m_device) {
    vkUnmapMemory(m_device->raw(), m_memory);
    m_mapped = nullptr;
  }
}

void Buffer::upload(const void* data, VkDeviceSize dataSize, VkDeviceSize offset) {
  void* ptr = map();
  if (!ptr) return;
  std::memcpy(static_cast<uint8_t*>(ptr) + offset, data, dataSize);
  unmap();
}

void Buffer::destroy() {
  if (m_buffer && m_device) {
    vkDestroyBuffer(m_device->raw(), m_buffer, nullptr);
  }
  if (m_memory && m_device) {
    vkFreeMemory(m_device->raw(), m_memory, nullptr);
  }
  m_buffer = VK_NULL_HANDLE;
  m_memory = VK_NULL_HANDLE;
  m_mapped = nullptr;
  m_size = 0;
}

} // namespace vkwind
