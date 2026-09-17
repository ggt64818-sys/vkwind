#pragma once

#include "vk_device.h"
#include "vk_cmd_buffer.h"
#include <vulkan/vulkan.h>
#include <cstdint>

namespace vkwind {

class Image {
public:
  Image() = default;
  Image(VkDevice* device, uint32_t width, uint32_t height, VkFormat format,
        VkImageUsageFlags usage, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
  ~Image();

  Image(Image&& other) noexcept;
  Image& operator=(Image&& other) noexcept;
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  VkImage handle() const { return m_image; }
  VkImageView view() const { return m_view; }
  VkFormat format() const { return m_format; }
  uint32_t width() const { return m_width; }
  uint32_t height() const { return m_height; }
  bool valid() const { return m_image != VK_NULL_HANDLE; }

  // Transition image layout via command buffer
  void transition_layout(VkCommandBuffer cmd, VkImageLayout newLayout);

private:
  void destroy();

  VkDevice* m_device = nullptr;
  VkImage m_image = VK_NULL_HANDLE;
  VkImageView m_view = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace vkwind
