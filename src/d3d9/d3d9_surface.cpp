#include "d3d9_surface.h"
#include "../util/util_log.h"

namespace vkwind {

static const char* kTag = "D3D9Surface";

D3D9Surface::D3D9Surface(D3D9Device* device, uint32_t width, uint32_t height, D3DFORMAT format)
  : m_device(device), m_width(width), m_height(height), m_format(format) {
  uint32_t bpp = 4;
  if (format == D3DFMT_R8G8B8 || format == D3DFMT_X8R8G8B8) bpp = 3;
  else if (format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5 || format == D3DFMT_A1R5G5B5) bpp = 2;

  m_pitch = width * bpp;
  m_data = calloc(1, m_pitch * height);
  VKWIND_DBG(kTag, "Created surface %ux%u, fmt=%d", width, height, format);
}

D3D9Surface::~D3D9Surface() {
  if (m_data) free(m_data);
}

uint32_t D3D9Surface::AddRef() { return ++m_refCount; }
uint32_t D3D9Surface::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9Surface::QueryInterface(const void* iid, void** obj) {
  (void)iid; *obj = nullptr;
  return D3DERR_NOTAVAILABLE;
}

void D3D9Surface::GetDevice(IDirect3DDevice9** ppDevice) {
  *ppDevice = m_device;
  if (*ppDevice) (*ppDevice)->AddRef();
}

void D3D9Surface::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9Surface::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9Surface::FreePrivateData(uint32_t refguid) {}
uint32_t D3D9Surface::SetPriority(uint32_t PriorityNew) { return 0; }
uint32_t D3D9Surface::GetPriority() { return 0; }
void D3D9Surface::PreLoad() {}
D3DRESOURCETYPE D3D9Surface::GetType() { return D3DRTYPE_SURFACE; }

int D3D9Surface::GetContainer(const void* riid, void** ppContainer) {
  (void)riid;
  *ppContainer = nullptr;
  return D3D_OK;
}

int D3D9Surface::GetDesc(void* pDesc) {
  (void)pDesc;
  return D3D_OK;
}

int D3D9Surface::LockRect(void* pLockedRect, const void* pRect, uint32_t Flags) {
  if (!pLockedRect || m_locked) return D3DERR_INVALIDCALL;
  auto* locked = static_cast<D3DLOCKED_RECT*>(pLockedRect);
  locked->Pitch = m_pitch;
  locked->pBits = m_data;
  m_locked = true;
  return D3D_OK;
}

int D3D9Surface::UnlockRect() {
  m_locked = false;
  return D3D_OK;
}

} // namespace vkwind
