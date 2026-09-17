#include "d3d9_fvf.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "FVFParser";

FVFDescription FVFParser::parse(uint32_t fvf) {
  FVFDescription desc;
  desc.rawFVF = fvf;

  uint32_t offset = 0;

  // Position (bits 1-3)
  uint32_t posType = fvf & D3DFVF_POSITION_MASK;
  switch (posType) {
    case D3DFVF_XYZ:
      desc.hasPosition = true;
      desc.positionComponents = 3;
      break;
    case D3DFVF_XYZRHW:
      desc.hasPosition = true;
      desc.hasPositionRHW = true;
      desc.positionComponents = 4;
      break;
    case D3DFVF_XYZB1:
      desc.hasPosition = true;
      desc.positionComponents = 3;
      desc.blendWeightCount = 1;
      break;
    case D3DFVF_XYZB2:
      desc.hasPosition = true;
      desc.positionComponents = 3;
      desc.blendWeightCount = 2;
      break;
    case D3DFVF_XYZB3:
      desc.hasPosition = true;
      desc.positionComponents = 3;
      desc.blendWeightCount = 3;
      break;
    case D3DFVF_XYZB4:
      desc.hasPosition = true;
      desc.positionComponents = 3;
      desc.blendWeightCount = 4;
      break;
    case D3DFVF_XYZB5:
      desc.hasPosition = true;
      desc.positionComponents = 3;
      desc.blendWeightCount = 5;
      break;
    default:
      desc.hasPosition = false;
      break;
  }

  // Normal (bit 4)
  desc.hasNormal = (fvf & D3DFVF_NORMAL) != 0;

  // Point size (bit 5)
  desc.hasPointSize = (fvf & D3DFVF_PSIZE) != 0;

  // Diffuse (bit 6)
  desc.hasDiffuse = (fvf & D3DFVF_DIFFUSE) != 0;

  // Specular (bit 7)
  desc.hasSpecular = (fvf & D3DFVF_SPECULAR) != 0;

  // Texture coordinates (bits 8-11)
  desc.texCoordCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

  // Blend indices (if blend weights > 0)
  if (desc.blendWeightCount > 0) {
    desc.hasBlendIndices = true;
  }

  // Build element list and calculate stride
  offset = 0;

  if (desc.hasPosition) {
    FVFDescription::Element elem;
    elem.semantic = desc.hasPositionRHW ? "POSITIONT" : "POSITION";
    elem.semanticIndex = 0;
    elem.format = desc.positionComponents == 4 ? VK_FORMAT_R32G32B32A32_SFLOAT
                                                : VK_FORMAT_R32G32B32_SFLOAT;
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += desc.positionComponents * sizeof(float);
  }

  // Blend weights
  for (uint32_t i = 0; i < desc.blendWeightCount; i++) {
    FVFDescription::Element elem;
    elem.semantic = "BLENDWEIGHT";
    elem.semanticIndex = i;
    elem.format = VK_FORMAT_R32_SFLOAT;
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += sizeof(float);
  }

  // Blend indices
  if (desc.hasBlendIndices) {
    FVFDescription::Element elem;
    elem.semantic = "BLENDINDICES";
    elem.semanticIndex = 0;
    elem.format = VK_FORMAT_R8G8B8A8_UINT;
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += 4;
  }

  if (desc.hasNormal) {
    FVFDescription::Element elem;
    elem.semantic = "NORMAL";
    elem.semanticIndex = 0;
    elem.format = VK_FORMAT_R32G32B32_SFLOAT;
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += 3 * sizeof(float);
  }

  if (desc.hasPointSize) {
    FVFDescription::Element elem;
    elem.semantic = "PSIZE";
    elem.semanticIndex = 0;
    elem.format = VK_FORMAT_R32_SFLOAT;
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += sizeof(float);
  }

  if (desc.hasDiffuse) {
    FVFDescription::Element elem;
    elem.semantic = "COLOR";
    elem.semanticIndex = 0;
    elem.format = VK_FORMAT_R8G8B8A8_UNORM;
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += 4;
  }

  if (desc.hasSpecular) {
    FVFDescription::Element elem;
    elem.semantic = "COLOR";
    elem.semanticIndex = 1;
    elem.format = VK_FORMAT_R8G8B8A8_UNORM;
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += 4;
  }

  for (uint32_t i = 0; i < desc.texCoordCount; i++) {
    FVFDescription::Element elem;
    elem.semantic = "TEXCOORD";
    elem.semanticIndex = i;
    elem.format = VK_FORMAT_R32G32_SFLOAT; // Default to float2
    elem.offset = offset;
    desc.elements.push_back(elem);
    offset += 2 * sizeof(float);
  }

  desc.stride = offset;

  VKWIND_DBG(kTag, "Parsed FVF 0x%x: stride=%u, %zu elements, texCoords=%u",
    fvf, desc.stride, desc.elements.size(), desc.texCoordCount);

  return desc;
}

void FVFParser::to_vk_vertex_input(const FVFDescription& fvf,
                                     std::vector<VkVertexInputBindingDescription>& bindings,
                                     std::vector<VkVertexInputAttributeDescription>& attributes,
                                     uint32_t bindingIndex) {
  VkVertexInputBindingDescription binding = {};
  binding.binding = bindingIndex;
  binding.stride = fvf.stride;
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  bindings.push_back(binding);

  uint32_t location = 0;
  for (auto& elem : fvf.elements) {
    VkVertexInputAttributeDescription attr = {};
    attr.binding = bindingIndex;
    attr.location = location;
    attr.format = elem.format;
    attr.offset = elem.offset;
    attributes.push_back(attr);
    location++;
  }

  VKWIND_DBG(kTag, "Created %zu vertex attributes from FVF 0x%x", attributes.size(), fvf.rawFVF);
}

uint32_t FVFParser::calculate_stride(uint32_t fvf) {
  FVFDescription desc = parse(fvf);
  return desc.stride;
}

VkFormat FVFParser::get_position_format(uint32_t fvf) {
  uint32_t posType = fvf & D3DFVF_POSITION_MASK;
  if (posType == D3DFVF_XYZRHW || posType == D3DFVF_XYZB1 ||
      posType == D3DFVF_XYZB2 || posType == D3DFVF_XYZB3 ||
      posType == D3DFVF_XYZB4 || posType == D3DFVF_XYZB5) {
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
  return VK_FORMAT_R32G32B32_SFLOAT;
}

VkFormat FVFParser::get_texcoord_format(uint32_t fvf, uint32_t index) {
  // D3D9 always uses float2 for texcoords in FVF
  (void)fvf; (void)index;
  return VK_FORMAT_R32G32_SFLOAT;
}

} // namespace vkwind
