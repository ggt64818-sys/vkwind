#include "d3d9_shader.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "D3D9Shader";

// --- PixelShader ---

D3D9PixelShader::D3D9PixelShader(D3D9Device* device, const uint32_t* bytecode)
  : m_device(device) {
  if (bytecode) {
    // Read DXBC size from first DWORD
    uint32_t size = bytecode[0];
    if (size > 0 && size < 1024 * 1024) {
      m_bytecode.resize(size / 4);
      memcpy(m_bytecode.data(), bytecode, size);
    }
  }
  VKWIND_DBG(kTag, "Created pixel shader (%zu bytes)", m_bytecode.size() * 4);
}

D3D9PixelShader::~D3D9PixelShader() {}

uint32_t D3D9PixelShader::AddRef() { return ++m_refCount; }
uint32_t D3D9PixelShader::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9PixelShader::QueryInterface(const void* iid, void** obj) { (void)iid; *obj = nullptr; return D3DERR_NOTAVAILABLE; }
void D3D9PixelShader::GetDevice(IDirect3DDevice9** ppDevice) { *ppDevice = m_device; if (*ppDevice) (*ppDevice)->AddRef(); }
void D3D9PixelShader::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9PixelShader::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9PixelShader::FreePrivateData(uint32_t refguid) {}
uint32_t D3D9PixelShader::SetPriority(uint32_t PriorityNew) { return 0; }
uint32_t D3D9PixelShader::GetPriority() { return 0; }
void D3D9PixelShader::PreLoad() {}
D3DRESOURCETYPE D3D9PixelShader::GetType() { return D3DRTYPE_SURFACE; } // DX9 doesn't have a shader resource type

int D3D9PixelShader::GetFunction(void* pFunction, uint32_t* pSizeOfData) {
  if (!pSizeOfData) return D3DERR_INVALIDCALL;
  uint32_t requiredSize = static_cast<uint32_t>(m_bytecode.size() * 4);
  if (!pFunction || *pSizeOfData < requiredSize) {
    *pSizeOfData = requiredSize;
    return D3DERR_MOREDATA;
  }
  memcpy(pFunction, m_bytecode.data(), requiredSize);
  *pSizeOfData = requiredSize;
  return D3D_OK;
}

// --- VertexShader ---

D3D9VertexShader::D3D9VertexShader(D3D9Device* device, const uint32_t* bytecode)
  : m_device(device) {
  if (bytecode) {
    uint32_t size = bytecode[0];
    if (size > 0 && size < 1024 * 1024) {
      m_bytecode.resize(size / 4);
      memcpy(m_bytecode.data(), bytecode, size);
    }
  }
  VKWIND_DBG(kTag, "Created vertex shader (%zu bytes)", m_bytecode.size() * 4);
}

D3D9VertexShader::~D3D9VertexShader() {}

uint32_t D3D9VertexShader::AddRef() { return ++m_refCount; }
uint32_t D3D9VertexShader::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9VertexShader::QueryInterface(const void* iid, void** obj) { (void)iid; *obj = nullptr; return D3DERR_NOTAVAILABLE; }
void D3D9VertexShader::GetDevice(IDirect3DDevice9** ppDevice) { *ppDevice = m_device; if (*ppDevice) (*ppDevice)->AddRef(); }
void D3D9VertexShader::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9VertexShader::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9VertexShader::FreePrivateData(uint32_t refguid) {}
uint32_t D3D9VertexShader::SetPriority(uint32_t PriorityNew) { return 0; }
uint32_t D3D9VertexShader::GetPriority() { return 0; }
void D3D9VertexShader::PreLoad() {}
D3DRESOURCETYPE D3D9VertexShader::GetType() { return D3DRTYPE_SURFACE; }

int D3D9VertexShader::GetFunction(void* pFunction, uint32_t* pSizeOfData) {
  if (!pSizeOfData) return D3DERR_INVALIDCALL;
  uint32_t requiredSize = static_cast<uint32_t>(m_bytecode.size() * 4);
  if (!pFunction || *pSizeOfData < requiredSize) {
    *pSizeOfData = requiredSize;
    return D3DERR_MOREDATA;
  }
  memcpy(pFunction, m_bytecode.data(), requiredSize);
  *pSizeOfData = requiredSize;
  return D3D_OK;
}

} // namespace vkwind
