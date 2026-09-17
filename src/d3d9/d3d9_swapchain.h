#pragma once

#include "d3d9_types.h"

namespace vkwind {

class D3D9Surface;

// D3D9 swap chain wrapper
class D3D9SwapChainImpl {
public:
  D3D9SwapChainImpl(void* device, const D3DPRESENT_PARAMETERS* params);
  ~D3D9SwapChainImpl();

  int present(const void* srcRect, const void* destRect, void* destWindowOverride, void* dirtyRegion);
  int get_back_buffer(uint32_t index, D3DBACKBUFFER_TYPE type, void** ppBackBuffer);

  uint32_t width() const { return m_params.BackBufferWidth; }
  uint32_t height() const { return m_params.BackBufferHeight; }

private:
  void* m_device = nullptr;
  D3DPRESENT_PARAMETERS m_params = {};
};

} // namespace vkwind
