#pragma once

#include "d3d9/d3d9_device.h"
#include "util/util_log.h"
#include "util/util_config.h"
#include <cstdint>

namespace vkwind {

// IDirect3D9 implementation
class D3D9 : public IDirect3D9 {
public:
  D3D9();
  ~D3D9();

  // IUnknown
  uint32_t AddRef() override;
  uint32_t Release() override;
  int QueryInterface(const void* iid, void** obj) override;

  // IDirect3D9 methods
  int RegisterSoftwareDevice(void* pInitializeFunction) override;
  uint32_t GetAdapterCount() override;
  int GetAdapterIdentifier(uint32_t Adapter, uint32_t Flags, void* pIdentifier) override;
  uint32_t GetAdapterModeCount(uint32_t Adapter, D3DFORMAT Format) override;
  int EnumAdapterModes(uint32_t Adapter, D3DFORMAT Format, uint32_t Mode, D3DDISPLAYMODE* pMode) override;
  int GetAdapterDisplayMode(uint32_t Adapter, D3DDISPLAYMODE* pMode) override;
  int CheckDeviceType(uint32_t Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, bool Windowed) override;
  int CheckDeviceFormat(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, uint32_t Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override;
  int CheckDeviceMultiSampleType(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, bool Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, uint32_t* pQualityLevels) override;
  int CheckDepthStencilMatch(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override;
  int CheckDeviceFormatConversion(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) override;
  int GetDeviceCaps(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) override;
  int GetAdapterMonitor(uint32_t Adapter) override;
  int CreateDevice(uint32_t Adapter, D3DDEVTYPE DeviceType, void* hFocusWindow, uint32_t BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) override;
};

} // namespace vkwind
