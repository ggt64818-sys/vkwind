#include "vk_swapchain.h"
#include "../util/util_log.h"

#include <algorithm>
#include <limits>

namespace vkwind {

static const char* kTag = "VkSwapchain";

static bool find_supported_depth_format(VkPhysicalDevice physicalDevice, VkFormat* outFormat) {
  VkFormat candidates[] = {
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM,
  };
  for (auto fmt : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      *outFormat = fmt;
      return true;
    }
  }
  return false;
}

VkSwapchain::VkSwapchain(VkDevice* device, VkSurfaceKHR surface, const SwapchainConfig& config)
  : m_device(device), m_surface(surface), m_config(config) {
  create_swapchain();
  create_image_views();
  create_render_pass();
  create_depth_resources();
  create_framebuffers();
}

VkSwapchain::~VkSwapchain() {
  cleanup();
}

void VkSwapchain::cleanup() {
  auto dev = m_device->raw();
  vkDeviceWaitIdle(dev);

  for (auto fb : m_framebuffers) {
    if (fb) vkDestroyFramebuffer(dev, fb, nullptr);
  }
  m_framebuffers.clear();

  if (m_depthView) vkDestroyImageView(dev, m_depthView, nullptr);
  if (m_depthImage) vkDestroyImage(dev, m_depthImage, nullptr);
  if (m_depthMemory) vkFreeMemory(dev, m_depthMemory, nullptr);
  m_depthView = VK_NULL_HANDLE;
  m_depthImage = VK_NULL_HANDLE;
  m_depthMemory = VK_NULL_HANDLE;

  if (m_renderPass) vkDestroyRenderPass(dev, m_renderPass, nullptr);
  m_renderPass = VK_NULL_HANDLE;

  for (auto view : m_imageViews) {
    if (view) vkDestroyImageView(dev, view, nullptr);
  }
  m_imageViews.clear();
  m_images.clear();

  if (m_swapchain) {
    vkDestroySwapchainKHR(dev, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
  }
}

void VkSwapchain::create_swapchain() {
  auto dev = m_device->raw();
  auto phys = m_device->physical();

  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, m_surface, &caps);

  uint32_t minImages = caps.minImageCount;
  uint32_t maxImages = caps.maxImageCount;
  if (maxImages == 0) maxImages = UINT32_MAX;
  m_config.imageCount = std::clamp(m_config.imageCount, minImages, maxImages);

  if (caps.currentExtent.width != UINT32_MAX) {
    m_config.width = caps.currentExtent.width;
    m_config.height = caps.currentExtent.height;
  } else {
    m_config.width = std::clamp(m_config.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    m_config.height = std::clamp(m_config.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }

  uint32_t modeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys, m_surface, &modeCount, nullptr);
  std::vector<VkPresentModeKHR> modes(modeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys, m_surface, &modeCount, modes.data());

  if (!m_config.vsync) {
    for (auto mode : modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) { m_config.presentMode = mode; break; }
    }
    bool hasImmediate = false;
    for (auto mode : modes) {
      if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) { hasImmediate = true; break; }
    }
    if (m_config.presentMode == VK_PRESENT_MODE_FIFO_KHR && hasImmediate) {
      m_config.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
  } else {
    m_config.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  }

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys, m_surface, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys, m_surface, &formatCount, formats.data());

  bool formatFound = false;
  for (auto& f : formats) {
    if (f.format == m_config.format && f.colorSpace == m_config.colorSpace) { formatFound = true; break; }
  }
  if (!formatFound && !formats.empty()) {
    // Prefer B8G8R8A8_UNORM to match D3D9's A8R8G8B8 (BGRA in memory), then R8G8B8A8, then first available
    bool found888 = false;
    for (auto& f : formats) {
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        m_config.format = f.format;
        m_config.colorSpace = f.colorSpace;
        found888 = true;
        break;
      }
    }
    if (!found888) {
      for (auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
          m_config.format = f.format;
          m_config.colorSpace = f.colorSpace;
          found888 = true;
          break;
        }
      }
    }
    if (!found888) {
      m_config.format = formats[0].format;
      m_config.colorSpace = formats[0].colorSpace;
    }
    VKWIND_WARN(kTag, "Requested format not available, using %d", m_config.format);
  }

  // Choose depth format
  if (!find_supported_depth_format(phys, &m_config.depthFormat)) {
    m_config.depthFormat = VK_FORMAT_UNDEFINED;
    VKWIND_WARN(kTag, "No supported depth format found");
  }

  VkSwapchainCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_surface;
  createInfo.minImageCount = m_config.imageCount;
  createInfo.imageFormat = m_config.format;
  createInfo.imageColorSpace = m_config.colorSpace;
  createInfo.imageExtent = {m_config.width, m_config.height};
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t queueFamilyIndices[] = {
    m_device->graphics_queue_family(),
    m_device->present_queue_family()
  };

  if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = caps.currentTransform;

  // Try supported compositeAlpha modes in order of preference
  VkCompositeAlphaFlagBitsKHR alphaModes[] = {
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  for (auto mode : alphaModes) {
    if (caps.supportedCompositeAlpha & mode) {
      createInfo.compositeAlpha = mode;
      break;
    }
  }
  createInfo.presentMode = m_config.presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  VkResult result = vkCreateSwapchainKHR(dev, &createInfo, nullptr, &m_swapchain);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create swapchain: %d", result);
    return;
  }

  VKWIND_INFO(kTag, "Swapchain created: %ux%u, fmt=%d, mode=%d, images=%u",
    m_config.width, m_config.height, m_config.format, m_config.presentMode, m_config.imageCount);

  vkGetSwapchainImagesKHR(dev, m_swapchain, &m_config.imageCount, nullptr);
  m_images.resize(m_config.imageCount);
  vkGetSwapchainImagesKHR(dev, m_swapchain, &m_config.imageCount, m_images.data());
}

