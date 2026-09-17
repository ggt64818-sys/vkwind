#pragma once

#include "d3d9_device.h"
#include <vector>

namespace vkwind {

class D3D9PixelShader : public IDirect3DPixelShader9 {
public:
  D3D9PixelShader(D3D9Device* device, const uint32_t* bytecode);
  ~D3D9PixelShader();

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

  int GetFunction(void* pFunction, uint32_t* pSizeOfData) override;

  const std::vector<uint32_t>& bytecode() const { return m_bytecode; }

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  std::vector<uint32_t> m_bytecode;
};

class D3D9VertexShader : public IDirect3DVertexShader9 {
public:
  D3D9VertexShader(D3D9Device* device, const uint32_t* bytecode);
  ~D3D9VertexShader();

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

  int GetFunction(void* pFunction, uint32_t* pSizeOfData) override;

  const std::vector<uint32_t>& bytecode() const { return m_bytecode; }

private:
  uint32_t m_refCount = 1;
  D3D9Device* m_device = nullptr;
  std::vector<uint32_t> m_bytecode;
};

} // namespace vkwind
