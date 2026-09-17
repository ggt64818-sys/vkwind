#include "d3d9_state.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "D3D9State";

// --- StateBlock ---

void D3D9StateBlock::capture(D3D9Device* device, uint32_t type) {
  // TODO: Capture full state from device
  m_hasRenderStates = true;
  m_hasTransforms = true;
  m_hasMaterial = true;
}

void D3D9StateBlock::apply(D3D9Device* device) {
  if (m_hasMaterial) {
    device->SetMaterial(&m_material);
  }
  // TODO: Apply render states and transforms
}

void D3D9StateBlock::clear() {
  m_hasRenderStates = false;
  m_hasTransforms = false;
  m_hasMaterial = false;
}

// --- SwapChain ---

D3D9SwapChain::D3D9SwapChain(D3D9Device* device, const D3DPRESENT_PARAMETERS* params)
  : m_device(device), m_params(*params) {}

D3D9SwapChain::~D3D9SwapChain() {}

uint32_t D3D9SwapChain::AddRef() { return ++m_refCount; }
uint32_t D3D9SwapChain::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9SwapChain::QueryInterface(const void* iid, void** obj) { (void)iid; *obj = nullptr; return D3DERR_NOTAVAILABLE; }

int D3D9SwapChain::Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, void* pDirtyRegion, uint32_t Flags) {
  return m_device->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

int D3D9SwapChain::GetFrontBuffer(IDirect3DSurface9* pDestSurface) {
  (void)pDestSurface;
  return D3D_OK;
}

int D3D9SwapChain::GetBackBuffer(uint32_t BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) {
  return m_device->GetBackBuffer(0, BackBuffer, Type, ppBackBuffer);
}

int D3D9SwapChain::GetRasterStatus(void* pRasterStatus) {
  if (pRasterStatus) {
    auto* status = static_cast<D3DRASTER_STATUS*>(pRasterStatus);
    status->InVBlank = FALSE;
    status->ScanLine = 0;
  }
  return D3D_OK;
}

int D3D9SwapChain::GetDisplayMode(void* pMode) {
  return m_device->GetDisplayMode(0, static_cast<D3DDISPLAYMODE*>(pMode));
}

int D3D9SwapChain::GetDevice(IDirect3DDevice9** ppDevice) {
  *ppDevice = m_device;
  if (*ppDevice) (*ppDevice)->AddRef();
  return D3D_OK;
}

int D3D9SwapChain::GetPresentParameters(void* pPresentationParameters) {
  if (pPresentationParameters) {
    memcpy(pPresentationParameters, &m_params, sizeof(D3DPRESENT_PARAMETERS));
  }
  return D3D_OK;
}

// --- Query ---

D3D9Query::D3D9Query(D3D9Device* device, D3DQUERYTYPE type)
  : m_device(device), m_type(type) {}

D3D9Query::~D3D9Query() {}

uint32_t D3D9Query::AddRef() { return ++m_refCount; }
uint32_t D3D9Query::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9Query::QueryInterface(const void* iid, void** obj) { (void)iid; *obj = nullptr; return D3DERR_NOTAVAILABLE; }
void D3D9Query::GetDevice(IDirect3DDevice9** ppDevice) { *ppDevice = m_device; if (*ppDevice) (*ppDevice)->AddRef(); }
void D3D9Query::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9Query::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9Query::FreePrivateData(uint32_t refguid) {}

int D3D9Query::GetType() { return static_cast<int>(m_type); }
uint32_t D3D9Query::GetDataSize() { return 4; }

int D3D9Query::Issue(uint32_t IssueFlags) {
  m_inFlight = true;
  return D3D_OK;
}

int D3D9Query::GetData(void* pData, uint32_t SizeToFill, uint32_t* pGetDataResult) {
  m_inFlight = false;
  if (pData && SizeToFill >= 4) {
    *static_cast<uint32_t*>(pData) = 1; // Always return "done"
  }
  if (pGetDataResult) *pGetDataResult = 0; // S_FALSE = data available
  return D3D_OK;
}

} // namespace vkwind
