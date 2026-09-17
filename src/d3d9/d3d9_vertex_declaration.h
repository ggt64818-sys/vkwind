#pragma once

#include "../util/util_log.h"
#include "d3d9_types.h"
#include <vector>
#include <cstdint>

namespace vkwind {

class D3D9VertexDeclaration {
public:
  D3D9VertexDeclaration(const D3DVERTEXELEMENT9* pElements) {
    if (!pElements) return;
    // Copy elements until END marker (Stream=0xFF)
    for (const auto* e = pElements; e->Stream != 0xFF; e++) {
      m_elements.push_back(*e);
    }
    VKWIND_DBG("D3D9VertexDeclaration", "Created with %zu elements", m_elements.size());
  }

  ~D3D9VertexDeclaration() = default;

  const std::vector<D3DVERTEXELEMENT9>& elements() const { return m_elements; }

  uint32_t calculate_stride() const {
    uint32_t maxEnd = 0;
    for (const auto& e : m_elements) {
      uint32_t end = e.Offset + decl_type_size(e.Type);
      if (end > maxEnd) maxEnd = end;
    }
    return maxEnd;
  }

  static uint32_t decl_type_size(uint8_t type) {
    switch (type) {
      case D3DDECLTYPE_FLOAT1:    return 4;
      case D3DDECLTYPE_FLOAT2:    return 8;
      case D3DDECLTYPE_FLOAT3:    return 12;
      case D3DDECLTYPE_FLOAT4:    return 16;
      case D3DDECLTYPE_D3DCOLOR:  return 4;
      case D3DDECLTYPE_UBYTE4:    return 4;
      case D3DDECLTYPE_SHORT2:    return 4;
      case D3DDECLTYPE_SHORT4:    return 8;
      case D3DDECLTYPE_UBYTE4N:   return 4;
      case D3DDECLTYPE_SHORT2N:   return 4;
      case D3DDECLTYPE_SHORT4N:   return 8;
      case D3DDECLTYPE_USHORT2N:  return 4;
      case D3DDECLTYPE_USHORT4N:  return 8;
      case D3DDECLTYPE_UDEC3:     return 4;
      case D3DDECLTYPE_DEC3N:     return 4;
      case D3DDECLTYPE_FLOAT16_2: return 4;
      case D3DDECLTYPE_FLOAT16_4: return 8;
      default: return 4;
    }
  }

private:
  std::vector<D3DVERTEXELEMENT9> m_elements;
};

} // namespace vkwind
