#include "vk_image.h"
#include "../util/util_log.h"

namespace vkwind {

static const char* kTag = "Image";

Image::Image(VkDevice* device, uint32_t width, uint32_t height, VkFormat format,
             VkImageUsageFlags usage, VkSampleCountFlagBits samples)
    : m_device(device), m_format(format), m_width(width), m_height(height) {

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = samples;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateImage(m_device->raw(), &imageInfo, nullptr, &m_image);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "vkCreateImage failed: %d", result);
    return;
  }

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(m_device->raw(), m_image, &memReqs);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
      memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  result = vkAllocateMemory(m_device->raw(), &allocInfo, nullptr, &m_memory);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "vkAllocateMemory failed: %d", result);
    vkDestroyImage(m_device->raw(), m_image, nullptr);
    m_image = VK_NULL_HANDLE;
    return;
  }

  vkBindImageMemory(m_device->raw(), m_image, m_memory, 0);

  // Create image view
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.layerCount = 1;

  // Use depth aspect for depth formats
  if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D24_UNORM_S8_UINT ||
      format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM_S8_UINT) {
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT) {
      viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
  }

  result = vkCreateImageView(m_device->raw(), &viewInfo, nullptr, &m_view);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "vkCreateImageView failed: %d", result);
  }

  VKWIND_DBG(kTag, "Created image %ux%u fmt=%d", width, height, format);
}

Image::~Image() { destroy(); }

Image::Image(Image&& other) noexcept
    : m_device(other.m_device), m_image(other.m_image), m_view(other.m_view),
      m_memory(other.m_memory), m_format(other.m_format),
      m_width(other.m_width), m_height(other.m_height),
      m_currentLayout(other.m_currentLayout) {
  other.m_image = VK_NULL_HANDLE;
  other.m_view = VK_NULL_HANDLE;
  other.m_memory = VK_NULL_HANDLE;
}

Image& Image::operator=(Image&& other) noexcept {
  if (this != &other) {
    destroy();
    m_device = other.m_device;
    m_image = other.m_image;
    m_view = other.m_view;
    m_memory = other.m_memory;
    m_format = other.m_format;
    m_width = other.m_width;
    m_height = other.m_height;
    m_currentLayout = other.m_currentLayout;
    other.m_image = VK_NULL_HANDLE;
    other.m_view = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
  }
  return *this;
}

void Image::transition_layout(VkCommandBuffer cmd, VkImageLayout newLayout) {
  if (m_currentLayout == newLayout) return;

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = m_currentLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;

  // Depth format check
  if (m_format == VK_FORMAT_D16_UNORM || m_format == VK_FORMAT_D24_UNORM_S8_UINT ||
      m_format == VK_FORMAT_D32_SFLOAT) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;

  // Determine access masks based on layouts
  if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
    barrier.srcAccessMask = 0;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  } else if (m_currentLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else {
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }

  if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else {
    barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }

  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                        0, nullptr, 0, nullptr, 1, &barrier);

  m_currentLayout = newLayout;
}

void Image::destroy() {
  if (m_view && m_device) {
    vkDestroyImageView(m_device->raw(), m_view, nullptr);
  }
  if (m_image && m_device) {
    vkDestroyImage(m_device->raw(), m_image, nullptr);
  }
  if (m_memory && m_device) {
    vkFreeMemory(m_device->raw(), m_memory, nullptr);
  }
  m_image = VK_NULL_HANDLE;
  m_view = VK_NULL_HANDLE;
  m_memory = VK_NULL_HANDLE;
}

} // namespace vkwind
