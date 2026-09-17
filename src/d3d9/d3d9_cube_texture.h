#pragma once

#include "d3d9_device.h"
#include <cstdint>

namespace vkwind {

class D3D9CubeTexture : public IDirect3DCubeTexture9 {
public:
  D3D9CubeTexture(D3D9Device* device, uint32_t edgeLength, uint32_t levels,
                  uint32_t usage, D3DFORMAT format, D3DPOOL pool);
  ~D3D9CubeTexture();

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

  uint32_t SetLOD(uint32_t LODNew) override;
  uint32_t GetLOD() override;
  uint32_t GetLevelCount() override;

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  uint32_t m_edgeLength = 0;
  uint32_t m_levels = 0;
  uint32_t m_usage = 0;
  D3DFORMAT m_format = D3DFMT_UNKNOWN;
  D3DPOOL m_pool = D3DPOOL_DEFAULT;
};

} // namespace vkwind
