#include "vk_cmd_buffer.h"
#include "../util/util_log.h"

#include <cstring>
#include <algorithm>

namespace vkwind {

static const char* kTag = "VkCmdBuffer";

CommandBufferManager::CommandBufferManager(VkDevice* device, uint32_t frameCount)
  : m_device(device), m_frameCount(frameCount) {
  m_frames.resize(frameCount);

  for (auto& frame : m_frames) {
    create_frame_resources(frame);
  }

  // Create persistent staging buffer
  VkBufferCreateInfo bufInfo = {};
  bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufInfo.size = m_persistentStagingSize;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  vkCreateBuffer(m_device->raw(), &bufInfo, nullptr, &m_persistentStaging);

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(m_device->raw(), m_persistentStaging, &memReqs);

  auto memProps = m_device->memory_properties();
  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;

  allocInfo.memoryTypeIndex = UINT32_MAX;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
    if ((memReqs.memoryTypeBits & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
      allocInfo.memoryTypeIndex = i;
      break;
    }
  }

  if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    VKWIND_ERR(kTag, "No host-visible memory for persistent staging buffer");
    return;
  }

  vkAllocateMemory(m_device->raw(), &allocInfo, nullptr, &m_persistentStagingMemory);
  vkBindBufferMemory(m_device->raw(), m_persistentStaging, m_persistentStagingMemory, 0);
  vkMapMemory(m_device->raw(), m_persistentStagingMemory, 0, m_persistentStagingSize, 0, &m_persistentStagingMapped);

  VKWIND_INFO(kTag, "Command buffer manager created (%u frames, %llu MB staging)",
    frameCount, (unsigned long long)(m_persistentStagingSize / (1024 * 1024)));
}

CommandBufferManager::~CommandBufferManager() {
  vkDeviceWaitIdle(m_device->raw());

  for (auto& frame : m_frames) {
    destroy_frame_resources(frame);
  }

  if (m_persistentStagingMapped) {
    vkUnmapMemory(m_device->raw(), m_persistentStagingMemory);
  }
  if (m_persistentStaging) {
    vkDestroyBuffer(m_device->raw(), m_persistentStaging, nullptr);
  }
  if (m_persistentStagingMemory) {
    vkFreeMemory(m_device->raw(), m_persistentStagingMemory, nullptr);
  }
}

void CommandBufferManager::create_frame_resources(FrameContext& frame) {
  // Command pool
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = m_device->graphics_queue_family();

  vkCreateCommandPool(m_device->raw(), &poolInfo, nullptr, &frame.commandPool);

  // Command buffer
  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = frame.commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  vkAllocateCommandBuffers(m_device->raw(), &allocInfo, &frame.commandBuffer);

  // Fence and semaphores
  frame.fence = m_device->create_fence(true);
  frame.imageAvailable = m_device->create_semaphore();
  frame.renderFinished = m_device->create_semaphore();
}

void CommandBufferManager::destroy_frame_resources(FrameContext& frame) {
  if (frame.fence) m_device->destroy_fence(frame.fence);
  if (frame.imageAvailable) m_device->destroy_semaphore(frame.imageAvailable);
  if (frame.renderFinished) m_device->destroy_semaphore(frame.renderFinished);
  if (frame.commandBuffer) {
    vkFreeCommandBuffers(m_device->raw(), frame.commandPool, 1, &frame.commandBuffer);
  }
  if (frame.commandPool) {
    vkDestroyCommandPool(m_device->raw(), frame.commandPool, nullptr);
  }
}

void CommandBufferManager::begin_frame() {
  auto& frame = m_frames[m_currentFrame];

  // Wait for this frame's fence — GPU is done with this frame's data
  vkWaitForFences(m_device->raw(), 1, &frame.fence, VK_TRUE, UINT64_MAX);
  vkResetFences(m_device->raw(), 1, &frame.fence);

  // Now it's safe to reuse this frame's region in the persistent staging ring buffer
  m_stagingWriteOffset = frame.persistentStagingStartOffset;

  // Reset command pool
  vkResetCommandPool(m_device->raw(), frame.commandPool, 0);

  // Reset staging offset
  frame.stagingOffset = 0;

  // Record where this frame starts writing in the ring buffer
  frame.persistentStagingStartOffset = m_stagingWriteOffset;

  // Begin command buffer
  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

  frame.submitted = false;
}

void CommandBufferManager::end_frame() {
  auto& frame = m_frames[m_currentFrame];
  vkEndCommandBuffer(frame.commandBuffer);
}

