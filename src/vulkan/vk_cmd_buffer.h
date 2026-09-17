#pragma once

#include "../vulkan/vk_device.h"
#include "../vulkan/vk_pipeline.h"
#include "../d3d9/d3d9_types.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <array>

namespace vkwind {

// Draw call submission
struct DrawCall {
  enum Type {
    Draw,
    DrawIndexed,
    DrawUP,
    DrawIndexedUP,
  };

  Type type;

  // Vertex data
  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VkDeviceSize vertexOffset = 0;
  uint32_t vertexStride = 0;
  uint32_t vertexCount = 0;
  uint32_t startVertex = 0;

  // Index data
  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VkDeviceSize indexOffset = 0;
  uint32_t indexCount = 0;
  uint32_t startIndex = 0;
  VkIndexType indexType = VK_INDEX_TYPE_UINT16;

  // Primitive type
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  // Inline data (for *UP calls)
  std::vector<uint8_t> vertexData;
  std::vector<uint8_t> indexData;
};

// Frame of in-flight commands
struct FrameContext {
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkSemaphore imageAvailable = VK_NULL_HANDLE;
  VkSemaphore renderFinished = VK_NULL_HANDLE;
  bool submitted = false;

  // Staging buffer for uploads
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  void* stagingMapped = nullptr;
  VkDeviceSize stagingOffset = 0;
  VkDeviceSize stagingSize = 0;

  // Track where this frame started writing in the persistent staging ring buffer
  VkDeviceSize persistentStagingStartOffset = 0;
};

class CommandBufferManager {
public:
  CommandBufferManager(VkDevice* device, uint32_t frameCount = 2);
  ~CommandBufferManager();

  // Frame management
  void begin_frame();
  void end_frame();
  void submit_frame(VkSemaphore waitSemaphore, VkSemaphore signalSemaphore);
  void wait_frame(uint32_t frameIndex);
  void advance_frame();
  uint32_t current_frame() const { return m_currentFrame; }

  // Command recording
  void begin_render_pass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                         uint32_t width, uint32_t height,
                         const float* clearColor = nullptr,
                         VkFormat depthFormat = VK_FORMAT_UNDEFINED);
  void end_render_pass();

  void bind_pipeline(::VkPipeline pipeline, VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS);
  void bind_vertex_buffer(VkBuffer buffer, uint32_t binding = 0, VkDeviceSize offset = 0);
  void bind_index_buffer(VkBuffer buffer, VkIndexType indexType = VK_INDEX_TYPE_UINT16, VkDeviceSize offset = 0);
  void bind_descriptor_set(VkPipelineLayout layout, VkDescriptorSet set, uint32_t setIndex = 0);

  void push_constants(VkPipelineLayout layout, VkShaderStageFlags stageFlags,
                       uint32_t offset, uint32_t size, const void* data);

  void set_viewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f);
  void set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
  void set_blend_constants(const float* constants);
  void set_stencil_reference(uint32_t reference);

  void draw(uint32_t vertexCount, uint32_t firstVertex = 0);
  void draw_indexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0);
  void draw_indirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride = 0);

  // Resource management
  VkCommandBuffer current_cmd() const { return m_frames[m_currentFrame].commandBuffer; }
  FrameContext& current_frame_ctx() { return m_frames[m_currentFrame]; }

  // Upload helpers
  bool upload_buffer(VkBuffer dst, const void* data, VkDeviceSize size, VkDeviceSize dstOffset = 0);
  bool upload_texture(VkImage dst, uint32_t width, uint32_t height, uint32_t depth,
                      VkFormat format, const void* data, uint32_t dataSize,
                      uint32_t mipLevel = 0, uint32_t arrayLayer = 0);
  void transition_image_layout(VkImage image, VkFormat format,
                                VkImageLayout oldLayout, VkImageLayout newLayout,
                                uint32_t mipLevel = 0, uint32_t levelCount = 1);

private:
  void create_frame_resources(FrameContext& frame);
  void destroy_frame_resources(FrameContext& frame);
  bool grow_staging(VkDeviceSize needed);

  VkDevice* m_device = nullptr;
  std::vector<FrameContext> m_frames;
  uint32_t m_currentFrame = 0;
  uint32_t m_frameCount = 0;

  // Persistent staging ring buffer
  VkBuffer m_persistentStaging = VK_NULL_HANDLE;
  VkDeviceMemory m_persistentStagingMemory = VK_NULL_HANDLE;
  void* m_persistentStagingMapped = nullptr;
  VkDeviceSize m_persistentStagingSize = 4 * 1024 * 1024; // 4MB
  VkDeviceSize m_stagingWriteOffset = 0; // Current write position in ring buffer
  static constexpr VkDeviceSize kStagingAlign = 256; // Alignment for sub-allocations
};

} // namespace vkwind
