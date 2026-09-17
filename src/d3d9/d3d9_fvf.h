#pragma once

#include "d3d9_types.h"
#include "../vulkan/vk_pipeline.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace vkwind {

// Parsed FVF (Flexible Vertex Format) data
struct FVFDescription {
  uint32_t rawFVF = 0;

  // Position
  bool hasPosition = false;
  bool hasPositionRHW = false; // Pre-transformed
  uint32_t positionComponents = 3; // xyz or xyzw

  // Normal
  bool hasNormal = false;

  // Point size
  bool hasPointSize = false;

  // Diffuse color
  bool hasDiffuse = false;

  // Specular color
  bool hasSpecular = false;

  // Texture coordinates
  uint32_t texCoordCount = 0;

  // Blend weights (XYZB1-5)
  uint32_t blendWeightCount = 0;
  bool hasBlendIndices = false;

  // Total stride in bytes
  uint32_t stride = 0;

  // Per-element descriptions
  struct Element {
    const char* semantic;
    uint32_t semanticIndex;
    VkFormat format;
    uint32_t offset;
  };
  std::vector<Element> elements;
};

class FVFParser {
public:
  // Parse a D3D9 FVF code
  static FVFDescription parse(uint32_t fvf);

  // Convert FVF description to Vulkan vertex input state
  static void to_vk_vertex_input(const FVFDescription& fvf,
                                  std::vector<VkVertexInputBindingDescription>& bindings,
                                  std::vector<VkVertexInputAttributeDescription>& attributes,
                                  uint32_t bindingIndex = 0);

  // Calculate stride for an FVF
  static uint32_t calculate_stride(uint32_t fvf);

  // Get format for a specific FVF element
  static VkFormat get_position_format(uint32_t fvf);
  static VkFormat get_texcoord_format(uint32_t fvf, uint32_t index);
};

} // namespace vkwind
