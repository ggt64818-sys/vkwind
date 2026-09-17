#include "d3d9.h"
#include "d3d9_device.h"
#include "../util/util_log.h"

#include <cstring>

namespace vkwind {

static const char* kTag = "D3D9";

D3D9::D3D9() {
  VKWIND_INFO(kTag, "Direct3D9 created");
}

D3D9::~D3D9() {
  VKWIND_INFO(kTag, "Direct3D9 destroyed");
}

uint32_t D3D9::AddRef() {
  static uint32_t s_refCount = 1;
  return ++s_refCount;
}

uint32_t D3D9::Release() {
  static uint32_t s_refCount = 1;
  uint32_t count = --s_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9::QueryInterface(const void* iid, void** obj) {
  (void)iid;
  *obj = nullptr;
  return D3DERR_NOTAVAILABLE;
}

int D3D9::RegisterSoftwareDevice(void* pInitializeFunction) {
  (void)pInitializeFunction;
  return D3D_OK;
}

uint32_t D3D9::GetAdapterCount() {
  return 1; // Single adapter
}

int D3D9::GetAdapterIdentifier(uint32_t Adapter, uint32_t Flags, void* pIdentifier) {
  (void)Adapter; (void)Flags;
  if (!pIdentifier) return D3DERR_INVALIDCALL;

  // Fill in minimal adapter identifier
  struct { uint32_t Size; uint32_t Version; char Driver[512]; char Description[512]; uint32_t VendorId; uint32_t DeviceId; uint32_t SubSysId; uint32_t Revision; }* id =
    static_cast<decltype(id)>(pIdentifier);
  memset(id, 0, sizeof(*id));
  id->Size = sizeof(*id);
  id->Version = 0x00010004;
  strncpy(id->Driver, "vkwind", sizeof(id->Driver) - 1);
  strncpy(id->Description, "Vulkan Wind D3D9", sizeof(id->Description) - 1);
  id->VendorId = 0x1002; // AMD (placeholder)
  id->DeviceId = 0x67DF;

  return D3D_OK;
}

uint32_t D3D9::GetAdapterModeCount(uint32_t Adapter, D3DFORMAT Format) {
  (void)Adapter; (void)Format;
  return 1; // One mode (current)
}

int D3D9::EnumAdapterModes(uint32_t Adapter, D3DFORMAT Format, uint32_t Mode, D3DDISPLAYMODE* pMode) {
  (void)Adapter; (void)Format; (void)Mode;
  if (!pMode) return D3DERR_INVALIDCALL;

  pMode->Width = 1920;
  pMode->Height = 1080;
  pMode->RefreshRate = 60;
  pMode->Format = D3DFMT_X8R8G8B8;
  return D3D_OK;
}

int D3D9::GetAdapterDisplayMode(uint32_t Adapter, D3DDISPLAYMODE* pMode) {
  (void)Adapter;
  if (!pMode) return D3DERR_INVALIDCALL;

  pMode->Width = 1920;
  pMode->Height = 1080;
  pMode->RefreshRate = 60;
  pMode->Format = D3DFMT_X8R8G8B8;
  return D3D_OK;
}

int D3D9::CheckDeviceType(uint32_t Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, bool Windowed) {
  (void)Adapter; (void)CheckType; (void)DisplayFormat; (void)BackBufferFormat; (void)Windowed;
  return D3D_OK; // Accept everything
}

int D3D9::CheckDeviceFormat(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, uint32_t Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) {
  (void)Adapter; (void)DeviceType; (void)AdapterFormat; (void)Usage; (void)RType; (void)CheckFormat;
  // TODO: Check actual Vulkan format support
  return D3D_OK;
}

int D3D9::CheckDeviceMultiSampleType(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, bool Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, uint32_t* pQualityLevels) {
  (void)Adapter; (void)DeviceType; (void)SurfaceFormat; (void)Windowed;
  if (MultiSampleType == D3DMULTISAMPLE_NONE) {
    if (pQualityLevels) *pQualityLevels = 1;
    return D3D_OK;
  }
  if (pQualityLevels) *pQualityLevels = 0;
  return D3DERR_NOTAVAILABLE;
}

int D3D9::CheckDepthStencilMatch(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) {
  (void)Adapter; (void)DeviceType; (void)AdapterFormat; (void)RenderTargetFormat; (void)DepthStencilFormat;
  return D3D_OK;
}

int D3D9::CheckDeviceFormatConversion(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) {
  (void)Adapter; (void)DeviceType; (void)SourceFormat; (void)TargetFormat;
  return D3D_OK;
}

int D3D9::GetDeviceCaps(uint32_t Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) {
  if (!pCaps) return D3DERR_INVALIDCALL;
  memset(pCaps, 0, sizeof(D3DCAPS9));

  pCaps->MaxTextureWidth = 16384;
  pCaps->MaxTextureHeight = 16384;
  pCaps->MaxVolumeExtent = 2048;
  pCaps->MaxTextureRepeat = 8192;
  pCaps->MaxTextureAspectRatio = 8192;
  pCaps->MaxAnisotropy = 16;
  pCaps->MaxVertexW = 1e10f;
  pCaps->GuardBandLeft = -16384.0f;
  pCaps->GuardBandTop = -16384.0f;
  pCaps->GuardBandRight = 16384.0f;
  pCaps->GuardBandBottom = 16384.0f;
  pCaps->MaxPointSize = 256.0f;
  pCaps->MaxPrimitiveCount = 0x00ffffff;
  pCaps->MaxVertexIndex = 0x00ffffff;
  pCaps->MaxStreams = 16;
  pCaps->MaxVertexStride = 256;
  pCaps->VertexShaderVersion = 0xfffe0300;
  pCaps->PixelShaderVersion = 0xfffe0300;
  pCaps->MaxVertexShaderConst = 256;
  pCaps->MaxPixelShaderValue = 65504.0f;
  pCaps->MaxTextureBlendStages = 8;
  pCaps->MaxSimultaneousTextures = 8;
  pCaps->MaxActiveLights = 8;
  pCaps->MaxUserClipPlanes = 6;
  pCaps->MaxVertexBlendMatrices = 4;
  pCaps->StencilCaps = 0x00001ff0;
  pCaps->FVFCaps = 0x001fffff;
  pCaps->TextureOpCaps = 0x00001edf;
  pCaps->VertexProcessingCaps = 0x0000003b;

  return D3D_OK;
}

int D3D9::GetAdapterMonitor(uint32_t Adapter) {
  (void)Adapter;
  return 0;
}

int D3D9::CreateDevice(uint32_t Adapter, D3DDEVTYPE DeviceType, void* hFocusWindow,
                        uint32_t BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
                        IDirect3DDevice9** ppReturnedDeviceInterface) {
  VKWIND_INFO(kTag, "CreateDevice: adapter=%u, type=%d, window=%p, flags=0x%x",
    Adapter, DeviceType, hFocusWindow, BehaviorFlags);

  if (!pPresentationParameters || !ppReturnedDeviceInterface) {
    return D3DERR_INVALIDCALL;
  }

  // Apply config overrides
  auto& config = Config::instance();

  // Create device
  auto* device = new D3D9Device(this, pPresentationParameters);
  *ppReturnedDeviceInterface = device;

  return D3D_OK;
}

} // namespace vkwind
