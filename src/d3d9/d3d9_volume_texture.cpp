#include "d3d9_volume_texture.h"
#include "../util/util_log.h"

namespace vkwind {

static const char* kTag = "D3D9VolumeTexture";

D3D9VolumeTexture::D3D9VolumeTexture(D3D9Device* device, uint32_t width, uint32_t height,
                                     uint32_t depth, uint32_t levels, uint32_t usage,
                                     D3DFORMAT format, D3DPOOL pool)
  : m_device(device), m_width(width), m_height(height), m_depth(depth),
    m_levels(levels), m_usage(usage), m_format(format), m_pool(pool) {
  VKWIND_DBG(kTag, "Created volume texture %ux%ux%u, %u levels (stub)", width, height, depth, levels);
}

D3D9VolumeTexture::~D3D9VolumeTexture() {}

uint32_t D3D9VolumeTexture::AddRef() { return ++m_refCount; }
uint32_t D3D9VolumeTexture::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9VolumeTexture::QueryInterface(const void* iid, void** obj) {
  (void)iid; *obj = nullptr;
  return D3DERR_NOTAVAILABLE;
}

void D3D9VolumeTexture::GetDevice(IDirect3DDevice9** ppDevice) {
  *ppDevice = m_device;
  if (*ppDevice) (*ppDevice)->AddRef();
}

void D3D9VolumeTexture::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9VolumeTexture::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9VolumeTexture::FreePrivateData(uint32_t refguid) {}
uint32_t D3D9VolumeTexture::SetPriority(uint32_t PriorityNew) { return 0; }
uint32_t D3D9VolumeTexture::GetPriority() { return 0; }
void D3D9VolumeTexture::PreLoad() {}
D3DRESOURCETYPE D3D9VolumeTexture::GetType() { return D3DRTYPE_VOLUMETEXTURE; }

uint32_t D3D9VolumeTexture::SetLOD(uint32_t LODNew) { return 0; }
uint32_t D3D9VolumeTexture::GetLOD() { return 0; }
uint32_t D3D9VolumeTexture::GetLevelCount() { return m_levels; }

} // namespace vkwind
