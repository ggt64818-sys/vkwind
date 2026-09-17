#pragma once

#include "d3d9_device.h"
#include <cstdint>

namespace vkwind {

class D3D9Texture : public IDirect3DTexture9 {
public:
  D3D9Texture(D3D9Device* device, uint32_t width, uint32_t height, uint32_t levels,
              uint32_t usage, D3DFORMAT format, D3DPOOL pool);
  ~D3D9Texture();

  // IDirect3DResource9
  uint32_t AddRef() override;
  uint32_t Release() override;
  int QueryInterface(const void* iid, void** obj) override;
  void GetDevice(IDirect3DDevice9** ppDevice) override;
  void SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) override;
  void GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) override;
  void FreePrivateData(uint32_t refguid) override;
  uint32_t SetPriority(uint32_t PriorityNew) override;
  uint32_t GetPriority() override;
  void PreLoad() override;
  D3DRESOURCETYPE GetType() override;

  // IDirect3DBaseTexture9
  uint32_t SetLOD(uint32_t LODNew) override;
  uint32_t GetLOD() override;
  uint32_t GetLevelCount() override;

  // IDirect3DTexture9
  uint32_t GetLevelDesc(uint32_t Level, void* pDesc) override;
  uint32_t GetSurfaceLevel(uint32_t Level, IDirect3DSurface9** ppSurfaceLevel) override;
  uint32_t LockRect(uint32_t Level, void* pLockedRect, const void* pRect, uint32_t Flags) override;
  uint32_t UnlockRect(uint32_t Level) override;
  uint32_t AddDirtyRect(const void* pDirtyRect) override;

  // Internal
  uint32_t width() const { return m_width; }
  uint32_t height() const { return m_height; }
  D3DFORMAT format() const { return m_format; }
  uint32_t levels() const { return m_levels; }
  VkImageView image_view(uint32_t level = 0) const;

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  uint32_t m_levels = 0;
  uint32_t m_usage = 0;
  D3DFORMAT m_format = D3DFMT_UNKNOWN;
  D3DPOOL m_pool = D3DPOOL_DEFAULT;

  struct LevelData {
    void* data = nullptr;
    uint32_t pitch = 0;
    uint32_t size = 0;
  };
  std::vector<LevelData> m_levelsData;

  // GPU-side image per mip level
  struct GpuLevel {
    Image image;
  };
  std::vector<GpuLevel> m_gpuLevels;
};

} // namespace vkwind
