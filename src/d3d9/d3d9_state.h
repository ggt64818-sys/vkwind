#pragma once

#include "d3d9_device.h"
#include <cstring>

namespace vkwind {

// D3D9 render state block for state management
class D3D9StateBlock {
public:
  D3D9StateBlock() = default;

  void capture(D3D9Device* device, uint32_t type);
  void apply(D3D9Device* device);
  void clear();

private:
  uint32_t m_renderStates[256] = {};
  D3DMATRIX m_transforms[256] = {};
  D3DMATERIAL9 m_material = {};
  bool m_hasRenderStates = false;
  bool m_hasTransforms = false;
  bool m_hasMaterial = false;
};

// D3D9 swap chain wrapper
class D3D9SwapChain : public IDirect3DSwapChain9 {
public:
  D3D9SwapChain(D3D9Device* device, const D3DPRESENT_PARAMETERS* params);
  ~D3D9SwapChain();

  uint32_t AddRef() override;
  uint32_t Release() override;
  int QueryInterface(const void* iid, void** obj) override;

  int Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, void* pDirtyRegion, uint32_t Flags) override;
  int GetFrontBuffer(IDirect3DSurface9* pDestSurface) override;
  int GetBackBuffer(uint32_t BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override;
  int GetRasterStatus(void* pRasterStatus) override;
  int GetDisplayMode(void* pMode) override;
  int GetDevice(IDirect3DDevice9** ppDevice) override;
  int GetPresentParameters(void* pPresentationParameters) override;

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  D3DPRESENT_PARAMETERS m_params = {};
};

// D3D9 query wrapper
class D3D9Query : public IDirect3DQuery9 {
public:
  D3D9Query(D3D9Device* device, D3DQUERYTYPE type);
  ~D3D9Query();

  uint32_t AddRef() override;
  uint32_t Release() override;
  int QueryInterface(const void* iid, void** obj) override;
  void GetDevice(IDirect3DDevice9** ppDevice) override;
  void SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) override;
  void GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) override;
  void FreePrivateData(uint32_t refguid) override;

  int GetType() override;
  uint32_t GetDataSize() override;
  int Issue(uint32_t IssueFlags) override;
  int GetData(void* pData, uint32_t SizeToFill, uint32_t* pGetDataResult) override;

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  D3DQUERYTYPE m_type;
  bool m_inFlight = false;
};

} // namespace vkwind
