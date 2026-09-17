#include "d3d9_cube_texture.h"
#include "../util/util_log.h"

namespace vkwind {

static const char* kTag = "D3D9CubeTexture";

D3D9CubeTexture::D3D9CubeTexture(D3D9Device* device, uint32_t edgeLength, uint32_t levels,
                                 uint32_t usage, D3DFORMAT format, D3DPOOL pool)
  : m_device(device), m_edgeLength(edgeLength), m_levels(levels),
    m_usage(usage), m_format(format), m_pool(pool) {
  VKWIND_DBG(kTag, "Created cube texture %u, %u levels (stub)", edgeLength, levels);
}

D3D9CubeTexture::~D3D9CubeTexture() {}

uint32_t D3D9CubeTexture::AddRef() { return ++m_refCount; }
uint32_t D3D9CubeTexture::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9CubeTexture::QueryInterface(const void* iid, void** obj) {
  (void)iid; *obj = nullptr;
  return D3DERR_NOTAVAILABLE;
}

void D3D9CubeTexture::GetDevice(IDirect3DDevice9** ppDevice) {
  *ppDevice = m_device;
  if (*ppDevice) (*ppDevice)->AddRef();
}

void D3D9CubeTexture::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9CubeTexture::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9CubeTexture::FreePrivateData(uint32_t refguid) {}
uint32_t D3D9CubeTexture::SetPriority(uint32_t PriorityNew) { return 0; }
uint32_t D3D9CubeTexture::GetPriority() { return 0; }
void D3D9CubeTexture::PreLoad() {}
D3DRESOURCETYPE D3D9CubeTexture::GetType() { return D3DRTYPE_CUBETEXTURE; }

uint32_t D3D9CubeTexture::SetLOD(uint32_t LODNew) { return 0; }
uint32_t D3D9CubeTexture::GetLOD() { return 0; }
uint32_t D3D9CubeTexture::GetLevelCount() { return m_levels; }

} // namespace vkwind
