#pragma once

#include "d3d9_device.h"

namespace vkwind {

class D3D9Surface : public IDirect3DSurface9 {
public:
  D3D9Surface(D3D9Device* device, uint32_t width, uint32_t height, D3DFORMAT format);
  ~D3D9Surface();

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

  int GetContainer(const void* riid, void** ppContainer) override;
  int GetDesc(void* pDesc) override;
  int LockRect(void* pLockedRect, const void* pRect, uint32_t Flags) override;
  int UnlockRect() override;

  // Internal
  uint32_t width() const { return m_width; }
  uint32_t height() const { return m_height; }
  D3DFORMAT format() const { return m_format; }

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  D3DFORMAT m_format = D3DFMT_UNKNOWN;
  void* m_data = nullptr;
  uint32_t m_pitch = 0;
  bool m_locked = false;
};

} // namespace vkwind
