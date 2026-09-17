#pragma once

#include "../d3d9/d3d9_types.h"
#include "../vulkan/vk_pipeline.h"
#include <vulkan/vulkan.h>
#include <cstdint>

namespace vkwind {

// Maps D3D9 render states to Vulkan pipeline configuration
class StateMapper {
public:
  // Fill PipelineState from D3D9 render states
  static void map_render_states(const uint32_t renderStates[256], PipelineState& out);

  // Convert D3D9 primitive type to VkPrimitiveTopology
  static VkPrimitiveTopology map_primitive_type(uint32_t d3dPrimitiveType);

  // Convert D3D9 compare function to VkCompareOp
  static VkCompareOp map_compare_func(D3DCMPFUNC func);

  // Convert D3D9 blend to VkBlendFactor
  static VkBlendFactor map_blend_factor(D3DBLEND blend);

  // Convert D3D9 blend op to VkBlendOp
  static VkBlendOp map_blend_op(uint32_t d3dBlendOp);

  // Convert D3D9 stencil op to VkStencilOp
  static VkStencilOp map_stencil_op(D3DSTENCILOP op);

  // Convert D3D9 cull mode to VkCullModeFlags
  static VkCullModeFlags map_cull_mode(D3DCULL cull);

  // Convert D3D9 fill mode to VkPolygonMode
  static VkPolygonMode map_fill_mode(D3DFILLMODE fill);

  // Map D3D9 format to VkFormat
  static VkFormat map_format(D3DFORMAT format);

  // Map D3D9 texture filter to VkFilter
  static VkFilter map_filter(D3DTEXTUREFILTERTYPE filter);

  // Map D3D9 texture address to VkSamplerAddressMode
  static VkSamplerAddressMode map_address_mode(D3DTEXTUREADDRESS addr);

  // Map D3D9 multit_sample type to VkSampleCountFlagBits
  static VkSampleCountFlagBits map_multisample_type(D3DMULTISAMPLE_TYPE type);
};

} // namespace vkwind
