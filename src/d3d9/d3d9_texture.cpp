#include "d3d9_texture.h"
#include "d3d9_surface.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "D3D9Texture";

D3D9Texture::D3D9Texture(D3D9Device* device, uint32_t width, uint32_t height, uint32_t levels,
                          uint32_t usage, D3DFORMAT format, D3DPOOL pool)
  : m_device(device), m_width(width), m_height(height), m_levels(levels),
    m_usage(usage), m_format(format), m_pool(pool) {
  if (m_levels == 0) {
    m_levels = 1;
    uint32_t w = width, h = height;
    while (w > 1 || h > 1) {
      m_levels++;
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
    }
  }

  m_levelsData.resize(m_levels);
  uint32_t w = width, h = height;
  for (uint32_t i = 0; i < m_levels; i++) {
    // Calculate size based on format
    uint32_t bpp = 4; // Default to 32-bit
    if (format == D3DFMT_R8G8B8 || format == D3DFMT_X8R8G8B8) bpp = 3;
    else if (format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5 || format == D3DFMT_A1R5G5B5) bpp = 2;

    m_levelsData[i].pitch = w * bpp;
    m_levelsData[i].size = m_levelsData[i].pitch * h;

    if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_MANAGED) {
      m_levelsData[i].data = calloc(1, m_levelsData[i].size);
    }

    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }

  VKWIND_DBG(kTag, "Created texture %ux%u, %u levels, fmt=%d", width, height, m_levels, format);
}

D3D9Texture::~D3D9Texture() {
  for (auto& level : m_levelsData) {
    if (level.data) free(level.data);
  }
  m_levelsData.clear();
  m_gpuLevels.clear();
}

uint32_t D3D9Texture::AddRef() { return ++m_refCount; }

uint32_t D3D9Texture::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9Texture::QueryInterface(const void* iid, void** obj) {
  (void)iid; *obj = nullptr;
  return D3DERR_NOTAVAILABLE;
}

void D3D9Texture::GetDevice(IDirect3DDevice9** ppDevice) {
  *ppDevice = m_device;
  if (*ppDevice) (*ppDevice)->AddRef();
}

void D3D9Texture::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {
  (void)refguid; (void)pData; (void)SizeOfData; (void)Flags;
}

void D3D9Texture::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {
  (void)refguid; (void)pData; (void)pSizeOfData;
}

void D3D9Texture::FreePrivateData(uint32_t refguid) { (void)refguid; }

uint32_t D3D9Texture::SetPriority(uint32_t PriorityNew) { (void)PriorityNew; return 0; }
uint32_t D3D9Texture::GetPriority() { return 0; }
void D3D9Texture::PreLoad() {}
D3DRESOURCETYPE D3D9Texture::GetType() { return D3DRTYPE_TEXTURE; }

uint32_t D3D9Texture::SetLOD(uint32_t LODNew) { (void)LODNew; return 0; }
uint32_t D3D9Texture::GetLOD() { return 0; }
uint32_t D3D9Texture::GetLevelCount() { return m_levels; }

uint32_t D3D9Texture::GetLevelDesc(uint32_t Level, void* pDesc) {
  if (Level >= m_levels || !pDesc) return D3DERR_INVALIDCALL;
  auto* desc = static_cast<D3DSURFACE_DESC*>(pDesc);
  desc->Format = m_format;
  desc->Type = D3DRTYPE_TEXTURE;
  desc->Usage = m_usage;
  desc->Pool = m_pool;
  desc->MultiSampleType = D3DMULTISAMPLE_NONE;
  desc->Width = std::max(1u, m_width >> Level);
  desc->Height = std::max(1u, m_height >> Level);
  return D3D_OK;
}

uint32_t D3D9Texture::GetSurfaceLevel(uint32_t Level, IDirect3DSurface9** ppSurfaceLevel) {
  if (Level >= m_levels || !ppSurfaceLevel) return D3DERR_INVALIDCALL;
  uint32_t w = std::max(1u, m_width >> Level);
  uint32_t h = std::max(1u, m_height >> Level);
  auto* surface = new D3D9Surface(m_device, w, h, m_format);
  *ppSurfaceLevel = surface;
  return D3D_OK;
}

uint32_t D3D9Texture::LockRect(uint32_t Level, void* pLockedRect, const void* pRect, uint32_t Flags) {
  if (Level >= m_levels || !pLockedRect) return D3DERR_INVALIDCALL;

  auto& level = m_levelsData[Level];
  if (!level.data) {
    level.data = calloc(1, level.size);
  }

  auto* locked = static_cast<D3DLOCKED_RECT*>(pLockedRect);
  locked->Pitch = level.pitch;
  locked->pBits = level.data;

  return D3D_OK;
}

uint32_t D3D9Texture::UnlockRect(uint32_t Level) {
  if (Level >= m_levels) return D3DERR_INVALIDCALL;

  auto& level = m_levelsData[Level];
  if (level.data && level.size > 0 && m_device->texture_manager()) {
    uint32_t w = std::max(1u, m_width >> Level);
    uint32_t h = std::max(1u, m_height >> Level);
    auto result = m_device->texture_manager()->upload_texture(w, h, m_format, level.data, level.size);
    if (result.image.valid()) {
      if (Level >= m_gpuLevels.size()) {
        m_gpuLevels.resize(Level + 1);
      }
      m_gpuLevels[Level].image = std::move(result.image);
      VKWIND_DBG(kTag, "Uploaded mip level %u (%ux%u, %u bytes) -> GPU image stored", Level, w, h, level.size);
    } else {
      VKWIND_DBG(kTag, "Uploaded mip level %u (%ux%u, %u bytes) but GPU image invalid", Level, w, h, level.size);
    }
  }

  return D3D_OK;
}

VkImageView D3D9Texture::image_view(uint32_t level) const {
  if (level < m_gpuLevels.size() && m_gpuLevels[level].image.valid()) {
    return m_gpuLevels[level].image.view();
  }
  return VK_NULL_HANDLE;
}

uint32_t D3D9Texture::AddDirtyRect(const void* pDirtyRect) {
  (void)pDirtyRect;
  return D3D_OK;
}

} // namespace vkwind
