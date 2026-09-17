#pragma once

#include "../vulkan/vk_cmd_buffer.h"
#include "../vulkan/vk_device.h"
#include "../vulkan/vk_buffer.h"
#include "../vulkan/vk_image.h"
#include "../d3d9/d3d9_state_mapper.h"
#include "d3d9_types.h"
#include <memory>
#include <unordered_map>

namespace vkwind {

struct TextureKey {
  uint32_t width;
  uint32_t height;
  D3DFORMAT format;

  bool operator==(const TextureKey& o) const {
    return width == o.width && height == o.height && format == o.format;
  }
};

struct TextureKeyHash {
  size_t operator()(const TextureKey& k) const {
    size_t h = std::hash<uint32_t>{}(k.width);
    h ^= std::hash<uint32_t>{}(k.height) << 1;
    h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(k.format)) << 2;
    return h;
  }
};

// Manages GPU-side texture resources and CPU→GPU uploads
class D3D9TextureManager {
public:
  D3D9TextureManager(VkDevice& device, CommandBufferManager& cmdManager);
  ~D3D9TextureManager();

  // Upload a texture to GPU memory via staging buffer
  struct UploadResult {
    Image image;
  };
  UploadResult upload_texture(uint32_t width, uint32_t height, D3DFORMAT format,
                               const void* data, size_t dataSize);

  // Create a render target texture (GPU-only, no staging)
  Image create_render_target(uint32_t width, uint32_t height, D3DFORMAT format);

  // Create a depth/stencil texture
  Image create_depth_stencil(uint32_t width, uint32_t height, D3DFORMAT format);

  // Begin a new frame - release staging buffers from previous frames
  void begin_frame();

private:
  VkDevice& m_device;
  CommandBufferManager& m_cmd_manager;

  // Pool of staging buffers indexed by size bucket
  struct StagingPool {
    std::vector<Buffer> freeBuffers;
    uint32_t minSize = 0;
  };
  std::unordered_map<uint32_t, StagingPool> m_stagingPools;

  // Get or allocate a staging buffer of at least minSize
  Buffer get_staging_buffer(uint32_t minSize);

  // Per-frame staging buffers that need to stay alive until GPU is done
  std::vector<std::vector<Buffer>> m_perFrameStaging;
};

} // namespace vkwind
