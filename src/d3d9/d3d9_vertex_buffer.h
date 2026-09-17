#pragma once

#include "d3d9_device.h"
#include "../vulkan/vk_buffer.h"
#include <memory>

namespace vkwind {

class D3D9VertexBuffer : public IDirect3DVertexBuffer9 {
public:
  D3D9VertexBuffer(D3D9Device* device, uint32_t length, uint32_t usage, uint32_t fvf, D3DPOOL pool);
  ~D3D9VertexBuffer();

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

  int Lock(uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) override;
  int Unlock() override;
  int GetDesc(void* pDesc) override;

  void* data() const { return m_data; }
  uint32_t length() const { return m_length; }
  uint32_t fvf() const { return m_fvf; }

  VkBuffer gpu_buffer() const { return m_gpuBuffer ? m_gpuBuffer->handle() : VK_NULL_HANDLE; }
  void ensure_gpu_buffer(CommandBufferManager& cmdManager);

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  uint32_t m_length = 0;
  uint32_t m_usage = 0;
  uint32_t m_fvf = 0;
  D3DPOOL m_pool = D3DPOOL_DEFAULT;
  void* m_data = nullptr;
  bool m_locked = false;

  std::unique_ptr<Buffer> m_gpuBuffer;
  bool m_dirty = false;
};

class D3D9IndexBuffer : public IDirect3DIndexBuffer9 {
public:
  D3D9IndexBuffer(D3D9Device* device, uint32_t length, uint32_t usage, D3DFORMAT format, D3DPOOL pool);
  ~D3D9IndexBuffer();

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

  int Lock(uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) override;
  int Unlock() override;
  int GetDesc(void* pDesc) override;

  void* data() const { return m_data; }
  uint32_t length() const { return m_length; }
  D3DFORMAT format() const { return m_format; }

  VkBuffer gpu_buffer() const { return m_gpuBuffer ? m_gpuBuffer->handle() : VK_NULL_HANDLE; }
  VkIndexType index_type() const { return (m_format == D3DFMT_INDEX16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32; }
  void ensure_gpu_buffer(CommandBufferManager& cmdManager);

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  uint32_t m_length = 0;
  uint32_t m_usage = 0;
  D3DFORMAT m_format = D3DFMT_INDEX16;
  D3DPOOL m_pool = D3DPOOL_DEFAULT;
  void* m_data = nullptr;
  bool m_locked = false;

  std::unique_ptr<Buffer> m_gpuBuffer;
  bool m_dirty = false;
};

} // namespace vkwind