void VkSwapchain::create_image_views() {
  auto dev = m_device->raw();
  m_imageViews.resize(m_images.size());

  for (size_t i = 0; i < m_images.size(); i++) {
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_images[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_config.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(dev, &viewInfo, nullptr, &m_imageViews[i]);
    if (result != VK_SUCCESS) {
      VKWIND_ERR(kTag, "Failed to create image view %zu: %d", i, result);
    }
  }
}

void VkSwapchain::create_render_pass() {
  auto dev = m_device->raw();

  // Color attachment
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = m_config.format;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  // Mali TBDR: STORE needed for presentation
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef = {};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  // Depth attachment
  VkAttachmentDescription depthAttachment = {};
  depthAttachment.format = m_config.depthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  // Mali TBDR: keep STORE for depth (D3D9 games may read depth for effects)
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthRef = {};
  depthRef.attachment = 1;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  if (m_config.depthFormat != VK_FORMAT_UNDEFINED) {
    subpass.pDepthStencilAttachment = &depthRef;
  }

  // Mali TBDR optimized subpass dependencies
  // Dep 1: External -> subpass 0 (image acquire -> first tile writes)
  //   Includes fragment shader stage for texture sampling during rasterization
  VkSubpassDependency dependencies[2] = {};

  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].srcAccessMask = 0;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  // Dep 2: subpass 0 -> External (tile writes -> presentation)
  //   Wait for all tile operations to complete before presenting
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  std::vector<VkAttachmentDescription> attachments = {colorAttachment};

  VkRenderPassCreateInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

  if (m_config.depthFormat != VK_FORMAT_UNDEFINED) {
    attachments.push_back(depthAttachment);
  }

  rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  rpInfo.pAttachments = attachments.data();
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 2;
  rpInfo.pDependencies = dependencies;

  VkResult result = vkCreateRenderPass(dev, &rpInfo, nullptr, &m_renderPass);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create render pass: %d", result);
  }

  VKWIND_DBG(kTag, "Swapchain render pass created: format=%d, depth=%d (Mali TBDR optimized)",
    m_config.format, m_config.depthFormat);
}

void VkSwapchain::create_depth_resources() {
  if (m_config.depthFormat == VK_FORMAT_UNDEFINED) return;

  auto dev = m_device->raw();
  auto phys = m_device->physical();

  // Create depth image
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {m_config.width, m_config.height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_config.depthFormat;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  vkCreateImage(dev, &imageInfo, nullptr, &m_depthImage);

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(dev, m_depthImage, &memReqs);

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;

  auto memProps = m_device->memory_properties();
  allocInfo.memoryTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((memReqs.memoryTypeBits & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      allocInfo.memoryTypeIndex = i;
      break;
    }
  }

  // Fallback: HOST_VISIBLE | DEVICE_LOCAL for UMA (Mali)
  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
      if ((memReqs.memoryTypeBits & (1 << i)) &&
          (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        allocInfo.memoryTypeIndex = i;
        break;
      }
    }
  }

  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    VKWIND_ERR(kTag, "No suitable memory for depth buffer");
    return;
  }

  vkAllocateMemory(dev, &allocInfo, nullptr, &m_depthMemory);
  vkBindImageMemory(dev, m_depthImage, m_depthMemory, 0);

  // Create depth image view
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_depthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = m_config.depthFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  vkCreateImageView(dev, &viewInfo, nullptr, &m_depthView);
}

void VkSwapchain::create_framebuffers() {
  auto dev = m_device->raw();
  m_framebuffers.resize(m_images.size());

  for (size_t i = 0; i < m_images.size(); i++) {
    std::vector<VkImageView> attachments = {m_imageViews[i]};
    if (m_depthView) {
      attachments.push_back(m_depthView);
    }

    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_renderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = m_config.width;
    fbInfo.height = m_config.height;
    fbInfo.layers = 1;

    VkResult result = vkCreateFramebuffer(dev, &fbInfo, nullptr, &m_framebuffers[i]);
    if (result != VK_SUCCESS) {
      VKWIND_ERR(kTag, "Failed to create framebuffer %zu: %d", i, result);
    }
  }
}

void VkSwapchain::resize(uint32_t width, uint32_t height) {
  if (width == m_config.width && height == m_config.height) return;

  VKWIND_INFO(kTag, "Resizing swapchain: %ux%u -> %ux%u",
    m_config.width, m_config.height, width, height);

  vkDeviceWaitIdle(m_device->raw());
  cleanup();
  m_config.width = width;
  m_config.height = height;
  create_swapchain();
  create_image_views();
  create_render_pass();
  create_depth_resources();
  create_framebuffers();
}

VkResult VkSwapchain::acquire_next_image(VkSemaphore signalSemaphore, uint32_t* imageIndex) {
  return vkAcquireNextImageKHR(
    m_device->raw(), m_swapchain, UINT64_MAX,
    signalSemaphore, VK_NULL_HANDLE, imageIndex);
}

VkResult VkSwapchain::present(VkSemaphore waitSemaphore, uint32_t imageIndex) {
  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &waitSemaphore;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &m_swapchain;
  presentInfo.pImageIndices = &imageIndex;

  return vkQueuePresentKHR(m_device->present_queue(), &presentInfo);
}

} // namespace vkwind
