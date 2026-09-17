#pragma once

#include "vk_device.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace vkwind {

// Compiled SPIR-V shader
struct CompiledShader {
  std::vector<uint32_t> spirv;
  VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
  std::string entryPoint = "main";
};

// Vulkan pipeline with all state baked in
struct PipelineState {
  // Shaders
  VkShaderModule vertexShader = VK_NULL_HANDLE;
  VkShaderModule fragmentShader = VK_NULL_HANDLE;
  VkShaderModule geometryShader = VK_NULL_HANDLE;

  // Vertex input
  std::vector<VkVertexInputBindingDescription> vertexBindings;
  std::vector<VkVertexInputAttributeDescription> vertexAttributes;

  // Input assembly
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkBool32 primitiveRestart = VK_FALSE;

  // Viewport/scissor (dynamic)
  uint32_t viewportCount = 1;
  uint32_t scissorCount = 1;

  // Rasterization
  VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
  VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
  VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  float lineWidth = 1.0f;

  // Multisampling
  VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Depth/stencil
  VkBool32 depthTestEnable = VK_FALSE;
  VkBool32 depthWriteEnable = VK_FALSE;
  VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
  VkBool32 stencilTestEnable = VK_FALSE;
  VkCompareOp stencilCompareOp = VK_COMPARE_OP_ALWAYS;
  VkStencilOp stencilFailOp = VK_STENCIL_OP_KEEP;
  VkStencilOp stencilDepthFailOp = VK_STENCIL_OP_KEEP;
  VkStencilOp stencilPassOp = VK_STENCIL_OP_KEEP;
  uint32_t stencilReadMask = 0xFF;
  uint32_t stencilWriteMask = 0xFF;
  uint32_t stencilReference = 0;
  VkCompareOp backStencilCompareOp = VK_COMPARE_OP_ALWAYS;
  VkStencilOp backStencilFailOp = VK_STENCIL_OP_KEEP;
  VkStencilOp backStencilDepthFailOp = VK_STENCIL_OP_KEEP;
  VkStencilOp backStencilPassOp = VK_STENCIL_OP_KEEP;
  uint32_t backStencilReadMask = 0xFF;
  uint32_t backStencilWriteMask = 0xFF;

  // Blend
  VkBool32 blendEnable = VK_FALSE;
  VkBlendFactor srcColorBlend = VK_BLEND_FACTOR_ONE;
  VkBlendFactor dstColorBlend = VK_BLEND_FACTOR_ZERO;
  VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
  VkBlendFactor srcAlphaBlend = VK_BLEND_FACTOR_ONE;
  VkBlendFactor dstAlphaBlend = VK_BLEND_FACTOR_ZERO;
  VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;
  VkColorComponentFlags colorWriteMask = 0xF;

  // Pipeline layout (must match shader descriptor bindings)
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};

// Mali G57 MC2 optimized constants
constexpr VkDeviceSize MALI_PUSH_CONSTANT_MAX = 128;
constexpr VkDeviceSize MALI_UBO_MIN_ALIGNMENT = 16;

class VkPipeline {
public:
  VkPipeline(VkDevice* device);
  ~VkPipeline();

  // Shader compilation (HLSL -> SPIR-V -> VkShaderModule)
  CompiledShader compile_vertex_shader(const std::string& hlsl, const std::string& entry = "main");
  CompiledShader compile_pixel_shader(const std::string& hlsl, const std::string& entry = "main");

  // Pipeline creation
  ::VkPipeline create_graphics_pipeline(const PipelineState& state, VkRenderPass renderPass);
  ::VkPipeline create_graphics_pipeline(const PipelineState& state, VkRenderPass renderPass,
                                         VkPipelineCache cache);
  ::VkPipeline create_compute_pipeline(VkShaderModule computeShader);

  // Pipeline layout
  VkPipelineLayout create_pipeline_layout(VkDescriptorSetLayout descLayout = VK_NULL_HANDLE);
  VkPipelineLayout create_pipeline_layout_with_push_constants(VkDescriptorSetLayout descLayout,
    VkShaderStageFlags stageFlags, uint32_t pushConstantSize);
  void destroy_pipeline_layout(VkPipelineLayout layout);

  // Render pass
  VkRenderPass create_render_pass(VkFormat colorFormat, VkFormat depthFormat = VK_FORMAT_UNDEFINED);
  VkRenderPass create_render_pass_optimized(VkFormat colorFormat, VkFormat depthFormat,
                                             bool depthWrite, bool msaa = false);
  void destroy_render_pass(VkRenderPass rp);

  // Framebuffer
  VkFramebuffer create_framebuffer(VkRenderPass renderPass, VkImageView colorView,
                                    uint32_t width, uint32_t height);
  void destroy_framebuffer(VkFramebuffer fb);

  // Descriptors
  VkDescriptorSetLayout create_descriptor_set_layout(uint32_t bindingCount,
    const VkDescriptorSetLayoutBinding* bindings);
  void destroy_descriptor_set_layout(VkDescriptorSetLayout layout);

  VkDescriptorPool create_descriptor_pool(uint32_t maxSets,
    uint32_t poolSizeCount, const VkDescriptorPoolSize* poolSizes);
  void destroy_descriptor_pool(VkDescriptorPool pool);

  VkDescriptorSet allocate_descriptor_set(VkDescriptorPool pool, VkDescriptorSetLayout layout);
  void update_descriptor_set(VkDescriptorSet set, uint32_t binding,
    VkDescriptorType type, uint32_t count, const VkDescriptorImageInfo* imageInfo);
  void update_descriptor_set(VkDescriptorSet set, uint32_t binding,
    VkDescriptorType type, uint32_t count, const VkDescriptorBufferInfo* bufferInfo);

  // Batched descriptor updates (reduces vkUpdateDescriptorSets calls for Mali)
  void update_descriptor_set_batch(VkDescriptorSet set,
    uint32_t writeCount, const VkWriteDescriptorSet* writes);

  // Pipeline cache (persistent for Mali shader compile savings)
  VkPipelineCache create_pipeline_cache(const void* initialData = nullptr, size_t dataSize = 0);
  bool save_pipeline_cache(VkPipelineCache cache, const char* path);
  bool load_pipeline_cache(const char* path, std::vector<uint8_t>& data);
  void destroy_pipeline_cache(VkPipelineCache cache);

  // UBO alignment helper (Mali G57: minUniformBufferOffsetAlignment = 16)
  static VkDeviceSize align_ubo(VkDeviceSize size) {
    return (size + MALI_UBO_MIN_ALIGNMENT - 1) & ~(MALI_UBO_MIN_ALIGNMENT - 1);
  }

  // Cache management
  void clear_shader_cache();

private:
  VkDevice* m_device = nullptr;

  // Shader cache (module -> handle)
  struct ShaderCacheEntry {
    std::string source;
    VkShaderModule module;
  };
  std::vector<ShaderCacheEntry> m_shaderCache;
};

} // namespace vkwind