void CommandBufferManager::submit_frame(VkSemaphore waitSemaphore, VkSemaphore signalSemaphore) {
  auto& frame = m_frames[m_currentFrame];

  // End the command buffer if not already ended
  // (end_frame may or may not have been called)

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSems[] = {waitSemaphore};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  if (waitSemaphore) {
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSems;
    submitInfo.pWaitDstStageMask = waitStages;
  } else {
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;
  }

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &frame.commandBuffer;

  VkSemaphore signalSems[] = {signalSemaphore};
  if (signalSemaphore) {
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSems;
  } else {
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;
  }

  VkResult result = vkQueueSubmit(m_device->graphics_queue(), 1, &submitInfo, frame.fence);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to submit command buffer: %d", result);
  }

  frame.submitted = true;
}

void CommandBufferManager::advance_frame() {
  m_currentFrame = (m_currentFrame + 1) % m_frameCount;
}

void CommandBufferManager::wait_frame(uint32_t frameIndex) {
  auto& frame = m_frames[frameIndex];
  vkWaitForFences(m_device->raw(), 1, &frame.fence, VK_TRUE, UINT64_MAX);
}

void CommandBufferManager::begin_render_pass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                                              uint32_t width, uint32_t height,
                                              const float* clearColor,
                                              VkFormat depthFormat,
                                              float clearDepth, uint32_t clearStencil) {
  VkRenderPassBeginInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpInfo.renderPass = renderPass;
  rpInfo.framebuffer = framebuffer;
  rpInfo.renderArea.offset = {0, 0};
  rpInfo.renderArea.extent = {width, height};

  std::vector<VkClearValue> clearValues;
  VkClearValue colorClear = {};
  if (clearColor) {
    memcpy(colorClear.color.float32, clearColor, sizeof(float) * 4);
  } else {
    colorClear.color.float32[0] = 0.0f;
    colorClear.color.float32[1] = 0.0f;
    colorClear.color.float32[2] = 0.0f;
    colorClear.color.float32[3] = 1.0f;
  }
  clearValues.push_back(colorClear);

  if (depthFormat != VK_FORMAT_UNDEFINED) {
    VkClearValue depthClear = {};
    depthClear.depthStencil.depth = clearDepth;
    depthClear.depthStencil.stencil = clearStencil;
    clearValues.push_back(depthClear);
  }

  rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  rpInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(m_frames[m_currentFrame].commandBuffer, &rpInfo,
                        VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBufferManager::end_render_pass() {
  vkCmdEndRenderPass(m_frames[m_currentFrame].commandBuffer);
}

void CommandBufferManager::bind_pipeline(::VkPipeline pipeline, VkPipelineBindPoint bindPoint) {
  vkCmdBindPipeline(m_frames[m_currentFrame].commandBuffer, bindPoint, pipeline);
}

void CommandBufferManager::bind_vertex_buffer(VkBuffer buffer, uint32_t binding, VkDeviceSize offset) {
  VkBuffer buffers[] = {buffer};
  VkDeviceSize offsets[] = {offset};
  vkCmdBindVertexBuffers(m_frames[m_currentFrame].commandBuffer, binding, 1, buffers, offsets);
}

void CommandBufferManager::bind_index_buffer(VkBuffer buffer, VkIndexType indexType, VkDeviceSize offset) {
  vkCmdBindIndexBuffer(m_frames[m_currentFrame].commandBuffer, buffer, offset, indexType);
}

void CommandBufferManager::bind_descriptor_set(VkPipelineLayout layout, VkDescriptorSet set, uint32_t setIndex) {
  vkCmdBindDescriptorSets(m_frames[m_currentFrame].commandBuffer,
                           VK_PIPELINE_BIND_POINT_GRAPHICS, layout, setIndex, 1, &set, 0, nullptr);
}

void CommandBufferManager::push_constants(VkPipelineLayout layout, VkShaderStageFlags stageFlags,
                                           uint32_t offset, uint32_t size, const void* data) {
  vkCmdPushConstants(m_frames[m_currentFrame].commandBuffer, layout, stageFlags, offset, size, data);
}

void CommandBufferManager::set_viewport(float x, float y, float width, float height, float minDepth, float maxDepth) {
  VkViewport viewport = {x, y, width, height, minDepth, maxDepth};
  vkCmdSetViewport(m_frames[m_currentFrame].commandBuffer, 0, 1, &viewport);
}

void CommandBufferManager::set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
  VkRect2D scissor = {{x, y}, {width, height}};
  vkCmdSetScissor(m_frames[m_currentFrame].commandBuffer, 0, 1, &scissor);
}

void CommandBufferManager::set_blend_constants(const float* constants) {
  vkCmdSetBlendConstants(m_frames[m_currentFrame].commandBuffer, constants);
}

void CommandBufferManager::set_stencil_reference(uint32_t reference) {
  vkCmdSetStencilReference(m_frames[m_currentFrame].commandBuffer,
                            VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void CommandBufferManager::draw(uint32_t vertexCount, uint32_t firstVertex) {
  vkCmdDraw(m_frames[m_currentFrame].commandBuffer, vertexCount, 1, firstVertex, 0);
}

void CommandBufferManager::draw_indexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) {
  vkCmdDrawIndexed(m_frames[m_currentFrame].commandBuffer, indexCount, 1, firstIndex, vertexOffset, 0);
}

void CommandBufferManager::draw_indirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
  vkCmdDrawIndirect(m_frames[m_currentFrame].commandBuffer, buffer, offset, drawCount,
                     stride ? stride : sizeof(VkDrawIndirectCommand));
}

bool CommandBufferManager::upload_buffer(VkBuffer dst, const void* data, VkDeviceSize size, VkDeviceSize dstOffset) {
  // Align size to kStagingAlign
  VkDeviceSize alignedSize = (size + kStagingAlign - 1) & ~(kStagingAlign - 1);

  if (alignedSize > m_persistentStagingSize) {
    VKWIND_ERR(kTag, "Upload too large: %llu bytes", (unsigned long long)size);
    return false;
  }

  // Ring buffer sub-allocation: advance write offset, wrap around
  VkDeviceSize writeOffset = m_stagingWriteOffset;
  m_stagingWriteOffset += alignedSize;
  if (m_stagingWriteOffset >= m_persistentStagingSize) {
    // Wrap around — this frame's data starts at 0
    m_stagingWriteOffset = alignedSize;
    writeOffset = 0;
  }

  // Copy data into the sub-allocated region
  memcpy(static_cast<char*>(m_persistentStagingMapped) + writeOffset, data, static_cast<size_t>(size));

  // Record copy command from sub-allocated region
  VkBufferCopy copyRegion = {writeOffset, dstOffset, size};
  vkCmdCopyBuffer(m_frames[m_currentFrame].commandBuffer,
                    m_persistentStaging, dst, 1, &copyRegion);

  return true;
}

bool CommandBufferManager::upload_texture(VkImage dst, uint32_t width, uint32_t height, uint32_t depth,
                                           VkFormat format, const void* data, uint32_t dataSize,
                                           uint32_t mipLevel, uint32_t arrayLayer) {
  // Calculate size
  uint32_t bpp = 4;
  switch (format) {
    case VK_FORMAT_R8G8B8_UNORM: bpp = 3; break;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: bpp = 4; break;
    case VK_FORMAT_R16G16B16A16_SFLOAT: bpp = 8; break;
    case VK_FORMAT_R32G32B32A32_SFLOAT: bpp = 16; break;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: bpp = 8; break; // 4x4 block
    case 75: bpp = 16; break; // VK_FORMAT_BC3_UNORM_BLOCK
    case 81: bpp = 16; break; // VK_FORMAT_BC5_UNORM_BLOCK
    case VK_FORMAT_BC7_SRGB_BLOCK: bpp = 16; break;
    default: bpp = 4; break;
  }

  uint32_t rowPitch = width * bpp;
  // Align to 4 bytes
  rowPitch = (rowPitch + 3) & ~3u;

  uint32_t slicePitch = rowPitch * height;
  uint32_t totalSize = slicePitch * depth;

  if (dataSize < totalSize) {
    VKWIND_WARN(kTag, "Texture data too small: need %u, got %u", totalSize, dataSize);
    totalSize = dataSize;
  }

  // Stage the data
  if (!upload_buffer(m_persistentStaging, data, totalSize, 0)) {
    return false;
  }

  // Transition to transfer dst
  transition_image_layout(dst, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel);

  // Copy buffer to image
  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = rowPitch / bpp;
  region.bufferImageHeight = height;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = mipLevel;
  region.imageSubresource.baseArrayLayer = arrayLayer;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, depth};

  vkCmdCopyBufferToImage(m_frames[m_currentFrame].commandBuffer,
                          m_persistentStaging, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &region);

  return true;
}

void CommandBufferManager::transition_image_layout(VkImage image, VkFormat format,
                                                    VkImageLayout oldLayout, VkImageLayout newLayout,
                                                    uint32_t mipLevel, uint32_t levelCount) {
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = mipLevel;
  barrier.subresourceRange.levelCount = levelCount;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage, dstStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else {
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }

  vkCmdPipelineBarrier(m_frames[m_currentFrame].commandBuffer,
                        srcStage, dstStage, 0,
                        0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace vkwind
