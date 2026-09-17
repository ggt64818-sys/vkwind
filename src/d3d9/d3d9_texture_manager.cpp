#include "d3d9_texture_manager.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "TextureManager";

static uint32_t round_up_pow2(uint32_t v) {
  v--;
  v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
  return v + 1;
}

D3D9TextureManager::D3D9TextureManager(VkDevice& device, CommandBufferManager& cmdManager)
    : m_device(device), m_cmd_manager(cmdManager) {
  // CommandBufferManager uses 2 frames by default
  m_perFrameStaging.resize(2);
}

D3D9TextureManager::~D3D9TextureManager() = default;

Buffer D3D9TextureManager::get_staging_buffer(uint32_t minSize) {
  uint32_t bucket = round_up_pow2(minSize);
  auto& pool = m_stagingPools[bucket];

  if (!pool.freeBuffers.empty()) {
    Buffer buf = std::move(pool.freeBuffers.back());
    pool.freeBuffers.pop_back();
    return buf;
  }

  VKWIND_TRACE(kTag, "Allocating staging buffer: %u bytes (bucket %u)", minSize, bucket);
  return Buffer(&m_device, bucket, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

D3D9TextureManager::UploadResult D3D9TextureManager::upload_texture(
    uint32_t width, uint32_t height, D3DFORMAT format,
    const void* data, size_t dataSize) {

  VkFormat vkFormat = StateMapper::map_format(format);

  // Create the GPU-side image
  Image gpuImage(&m_device, width, height, vkFormat,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  // Allocate staging buffer
  Buffer staging = get_staging_buffer(static_cast<uint32_t>(dataSize));

  // Copy CPU data into staging
  void* mapped = staging.map();
  if (!mapped) {
    VKWIND_ERR(kTag, "Failed to map staging buffer");
    return {};
  }
  std::memcpy(mapped, data, dataSize);
  staging.unmap();

  // Record copy command
  VkCommandBuffer cmd = m_cmd_manager.current_cmd();
  gpuImage.transition_layout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkBufferImageCopy copyRegion = {};
  copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copyRegion.imageSubresource.layerCount = 1;
  copyRegion.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(cmd, staging.handle(), gpuImage.handle(),
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

  gpuImage.transition_layout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // Track staging buffer lifetime
  uint32_t frameIdx = m_cmd_manager.current_frame();
  m_perFrameStaging[frameIdx].push_back(std::move(staging));

  VKWIND_DBG(kTag, "Uploaded texture %ux%u fmt=%u, %.1f KB",
    width, height, static_cast<uint32_t>(format), dataSize / 1024.0);

  UploadResult result;
  result.image = std::move(gpuImage);
  return result;
}

Image D3D9TextureManager::create_render_target(uint32_t width, uint32_t height, D3DFORMAT format) {
  VkFormat vkFormat = StateMapper::map_format(format);
  Image rt(&m_device, width, height, vkFormat,
           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  VKWIND_DBG(kTag, "Created render target %ux%u", width, height);
  return rt;
}

Image D3D9TextureManager::create_depth_stencil(uint32_t width, uint32_t height, D3DFORMAT format) {
  VkFormat vkFormat = StateMapper::map_format(format);
  Image ds(&m_device, width, height, vkFormat,
           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  VKWIND_DBG(kTag, "Created depth/stencil %ux%u", width, height);
  return ds;
}

void D3D9TextureManager::begin_frame() {
  uint32_t frameCount = static_cast<uint32_t>(m_perFrameStaging.size());
  uint32_t prevFrame = (m_cmd_manager.current_frame() + frameCount - 1) % frameCount;
  // Return staging buffers from previous frame to pool
  for (auto& buf : m_perFrameStaging[prevFrame]) {
    uint32_t bucket = round_up_pow2(static_cast<uint32_t>(buf.size()));
    m_stagingPools[bucket].freeBuffers.push_back(std::move(buf));
  }
  m_perFrameStaging[prevFrame].clear();
}

} // namespace vkwind
