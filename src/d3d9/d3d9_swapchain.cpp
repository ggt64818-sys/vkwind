#include "d3d9_swapchain.h"
#include "../util/util_log.h"

namespace vkwind {

static const char* kTag = "D3D9SwapChain";

D3D9SwapChainImpl::D3D9SwapChainImpl(void* device, const D3DPRESENT_PARAMETERS* params)
  : m_device(device), m_params(*params) {
  VKWIND_INFO(kTag, "SwapChain created: %ux%u", m_params.BackBufferWidth, m_params.BackBufferHeight);
}

D3D9SwapChainImpl::~D3D9SwapChainImpl() {}

int D3D9SwapChainImpl::present(const void* srcRect, const void* destRect, void* destWindowOverride, void* dirtyRegion) {
  // TODO: Present through Vulkan swapchain
  return D3D_OK;
}

int D3D9SwapChainImpl::get_back_buffer(uint32_t index, D3DBACKBUFFER_TYPE type, void** ppBackBuffer) {
  (void)index; (void)type;
  *ppBackBuffer = nullptr;
  return D3D_OK;
}

} // namespace vkwind
