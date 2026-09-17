#include "d3d9_state.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "D3D9State";

// --- StateBlock ---

void D3D9StateBlock::capture(D3D9Device* device, uint32_t type) {
  m_type = type;

  // Capture render states (D3DRS_*)
  if (type & 0x00000001) { // D3D9SB_ALL
    memcpy(m_renderStates, device->m_renderStates, sizeof(m_renderStates));
    m_hasRenderStates = true;
  }

  // Capture transforms (D3DTS_*)
  if (type & 0x00000002) {
    memcpy(m_transforms, device->m_transforms, sizeof(m_transforms));
    m_hasTransforms = true;
  }

  // Capture material
  if (type & 0x00000004) {
    m_material = device->m_material;
    m_hasMaterial = true;
  }

  // Capture viewport
  if (type & 0x00000008) {
    m_viewport = device->m_viewport;
    m_hasViewport = true;
  }

  // Capture scissor
  if (type & 0x00000010) {
    m_scissor = device->m_scissor;
    m_scissorEnabled = device->m_scissorEnabled;
    m_hasScissor = true;
  }

  // Capture texture stage states
  if (type & 0x00000020) {
    memcpy(m_textureStageStates, device->m_textureStageStates, sizeof(m_textureStageStates));
    m_hasTextureStageStates = true;
  }

  // Capture sampler states
  if (type & 0x00000040) {
    memcpy(m_samplerStates, device->m_samplerStates, sizeof(m_samplerStates));
    m_hasSamplerStates = true;
  }

  // Capture textures
  if (type & 0x00000080) {
    for (int i = 0; i < 8; i++) {
      m_textures[i] = device->m_textures[i];
      if (m_textures[i]) m_textures[i]->AddRef();
    }
    m_hasTextures = true;
  }

  // Capture clip planes
  if (type & 0x00000100) {
    memcpy(m_clipPlanes, device->m_clipPlanes, sizeof(m_clipPlanes));
    m_hasClipPlanes = true;
  }

  // Capture lights
  if (type & 0x00000200) {
    memcpy(m_lights, device->m_lights, sizeof(m_lights));
    m_hasLights = true;
  }

  // Capture stream source
  if (type & 0x00000400) {
    memcpy(m_streamSources, device->m_streamSources, sizeof(m_streamSources));
    m_fvf = device->m_fvf;
    m_hasStreamSources = true;
  }

  // Capture index buffer
  if (type & 0x00000800) {
    m_indexBuffer = device->m_indexBuffer;
    if (m_indexBuffer) m_indexBuffer->AddRef();
    m_hasIndexBuffer = true;
  }

  // Capture pixel/vertex shader
  if (type & 0x00001000) {
    m_pixelShader = device->m_pixelShader;
    if (m_pixelShader) m_pixelShader->AddRef();
    m_vertexShader = device->m_vertexShader;
    if (m_vertexShader) m_vertexShader->AddRef();
    memcpy(m_vsFloatConstants, device->m_vsFloatConstants, sizeof(m_vsFloatConstants));
    memcpy(m_psFloatConstants, device->m_psFloatConstants, sizeof(m_psFloatConstants));
    memcpy(m_vsIntConstants, device->m_vsIntConstants, sizeof(m_vsIntConstants));
    memcpy(m_psIntConstants, device->m_psIntConstants, sizeof(m_psIntConstants));
    memcpy(m_vsBoolConstants, device->m_vsBoolConstants, sizeof(m_vsBoolConstants));
    memcpy(m_psBoolConstants, device->m_psBoolConstants, sizeof(m_psBoolConstants));
    m_hasShaders = true;
  }
}

void D3D9StateBlock::apply(D3D9Device* device) {
  // Apply render states
  if (m_hasRenderStates) {
    for (uint32_t i = 0; i < 256; i++) {
      if (m_renderStates[i] != 0 || i == 0) {
        device->SetRenderState(i, m_renderStates[i]);
      }
    }
  }

  // Apply transforms
  if (m_hasTransforms) {
    for (uint32_t i = 0; i < 256; i++) {
      device->SetTransform(i, &m_transforms[i]);
    }
  }

  // Apply material
  if (m_hasMaterial) {
    device->SetMaterial(&m_material);
  }

  // Apply viewport
  if (m_hasViewport) {
    device->SetViewport(&m_viewport);
  }

  // Apply scissor
  if (m_hasScissor) {
    if (m_scissorEnabled) {
      device->SetScissorRect(&m_scissor);
    }
  }

  // Apply texture stage states
  if (m_hasTextureStageStates) {
    for (uint32_t stage = 0; stage < 8; stage++) {
      for (uint32_t state = 0; state < 32; state++) {
        if (m_textureStageStates[stage][state] != 0) {
          device->SetTextureStageState(stage, state, m_textureStageStates[stage][state]);
        }
      }
    }
  }

  // Apply sampler states
  if (m_hasSamplerStates) {
    for (uint32_t sampler = 0; sampler < 8; sampler++) {
      for (uint32_t state = 0; state < 14; state++) {
        device->SetSamplerState(sampler, state, m_samplerStates[sampler][state]);
      }
    }
  }

  // Apply textures
  if (m_hasTextures) {
    for (int i = 0; i < 8; i++) {
      device->SetTexture(i, m_textures[i]);
      if (m_textures[i]) m_textures[i]->Release();
      m_textures[i] = nullptr;
    }
  }

  // Apply clip planes
  if (m_hasClipPlanes) {
    for (uint32_t i = 0; i < 6; i++) {
      device->SetClipPlane(i, m_clipPlanes[i]);
    }
  }

  // Apply lights
  if (m_hasLights) {
    for (int i = 0; i < 8; i++) {
      if (m_lights[i].defined) {
        device->SetLight(i, &m_lights[i].light);
        device->LightEnable(i, m_lights[i].enabled);
      }
    }
  }

  // Apply stream sources
  if (m_hasStreamSources) {
    for (uint32_t i = 0; i < 16; i++) {
      if (m_streamSources[i].buffer) {
        device->SetStreamSource(i, m_streamSources[i].buffer,
                                m_streamSources[i].offset, m_streamSources[i].stride);
      }
    }
    device->SetFVF(m_fvf);
  }

  // Apply index buffer
  if (m_hasIndexBuffer && m_indexBuffer) {
    device->SetIndices(m_indexBuffer);
    m_indexBuffer->Release();
    m_indexBuffer = nullptr;
  }

  // Apply shaders
  if (m_hasShaders) {
    if (m_pixelShader) {
      device->SetPixelShader(m_pixelShader);
      m_pixelShader->Release();
      m_pixelShader = nullptr;
    }
    if (m_vertexShader) {
      device->SetVertexShader(m_vertexShader);
      m_vertexShader->Release();
      m_vertexShader = nullptr;
    }
    device->SetVertexShaderConstant(0, m_vsFloatConstants, 256);
    device->SetPixelShaderConstant(0, m_psFloatConstants, 256);
  }
}

void D3D9StateBlock::clear() {
  m_hasRenderStates = false;
  m_hasTransforms = false;
  m_hasMaterial = false;
  m_hasViewport = false;
  m_hasScissor = false;
  m_hasTextureStageStates = false;
  m_hasSamplerStates = false;
  m_hasTextures = false;
  m_hasClipPlanes = false;
  m_hasLights = false;
  m_hasStreamSources = false;
  m_hasIndexBuffer = false;
  m_hasShaders = false;
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
