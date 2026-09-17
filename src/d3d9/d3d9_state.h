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
  uint32_t m_type = 0;
  uint32_t m_renderStates[256] = {};
  D3DMATRIX m_transforms[256] = {};
  D3DMATERIAL9 m_material = {};
  D3DVIEWPORT9 m_viewport = {};
  D3D9Device::ScissorRect m_scissor = {};
  bool m_scissorEnabled = false;
  uint32_t m_textureStageStates[8][32] = {};
  uint32_t m_samplerStates[8][14] = {};
  IDirect3DBaseTexture9* m_textures[8] = {};
  float m_clipPlanes[6][4] = {};
  struct LightState {
    D3DLIGHT9 light = {};
    bool enabled = false;
    bool defined = false;
  };
  LightState m_lights[8] = {};
  struct StreamSource {
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t stride = 0;
  };
  StreamSource m_streamSources[16] = {};
  uint32_t m_fvf = 0;
  IDirect3DIndexBuffer9* m_indexBuffer = nullptr;
  IDirect3DPixelShader9* m_pixelShader = nullptr;
  IDirect3DVertexShader9* m_vertexShader = nullptr;
  float m_vsFloatConstants[256 * 4] = {};
  float m_psFloatConstants[256 * 4] = {};
  int32_t m_vsIntConstants[16 * 4] = {};
  int32_t m_psIntConstants[16 * 4] = {};
  int32_t m_vsBoolConstants[16] = {};
  int32_t m_psBoolConstants[16] = {};

  bool m_hasRenderStates = false;
  bool m_hasTransforms = false;
  bool m_hasMaterial = false;
  bool m_hasViewport = false;
  bool m_hasScissor = false;
  bool m_hasTextureStageStates = false;
  bool m_hasSamplerStates = false;
  bool m_hasTextures = false;
  bool m_hasClipPlanes = false;
  bool m_hasLights = false;
  bool m_hasStreamSources = false;
  bool m_hasIndexBuffer = false;
  bool m_hasShaders = false;
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
