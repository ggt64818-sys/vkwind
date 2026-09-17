#include "d3d9_vertex_buffer.h"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "D3D9Buffer";

// --- VertexBuffer ---

D3D9VertexBuffer::D3D9VertexBuffer(D3D9Device* device, uint32_t length, uint32_t usage, uint32_t fvf, D3DPOOL pool)
  : m_device(device), m_length(length), m_usage(usage), m_fvf(fvf), m_pool(pool) {
  if (pool != D3DPOOL_DEFAULT || (usage & D3DUSAGE_DYNAMIC)) {
    m_data = calloc(1, length);
  }
  VKWIND_DBG(kTag, "Created vertex buffer: %u bytes, fvf=0x%x, pool=%d", length, fvf, pool);
}

D3D9VertexBuffer::~D3D9VertexBuffer() {
  if (m_data) free(m_data);
}

uint32_t D3D9VertexBuffer::AddRef() { return ++m_refCount; }
uint32_t D3D9VertexBuffer::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9VertexBuffer::QueryInterface(const void* iid, void** obj) { (void)iid; *obj = nullptr; return D3DERR_NOTAVAILABLE; }
void D3D9VertexBuffer::GetDevice(IDirect3DDevice9** ppDevice) { *ppDevice = m_device; if (*ppDevice) (*ppDevice)->AddRef(); }
void D3D9VertexBuffer::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9VertexBuffer::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9VertexBuffer::FreePrivateData(uint32_t refguid) {}
uint32_t D3D9VertexBuffer::SetPriority(uint32_t PriorityNew) { return 0; }
uint32_t D3D9VertexBuffer::GetPriority() { return 0; }
void D3D9VertexBuffer::PreLoad() {}
D3DRESOURCETYPE D3D9VertexBuffer::GetType() { return D3DRTYPE_VERTEXBUFFER; }

int D3D9VertexBuffer::Lock(uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) {
  if (m_locked) return D3DERR_INVALIDCALL;
  if (!m_data) m_data = calloc(1, m_length);

  char* ptr = static_cast<char*>(m_data) + OffsetToLock;
  *ppbData = ptr;
  m_locked = true;
  m_dirty = true;
  return D3D_OK;
}

int D3D9VertexBuffer::Unlock() {
  m_locked = false;
  m_dirty = true;
  return D3D_OK;
}

int D3D9VertexBuffer::GetDesc(void* pDesc) {
  auto* desc = static_cast<D3DVERTEXBUFFER_DESC*>(pDesc);
  desc->Format = D3DFMT_VERTEXDATA;
  desc->Type = D3DRTYPE_VERTEXBUFFER;
  desc->Usage = m_usage;
  desc->Pool = m_pool;
  desc->Size = m_length;
  desc->FVF = m_fvf;
  return D3D_OK;
}

void D3D9VertexBuffer::ensure_gpu_buffer(CommandBufferManager& cmdManager) {
  if (m_gpuBuffer && !m_dirty) return;

  if (!m_data) return;

  auto* vkDevice = m_device->vk_device();
  if (!vkDevice) return;

  if (!m_gpuBuffer) {
    m_gpuBuffer = std::make_unique<Buffer>(vkDevice, m_length,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  }

  cmdManager.upload_buffer(m_gpuBuffer->handle(), m_data, m_length);
  m_dirty = false;
}

// --- IndexBuffer ---

D3D9IndexBuffer::D3D9IndexBuffer(D3D9Device* device, uint32_t length, uint32_t usage, D3DFORMAT format, D3DPOOL pool)
  : m_device(device), m_length(length), m_usage(usage), m_format(format), m_pool(pool) {
  if (pool != D3DPOOL_DEFAULT || (usage & D3DUSAGE_DYNAMIC)) {
    m_data = calloc(1, length);
  }
  VKWIND_DBG(kTag, "Created index buffer: %u bytes, fmt=%d, pool=%d", length, format, pool);
}

D3D9IndexBuffer::~D3D9IndexBuffer() {
  if (m_data) free(m_data);
}

uint32_t D3D9IndexBuffer::AddRef() { return ++m_refCount; }
uint32_t D3D9IndexBuffer::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9IndexBuffer::QueryInterface(const void* iid, void** obj) { (void)iid; *obj = nullptr; return D3DERR_NOTAVAILABLE; }
void D3D9IndexBuffer::GetDevice(IDirect3DDevice9** ppDevice) { *ppDevice = m_device; if (*ppDevice) (*ppDevice)->AddRef(); }
void D3D9IndexBuffer::SetPrivateData(uint32_t refguid, const void* pData, uint32_t SizeOfData, uint32_t Flags) {}
void D3D9IndexBuffer::GetPrivateData(uint32_t refguid, void* pData, uint32_t* pSizeOfData) {}
void D3D9IndexBuffer::FreePrivateData(uint32_t refguid) {}
uint32_t D3D9IndexBuffer::SetPriority(uint32_t PriorityNew) { return 0; }
uint32_t D3D9IndexBuffer::GetPriority() { return 0; }
void D3D9IndexBuffer::PreLoad() {}
D3DRESOURCETYPE D3D9IndexBuffer::GetType() { return D3DRTYPE_INDEXBUFFER; }

int D3D9IndexBuffer::Lock(uint32_t OffsetToLock, uint32_t SizeToLock, void** ppbData, uint32_t Flags) {
  if (m_locked) return D3DERR_INVALIDCALL;
  if (!m_data) m_data = calloc(1, m_length);

  char* ptr = static_cast<char*>(m_data) + OffsetToLock;
  *ppbData = ptr;
  m_locked = true;
  m_dirty = true;
  return D3D_OK;
}

int D3D9IndexBuffer::Unlock() {
  m_locked = false;
  m_dirty = true;
  return D3D_OK;
}

int D3D9IndexBuffer::GetDesc(void* pDesc) {
  auto* desc = static_cast<D3DINDEXBUFFER_DESC*>(pDesc);
  desc->Format = m_format;
  desc->Type = D3DRTYPE_INDEXBUFFER;
  desc->Usage = m_usage;
  desc->Pool = m_pool;
  desc->Size = m_length;
  return D3D_OK;
}

void D3D9IndexBuffer::ensure_gpu_buffer(CommandBufferManager& cmdManager) {
  if (m_gpuBuffer && !m_dirty) return;

  if (!m_data) return;

  auto* vkDevice = m_device->vk_device();
  if (!vkDevice) return;

  if (!m_gpuBuffer) {
    m_gpuBuffer = std::make_unique<Buffer>(vkDevice, m_length,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  }

  cmdManager.upload_buffer(m_gpuBuffer->handle(), m_data, m_length);
  m_dirty = false;
}

} // namespace vkwind
