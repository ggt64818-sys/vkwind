#include "d3d9_state_mapper.h"
#include "../util/util_log.h"

namespace vkwind {

static const char* kTag = "StateMapper";

void StateMapper::map_render_states(const uint32_t renderStates[256], PipelineState& out) {
  // ZEnable
  out.depthTestEnable = renderStates[D3DRS_ZENABLE] ? VK_TRUE : VK_FALSE;
  out.depthWriteEnable = renderStates[D3DRS_ZWRITEENABLE] ? VK_TRUE : VK_FALSE;
  out.depthCompareOp = map_compare_func(static_cast<D3DCMPFUNC>(renderStates[D3DRS_ZFUNC]));

  // Alpha blending
  out.blendEnable = renderStates[D3DRS_ALPHABLENDENABLE] ? VK_TRUE : VK_FALSE;
  out.srcColorBlend = map_blend_factor(static_cast<D3DBLEND>(renderStates[D3DRS_SRCBLEND]));
  out.dstColorBlend = map_blend_factor(static_cast<D3DBLEND>(renderStates[D3DRS_DESTBLEND]));
  out.colorBlendOp = map_blend_op(renderStates[D3DRS_BLENDOP]);

  if (renderStates[D3DRS_SEPARATEALPHABLENDENABLE]) {
    out.srcAlphaBlend = map_blend_factor(static_cast<D3DBLEND>(renderStates[D3DRS_SRCBLENDALPHA]));
    out.dstAlphaBlend = map_blend_factor(static_cast<D3DBLEND>(renderStates[D3DRS_DESTBLENDALPHA]));
    out.alphaBlendOp = map_blend_op(renderStates[D3DRS_BLENDOPALPHA]);
  } else {
    out.srcAlphaBlend = out.srcColorBlend;
    out.dstAlphaBlend = out.dstColorBlend;
    out.alphaBlendOp = out.colorBlendOp;
  }

  // Color write mask
  out.colorWriteMask = 0;
  if (renderStates[D3DRS_COLORWRITEENABLE] & 0x1) out.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
  if (renderStates[D3DRS_COLORWRITEENABLE] & 0x2) out.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
  if (renderStates[D3DRS_COLORWRITEENABLE] & 0x4) out.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
  if (renderStates[D3DRS_COLORWRITEENABLE] & 0x8) out.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;

  // Cull mode
  out.cullMode = map_cull_mode(static_cast<D3DCULL>(renderStates[D3DRS_CULLMODE]));

  // Fill mode
  out.polygonMode = map_fill_mode(static_cast<D3DFILLMODE>(renderStates[D3DRS_FILLMODE]));

  // Stencil
  out.stencilTestEnable = renderStates[D3DRS_STENCILENABLE] ? VK_TRUE : VK_FALSE;
  out.stencilCompareOp = map_compare_func(static_cast<D3DCMPFUNC>(renderStates[D3DRS_STENCILFUNC]));
  out.stencilFailOp = map_stencil_op(static_cast<D3DSTENCILOP>(renderStates[D3DRS_STENCILFAIL]));
  out.stencilDepthFailOp = map_stencil_op(static_cast<D3DSTENCILOP>(renderStates[D3DRS_STENCILZFAIL]));
  out.stencilPassOp = map_stencil_op(static_cast<D3DSTENCILOP>(renderStates[D3DRS_STENCILPASS]));
  out.stencilReadMask = renderStates[D3DRS_STENCILMASK] ? renderStates[D3DRS_STENCILMASK] : 0xFF;
  out.stencilWriteMask = renderStates[D3DRS_STENCILWRITEMASK] ? renderStates[D3DRS_STENCILWRITEMASK] : 0xFF;
  out.stencilReference = renderStates[D3DRS_STENCILREF];

  // Back-face stencil (two-sided)
  if (renderStates[D3DRS_TWOSIDEDSTENCILMODE]) {
    out.backStencilCompareOp = map_compare_func(static_cast<D3DCMPFUNC>(renderStates[D3DRS_CCW_STENCILFUNC]));
    out.backStencilFailOp = map_stencil_op(static_cast<D3DSTENCILOP>(renderStates[D3DRS_CCW_STENCILFAIL]));
    out.backStencilDepthFailOp = map_stencil_op(static_cast<D3DSTENCILOP>(renderStates[D3DRS_CCW_STENCILZFAIL]));
    out.backStencilPassOp = map_stencil_op(static_cast<D3DSTENCILOP>(renderStates[D3DRS_CCW_STENCILPASS]));
  } else {
    out.backStencilCompareOp = out.stencilCompareOp;
    out.backStencilFailOp = out.stencilFailOp;
    out.backStencilDepthFailOp = out.stencilDepthFailOp;
    out.backStencilPassOp = out.stencilPassOp;
  }

  // Line width
  out.lineWidth = 1.0f;

  VKWIND_TRACE(kTag, "Mapped render states: depth=%d, blend=%d, cull=%d, fill=%d",
    out.depthTestEnable, out.blendEnable, out.cullMode, out.polygonMode);
}

VkPrimitiveTopology StateMapper::map_primitive_type(uint32_t d3dPrimitiveType) {
  // D3DPRIMITIVETYPE values
  switch (d3dPrimitiveType) {
    case 1:  return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;     // D3DPT_POINTLIST
    case 2:  return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;       // D3DPT_LINELIST
    case 3:  return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;      // D3DPT_LINESTRIP
    case 4:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;   // D3DPT_TRIANGLELIST
    case 5:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;  // D3DPT_TRIANGLESTRIP
    case 6:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;    // D3DPT_TRIANGLEFAN
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  }
}

VkCompareOp StateMapper::map_compare_func(D3DCMPFUNC func) {
  switch (func) {
    case D3DCMP_NEVER:         return VK_COMPARE_OP_NEVER;
    case D3DCMP_LESS:          return VK_COMPARE_OP_LESS;
    case D3DCMP_EQUAL:         return VK_COMPARE_OP_EQUAL;
    case D3DCMP_LESSEQUAL:     return VK_COMPARE_OP_LESS_OR_EQUAL;
    case D3DCMP_GREATER:       return VK_COMPARE_OP_GREATER;
    case D3DCMP_NOTEQUAL:      return VK_COMPARE_OP_NOT_EQUAL;
    case D3DCMP_GREATEREQUAL:  return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case D3DCMP_ALWAYS:        return VK_COMPARE_OP_ALWAYS;
    default:                   return VK_COMPARE_OP_ALWAYS;
  }
}

VkBlendFactor StateMapper::map_blend_factor(D3DBLEND blend) {
  switch (blend) {
    case D3DBLEND_ZERO:              return VK_BLEND_FACTOR_ZERO;
    case D3DBLEND_ONE:               return VK_BLEND_FACTOR_ONE;
    case D3DBLEND_SRCCOLOR:          return VK_BLEND_FACTOR_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:       return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA:          return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:       return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA:         return VK_BLEND_FACTOR_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR:         return VK_BLEND_FACTOR_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT:       return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case D3DBLEND_BOTHSRCALPHA:      return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3DBLEND_BOTHINVSRCALPHA:   return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_BLENDFACTOR:       return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3DBLEND_INVBLENDFACTOR:    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case D3DBLEND_SRCCOLOR2:         return VK_BLEND_FACTOR_SRC1_COLOR;
    case D3DBLEND_INVSRCCOLOR2:      return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    default:                         return VK_BLEND_FACTOR_ONE;
  }
}

VkBlendOp StateMapper::map_blend_op(uint32_t d3dBlendOp) {
  // D3DBLENDOP values: 1=ADD, 2=SUB, 3=REVSUB, 4=MIN, 5=MAX
  switch (d3dBlendOp) {
    case 1:  return VK_BLEND_OP_ADD;
    case 2:  return VK_BLEND_OP_SUBTRACT;
    case 3:  return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 4:  return VK_BLEND_OP_MIN;
    case 5:  return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
  }
}

VkStencilOp StateMapper::map_stencil_op(D3DSTENCILOP op) {
  switch (op) {
    case D3DSTENCILOP_KEEP:    return VK_STENCIL_OP_KEEP;
    case D3DSTENCILOP_ZERO:    return VK_STENCIL_OP_ZERO;
    case D3DSTENCILOP_REPLACE: return VK_STENCIL_OP_REPLACE;
    case D3DSTENCILOP_INCRSAT: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case D3DSTENCILOP_DECRSAT: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case D3DSTENCILOP_INVERT:  return VK_STENCIL_OP_INVERT;
    case D3DSTENCILOP_INCR:    return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case D3DSTENCILOP_DECR:    return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:                   return VK_STENCIL_OP_KEEP;
  }
}

VkCullModeFlags StateMapper::map_cull_mode(D3DCULL cull) {
  switch (cull) {
    case D3DCULL_NONE:  return VK_CULL_MODE_NONE;
    case D3DCULL_CW:    return VK_CULL_MODE_FRONT_BIT;
    case D3DCULL_CCW:   return VK_CULL_MODE_BACK_BIT;
    default:            return VK_CULL_MODE_BACK_BIT;
  }
}

VkPolygonMode StateMapper::map_fill_mode(D3DFILLMODE fill) {
  switch (fill) {
    case D3DFILL_POINT:     return VK_POLYGON_MODE_POINT;
    case D3DFILL_WIREFRAME: return VK_POLYGON_MODE_LINE;
    case D3DFILL_SOLID:     return VK_POLYGON_MODE_FILL;
    default:                return VK_POLYGON_MODE_FILL;
  }
}

VkFormat StateMapper::map_format(D3DFORMAT format) {
  switch (format) {
    case D3DFMT_R8G8B8:       return VK_FORMAT_R8G8B8_UNORM;
    case D3DFMT_A8R8G8B8:     return VK_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_X8R8G8B8:     return VK_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_R5G6B5:       return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case D3DFMT_X1R5G5B5:     return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case D3DFMT_A1R5G5B5:     return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case D3DFMT_A4R4G4B4:     return VK_FORMAT_A4B4G4R4_UNORM_PACK16;
    case D3DFMT_A8B8G8R8:     return VK_FORMAT_R8G8B8A8_UNORM;
    case D3DFMT_X8B8G8R8:     return VK_FORMAT_R8G8B8A8_UNORM;
    case D3DFMT_A2R10G10B10:  return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case D3DFMT_A2B10G10R10:  return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case D3DFMT_G16R16:       return VK_FORMAT_R16G16_UNORM;
    case D3DFMT_R16F:         return VK_FORMAT_R16_SFLOAT;
    case D3DFMT_G16R16F:      return VK_FORMAT_R16G16_SFLOAT;
    case D3DFMT_A16B16G16R16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case D3DFMT_R32F:         return VK_FORMAT_R32_SFLOAT;
    case D3DFMT_G32R32F:      return VK_FORMAT_R32G32_SFLOAT;
    case D3DFMT_A32B32G32R32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case D3DFMT_D16:          return VK_FORMAT_D16_UNORM;
    case D3DFMT_D24S8:        return VK_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D24X8:        return VK_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D32:          return VK_FORMAT_D32_SFLOAT;
    case D3DFMT_D16_LOCKABLE: return VK_FORMAT_D16_UNORM;
    case D3DFMT_D15S1:        return VK_FORMAT_D16_UNORM_S8_UINT;
    case D3DFMT_D24X4S4:      return VK_FORMAT_D24_UNORM_S8_UINT;
    default:                  return VK_FORMAT_UNDEFINED;
  }
}

VkFilter StateMapper::map_filter(D3DTEXTUREFILTERTYPE filter) {
  switch (filter) {
    case D3DTEXF_POINT:           return VK_FILTER_NEAREST;
    case D3DTEXF_LINEAR:          return VK_FILTER_LINEAR;
    case D3DTEXF_ANISOTROPIC:     return VK_FILTER_LINEAR;
    case D3DTEXF_PYRAMIDALQUAD:   return VK_FILTER_LINEAR;
    case D3DTEXF_GAUSSIANQUAD:    return VK_FILTER_LINEAR;
    default:                      return VK_FILTER_NEAREST;
  }
}

VkSamplerAddressMode StateMapper::map_address_mode(D3DTEXTUREADDRESS addr) {
  switch (addr) {
    case D3DTADDRESS_WRAP:      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case D3DTADDRESS_MIRROR:    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case D3DTADDRESS_CLAMP:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case D3DTADDRESS_BORDER:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case D3DTADDRESS_MIRRORONCE: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    default:                    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  }
}

VkSampleCountFlagBits StateMapper::map_multisample_type(D3DMULTISAMPLE_TYPE type) {
  if (type == D3DMULTISAMPLE_NONE) return VK_SAMPLE_COUNT_1_BIT;
  if (type >= D3DMULTISAMPLE_2_SAMPLES && type <= D3DMULTISAMPLE_16_SAMPLES) {
    return static_cast<VkSampleCountFlagBits>(1u << (type - 1));
  }
  return VK_SAMPLE_COUNT_1_BIT;
}

} // namespace vkwind
