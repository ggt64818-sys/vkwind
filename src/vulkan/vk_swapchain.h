#pragma once

#include "vk_device.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <functional>

namespace vkwind {

struct SwapchainConfig {
  uint32_t width = 800;
  uint32_t height = 600;
  VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
  VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  uint32_t imageCount = 2;
  bool vsync = true;
  VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
};

class VkSwapchain {
public:
  VkSwapchain(VkDevice* device, VkSurfaceKHR surface, const SwapchainConfig& config);
  ~VkSwapchain();

  VkSwapchain(const VkSwapchain&) = delete;
  VkSwapchain& operator=(const VkSwapchain&) = delete;

  void resize(uint32_t width, uint32_t height);
  VkResult acquire_next_image(VkSemaphore signalSemaphore, uint32_t* imageIndex);
  VkResult present(VkSemaphore waitSemaphore, uint32_t imageIndex);

  VkSwapchainKHR handle() const { return m_swapchain; }
  uint32_t image_count() const { return static_cast<uint32_t>(m_images.size()); }
  VkFormat image_format() const { return m_config.format; }
  VkFormat depth_format() const { return m_config.depthFormat; }
  uint32_t width() const { return m_config.width; }
  uint32_t height() const { return m_config.height; }

  VkImage get_image(uint32_t index) const { return m_images[index]; }
  VkImageView get_image_view(uint32_t index) const { return m_imageViews[index]; }

  VkRenderPass render_pass() const { return m_renderPass; }
  VkFramebuffer framebuffer(uint32_t index) const { return m_framebuffers[index]; }
  VkImageView depth_view() const { return m_depthView; }

private:
  void create_swapchain();
  void create_image_views();
  void create_render_pass();
  void create_depth_resources();
  void create_framebuffers();
  void cleanup();

  VkDevice* m_device = nullptr;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  SwapchainConfig m_config;

  std::vector<VkImage> m_images;
  std::vector<VkImageView> m_imageViews;

  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  VkImage m_depthImage = VK_NULL_HANDLE;
  VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
  VkImageView m_depthView = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> m_framebuffers;
};

} // namespace vkwind
