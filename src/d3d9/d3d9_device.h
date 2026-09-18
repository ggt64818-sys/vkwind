#pragma once

#include "d3d9_types.h"
#include "../vulkan/vk_instance.h"
#include "../vulkan/vk_device.h"
#include "../vulkan/vk_swapchain.h"
#include "../vulkan/vk_pipeline.h"
#include "../vulkan/vk_memory.h"
#include "../vulkan/vk_cmd_buffer.h"
#include "d3d9_state_mapper.h"
#include "d3d9_fvf.h"
#include "d3d9_texture_manager.h"
#include "d3d9_vertex_declaration.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>

namespace vkwind {

class D3D9StateBlock;

class D3D9Device : public IDirect3DDevice9 {
  friend class D3D9StateBlock;
public:
  D3D9Device(IDirect3D9* d3d, const D3DPRESENT_PARAMETERS* params);
  ~D3D9Device();

  // IUnknown
  uint32_t AddRef() override;
  uint32_t Release() override;
  int QueryInterface(const void* iid, void** obj) override;

  // Device management
  int TestCooperativeLevel() override;
  int GetAvailablePoolMem(uint32_t Usage) override;
  int EvictManagedResources() override;
  int GetDirect3D(IDirect3D9** ppD3D9) override;
  int GetDeviceCaps(D3DCAPS9* pCaps) override;
  int GetDisplayMode(uint32_t iSwapChain, D3DDISPLAYMODE* pMode) override;
  int GetCreationParameters(void* pParameters) override;
  int SetCursorProperties(uint32_t XHotSpot, uint32_t YHotSpot, IDirect3DSurface9* pCursorBitmap) override;
  void SetCursorPosition(int X, int Y, uint32_t Flags) override;
  int ShowCursor(bool bShow) override;
  int CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override;
  int GetSwapChain(uint32_t iSwapChain, IDirect3DSwapChain9** pSwapChain) override;
  uint32_t GetNumberOfSwapChains() override;
  int Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override;
  int Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, const void* pDirtyRegion) override;
  int GetBackBuffer(uint32_t iSwapChain, uint32_t iBackBuffer, uint32_t Type, IDirect3DSurface9** ppBackBuffer) override;
  int GetRasterStatus(uint32_t iSwapChain, void* pRasterStatus) override;
  int SetDialogBoxMode(bool bEnableDialogs) override;
  void SetGammaRamp(uint32_t iSwapChain, uint32_t Flags, const void* pRamp) override;
  void GetGammaRamp(uint32_t iSwapChain, void* pRamp) override;

  // Textures
  int CreateTexture(uint32_t Width, uint32_t Height, uint32_t Levels, uint32_t Usage,
                    D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, void* pSharedHandle) override;
  int CreateVolumeTexture(uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t Levels,
                          uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                          IDirect3DVolumeTexture9** ppVolumeTexture, void* pSharedHandle) override;
  int CreateCubeTexture(uint32_t EdgeLength, uint32_t Levels, uint32_t Usage,
                        D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, void* pSharedHandle) override;

  // Buffers
  int CreateVertexBuffer(uint32_t Length, uint32_t Usage, uint32_t FVF, D3DPOOL Pool,
                         IDirect3DVertexBuffer9** ppVertexBuffer, void* pSharedHandle) override;
  int CreateIndexBuffer(uint32_t Length, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                        IDirect3DIndexBuffer9** ppIndexBuffer, void* pSharedHandle) override;

  // Surfaces
  int CreateRenderTarget(uint32_t Width, uint32_t Height, D3DFORMAT Format,
                         D3DMULTISAMPLE_TYPE MultiSample, uint32_t MultisampleQuality,
                         bool Lockable, IDirect3DSurface9** ppSurface, void* pSharedHandle) override;
  int CreateDepthStencilSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format,
                                D3DMULTISAMPLE_TYPE MultiSample, uint32_t MultisampleQuality,
                                bool Discard, IDirect3DSurface9** ppSurface, void* pSharedHandle) override;
  int CreateImageSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format, IDirect3DSurface9** ppSurface) override;
  int CopyRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
               IDirect3DSurface9* pDestSurface, void* pDestPoint) override;
  int UpdateSurface(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                    IDirect3DSurface9* pDestSurface, void* pDestPoint) override;
  int UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override;
  int GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override;
  int GetFrontBufferData(uint32_t iSwapChain, IDirect3DSurface9* pDestSurface) override;
  int StretchRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                  IDirect3DSurface9* pDestSurface, const void* pDestRect, uint32_t Filter) override;
  int ColorFill(IDirect3DSurface9* pSurface, const void* pRect, uint32_t Color) override;
  int CreateOffscreenPlainSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format,
                                  D3DPOOL Pool, IDirect3DSurface9** ppSurface, void* pSharedHandle) override;

  // Render targets
  void SetRenderTarget(uint32_t RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override;
  void GetRenderTarget(uint32_t RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override;
  void SetDepthStencilSurface(IDirect3DSurface9* pZStencilSurface) override;
  void GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override;

  // Scene
  int BeginScene() override;
  int EndScene() override;
  int Clear(uint32_t Count, const void* pRects, uint32_t Flags, uint32_t Color, float Z, uint32_t Stencil) override;

  // Transforms
  int SetTransform(uint32_t State, const D3DMATRIX* pMatrix) override;
  int GetTransform(uint32_t State, D3DMATRIX* pMatrix) override;
  int MultiplyTransform(uint32_t State, const D3DMATRIX* pMatrix) override;

  // Viewport
  void SetViewport(const D3DVIEWPORT9* pViewport) override;
  void GetViewport(D3DVIEWPORT9* pViewport) override;

  // Material
  void SetMaterial(const D3DMATERIAL9* pMaterial) override;
  void GetMaterial(D3DMATERIAL9* pMaterial) override;

  // Lights
  void SetLight(uint32_t Index, const D3DLIGHT9* pLight) override;
  void GetLight(uint32_t Index, D3DLIGHT9* pLight) override;
  int LightEnable(uint32_t Index, bool Enable) override;
  void GetLightEnable(uint32_t Index, bool* pEnable) override;

  // Clip planes
  int SetClipPlane(uint32_t Index, const float* pPlane) override;
  void GetClipPlane(uint32_t Index, float* pPlane) override;

  // Render state
  int SetRenderState(uint32_t State, uint32_t Value) override;
  int GetRenderState(uint32_t State, uint32_t* pValue) override;

  // State blocks
  int BeginStateBlock() override;
  int EndStateBlock(uint32_t* pToken) override;

  // Clip status
  int CreateClipStatus(void** ppClipStatus) override;
  int GetClipStatus(void* pClipStatus) override;

  // Textures
  int GetTexture(uint32_t Stage, IDirect3DBaseTexture9** ppTexture) override;
  int SetTexture(uint32_t Stage, IDirect3DBaseTexture9* pTexture) override;
  int GetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t* pValue) override;
  int SetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t Value) override;
  int GetSamplerState(uint32_t Sampler, uint32_t Type, uint32_t* pValue) override;
  int SetSamplerState(uint32_t Sampler, uint32_t Type, uint32_t Value) override;
  int ValidateDevice(uint32_t* pNumPasses) override;

  // Palette
  int SetPaletteEntries(uint32_t PaletteNumber, const void* pEntries) override;
  int GetPaletteEntries(uint32_t PaletteNumber, void* pEntries) override;
  int SetCurrentTexturePalette(uint32_t PaletteNumber) override;
  int GetCurrentTexturePalette(uint32_t* PaletteNumber) override;

  // Scissor
  int SetScissorRect(const void* pRect) override;
  void GetScissorRect(void* pRect) override;

  // Software vertex processing
  int SetSoftwareVertexProcessing(bool bSoftware) override;
  bool GetSoftwareVertexProcessing() override;

  // NPatch
  int SetNPatchMode(float nSegments) override;
  float GetNPatchMode() override;

  // Drawing
  int DrawPrimitive(uint32_t PrimitiveType, uint32_t StartVertex, uint32_t PrimitiveCount) override;
  int DrawIndexedPrimitive(uint32_t PrimitiveType, int BaseVertexIndex, uint32_t MinVertexIndex,
                           uint32_t NumVertexIndices, uint32_t StartIndex, uint32_t PrimitiveCount) override;
  int DrawPrimitiveUP(uint32_t PrimitiveType, uint32_t PrimitiveCount,
                      const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) override;
  int DrawIndexedPrimitiveUP(uint32_t PrimitiveType, uint32_t MinVertexIndex, uint32_t NumVertexIndices,
                             uint32_t PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat,
                             const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) override;

  // Process vertices
  int ProcessVertices(uint32_t SrcStartIndex, uint32_t DestIndex, uint32_t VertexCount,
                      IDirect3DVertexBuffer9* pDestBuffer, void* pVertexDecl, uint32_t Flags) override;

  // Vertex shaders
  int CreateVertexShader(const uint32_t* pDeclaration, const uint32_t* pFunction, void** ppVertexShader, uint32_t Flags) override;
  int SetVertexShader(void* pShader) override;
  void* GetVertexShader() override;
  int SetVertexShaderConstant(uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) override;
  int GetVertexShaderConstant(uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) override;
  int SetVertexShaderDecl(void* pDecl) override;
  int SetVertexShaderFunction(const uint32_t* pFunction) override;

  // Vertex format
  int SetFVF(uint32_t FVF) override;
  int GetFVF(uint32_t* pFVF) override;

  int CreateVertexDeclaration(const D3DVERTEXELEMENT9* pVertexElements, void** ppDecl) override;
  int SetVertexDeclaration(void* pDecl) override;
  void GetVertexDeclaration(void** ppDecl) override;

  // Stream source
  int SetStreamSource(uint32_t StreamNumber, IDirect3DVertexBuffer9* pStreamData,
                      uint32_t OffsetInBytes, uint32_t Stride) override;
  void GetStreamSource(uint32_t StreamNumber, IDirect3DVertexBuffer9** ppStreamData,
                       uint32_t* pOffsetInBytes, uint32_t* pStride) override;
  int SetStreamSourceFreq(uint32_t StreamNumber, uint32_t Setting) override;
  void GetStreamSourceFreq(uint32_t StreamNumber, uint32_t* pSetting) override;

  // Index buffer
  int SetIndices(IDirect3DIndexBuffer9* pIndexData) override;
  void GetIndices(IDirect3DIndexBuffer9** ppIndexData) override;

  // Pixel shaders
  int CreatePixelShader(const uint32_t* pFunction, IDirect3DPixelShader9** ppShader) override;
  int SetPixelShader(IDirect3DPixelShader9* pShader) override;
  IDirect3DPixelShader9* GetPixelShader() override;
  int SetPixelShaderConstant(uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) override;
  int GetPixelShaderConstant(uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) override;
  int SetPixelShaderFunction(const uint32_t* pFunction) override;

  // Patches
  int DrawRectPatch(uint32_t Handle, const float* pNumSegs, const void* pRectPatchInfo) override;
  int DrawTriPatch(uint32_t Handle, const float* pNumSegs, const void* pTriPatchInfo) override;
  int DeletePatch(uint32_t Handle) override;

  // Query
  int CreateQuery(uint32_t Type, IDirect3DQuery9** ppQuery) override;

  // Internal access
  IDirect3D9* GetD3D() const { return m_d3d; }
  VkDevice* vk_device() const { return m_vkDevice.get(); }
  VkSwapchain* vk_swapchain() const { return m_vkSwapchain.get(); }
  CommandBufferManager* cmd_manager() const { return m_cmdManager.get(); }
  D3D9TextureManager* texture_manager() const { return m_textureManager.get(); }

private:
  void apply_render_state(uint32_t state, uint32_t value);
  void apply_viewport_scissor();
  void push_mvp_constants();
  void setup_default_states();
  void create_descriptor_resources();
  void destroy_descriptor_resources();
  void update_texture_descriptors();
  ::VkPipeline create_or_get_pipeline();
  void ensure_render_pass_active();
  void end_active_render_pass();

  uint32_t m_refCount = 1;
  IDirect3D9* m_d3d = nullptr;
  uint32_t m_adapter = 0;
  D3DDEVTYPE m_deviceType = D3DDEVTYPE_HAL;
  D3DPRESENT_PARAMETERS m_presParams = {};

  // Vulkan objects
  std::unique_ptr<vkwind::VkInstance> m_vkInstance;
  std::unique_ptr<vkwind::VkDevice> m_vkDevice;
  std::unique_ptr<vkwind::VkSwapchain> m_vkSwapchain;
  std::unique_ptr<vkwind::VkPipeline> m_vkPipeline;
  std::unique_ptr<vkwind::VkMemory> m_vkMemory;
  std::unique_ptr<vkwind::CommandBufferManager> m_cmdManager;
  std::unique_ptr<vkwind::D3D9TextureManager> m_textureManager;

  // Thread safety
  mutable std::mutex m_mutex;

  // Lights
  struct LightState {
    D3DLIGHT9 light = {};
    bool enabled = false;
    bool defined = false;
  };
  LightState m_lights[8] = {};

  // Clip planes
  float m_clipPlanes[6][4] = {};

  // Device state
  bool m_deviceLost = false;

  // Current pipeline state
  PipelineState m_pipelineState = {};
  FVFDescription m_currentFVF = {};

  // State
  bool m_inScene = false;
  D3DMATRIX m_transforms[300] = {};
  uint32_t m_renderStates[256] = {};
  D3DMATERIAL9 m_material = {};
  D3DVIEWPORT9 m_viewport = {};
  struct ScissorRect { int32_t left, top, right, bottom; };
  ScissorRect m_scissor = {};
  bool m_scissorEnabled = false;

  // Bound resources
  struct StreamSource {
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t stride = 0;
  };
  StreamSource m_streamSources[16] = {};
  uint32_t m_fvf = 0;
  D3D9VertexDeclaration* m_vertexDecl = nullptr;
  IDirect3DIndexBuffer9* m_indexBuffer = nullptr;
  IDirect3DBaseTexture9* m_textures[8] = {};
  bool m_texturesDirty = false;
  IDirect3DSurface9* m_renderTargets[4] = {};
  IDirect3DSurface9* m_depthStencilSurface = nullptr;
  IDirect3DPixelShader9* m_pixelShader = nullptr;
  IDirect3DVertexShader9* m_vertexShader = nullptr;
  void* m_vertexShaderDecl = nullptr;

  // Render pass state
  uint32_t m_currentImageIndex = 0;
  bool m_renderPassActive = false;
  VkCommandBuffer m_activeRenderCmd = VK_NULL_HANDLE;
  bool m_clearPending = false;
  float m_clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float m_clearDepth = 1.0f;
  uint32_t m_clearStencil = 0;
  uint32_t m_clearFlags = 0;

  // Pipeline cache
  ::VkPipeline m_currentPipeline = VK_NULL_HANDLE;
  bool m_pipelineDirty = true;
  ::VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
  struct PipelineKey {
    void* vs; void* ps; uint32_t fvf; uint32_t renderStates[256];
    bool operator==(const PipelineKey& o) const;
  };
  struct PipelineKeyHash { size_t operator()(const PipelineKey& k) const; };
  std::unordered_map<PipelineKey, ::VkPipeline, PipelineKeyHash> m_pipelineCacheMap;

  // Async pipeline compilation — eliminates first-time compilation hitches
  struct AsyncPipelineJob {
    PipelineKey key;
    PipelineState state;
    VkRenderPass renderPass;
  };
  std::thread m_pipelineWorker;
  std::mutex m_asyncMutex;
  std::queue<AsyncPipelineJob> m_asyncQueue;
  std::unordered_map<PipelineKey, ::VkPipeline, PipelineKeyHash> m_asyncResults;
  std::atomic<bool> m_workerRunning{false};
  bool m_asyncPipelineEnabled = true;  // VKWIND_ASYNC_PIPELINE env
  void pipeline_worker_thread();
  ::VkPipeline create_or_get_pipeline_async();

  // Frame-temporary buffers (survive until frame submit)
  std::vector<std::unique_ptr<Buffer>> m_frameTempBuffers;

  // Ring buffer for DrawPrimitiveUP / DrawIndexedPrimitiveUP vertex+index data
  // Avoids per-draw vkCreateBuffer + vkAllocateMemory
  std::unique_ptr<Buffer> m_upVertexBuffer;      // HOST_VISIBLE vertex ring
  std::unique_ptr<Buffer> m_upIndexBuffer;        // HOST_VISIBLE index ring
  VkDeviceSize m_upVertexWriteOffset = 0;
  VkDeviceSize m_upIndexWriteOffset = 0;
  static constexpr VkDeviceSize kUpBufferSize = 1024 * 1024; // 1MB each
  static constexpr VkDeviceSize kUpAlign = 256;

  // Pending draw calls (replayed inside render pass in Present)
  struct PendingDrawUP {
    ::VkPipeline pipeline = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> vertexBuffer;
    VkBuffer vertexBufferDirect = VK_NULL_HANDLE; // Ring buffer handle (if using ring)
    VkDeviceSize vertexBufferOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t firstVertex = 0;
    // Index buffer (for DrawIndexedPrimitiveUP)
    VkBuffer indexBufferDirect = VK_NULL_HANDLE;
    VkDeviceSize indexBufferOffset = 0;
    uint32_t indexCount = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;
    bool hasIndexBuffer = false;
  };
  // Staging: DrawPrimitiveUP stores here before Present's begin_frame clears old data.
  std::vector<PendingDrawUP> m_pendingDraws;
  // Per-frame: after begin_frame, staging is moved here for replay and GPU lifetime.
  // Cleared 2 frames later when fence is waited in begin_frame.
  static constexpr uint32_t kMaxFrames = 2;
  std::vector<PendingDrawUP> m_frameDraws[kMaxFrames];
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

  // Descriptor resources
  VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_descPool = VK_NULL_HANDLE;
  VkDescriptorSet m_descSets[8] = {};
  VkSampler m_defaultSampler = VK_NULL_HANDLE;
  VkSampler m_samplers[8] = {};
  struct SamplerState {
    uint32_t magFilter = 2; // D3DTEXF_LINEAR
    uint32_t minFilter = 2; // D3DTEXF_LINEAR
    uint32_t mipFilter = 2; // D3DTEXF_LINEAR
    uint32_t addressU = 1;  // D3DTADDRESS_WRAP
    uint32_t addressV = 1;
    uint32_t addressW = 1;
    uint32_t maxMipLevel = 0;
    uint32_t maxAnisotropy = 1;
  };
  SamplerState m_samplerStates[8] = {};
  struct SamplerCacheKey {
    uint32_t magFilter, minFilter, mipFilter, addressU, addressV, addressW, maxAnisotropy;
    bool operator==(const SamplerCacheKey& o) const;
  };
  struct SamplerCacheKeyHash { size_t operator()(const SamplerCacheKey& k) const; };
  std::unordered_map<SamplerCacheKey, VkSampler, SamplerCacheKeyHash> m_samplerCache;
  bool m_samplerDirty[8] = {};

  // Persistent semaphores (created once, reused every frame)
  VkSemaphore m_acquireSemaphore = VK_NULL_HANDLE;
  VkSemaphore m_presentSemaphore = VK_NULL_HANDLE;

  // Transform dirty tracking (avoid redundant MVP recomputation)
  bool m_transformDirty = true;
  float m_lastMVP[16] = {};
  uint32_t m_lastWorldTS = 0;
  uint32_t m_lastViewTS = 0;
  uint32_t m_lastProjTS = 0;
  uint32_t m_curWorldTS = 0;
  uint32_t m_curViewTS = 0;
  uint32_t m_curProjTS = 0;
  float m_lastAlphaPC[8] = {};

  // Texture stage states (D3DTSS_COLOROP, D3DTSS_ALPHAOP, etc.)
  uint32_t m_textureStageStates[8][32] = {};

  // Per-shader compiled SPIR-V
  struct CompiledShaderModule {
    VkShaderModule module = VK_NULL_HANDLE;
    std::vector<uint32_t> spirv;
  };
  CompiledShaderModule m_compiledVS;
  CompiledShaderModule m_compiledPS;
  bool m_shadersDirty = false;

  ::VkShaderModule m_fallbackVS = VK_NULL_HANDLE;
  ::VkShaderModule m_fallbackFS = VK_NULL_HANDLE;

  // UBO for MVP matrix
  ::VkBuffer m_uboBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_uboMemory = VK_NULL_HANDLE;
  void* m_uboMapped = nullptr;

  // UBO for VS/PS shader constants (D3D9 c0-c255)
  static constexpr uint32_t kConstantsUBOSize = 256 * 4 * sizeof(float); // 4096 bytes per stage
  ::VkBuffer m_vsConstBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_vsConstMemory = VK_NULL_HANDLE;
  void* m_vsConstMapped = nullptr;
  ::VkBuffer m_psConstBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_psConstMemory = VK_NULL_HANDLE;
  void* m_psConstMapped = nullptr;

  // Alpha test state (push constants)
  float m_alphaRef = 0.0f;
  int32_t m_alphaFunc = 0; // 0=disabled, 1=NEVER..8=ALWAYS (D3DCMP values)

  // Shader constant registers (D3D9 float c0-c255, int i0-i15, bool b0-b15)
  static constexpr uint32_t kMaxFloatConstants = 256;
  static constexpr uint32_t kMaxIntConstants = 16;
  static constexpr uint32_t kMaxBoolConstants = 16;
  float m_vsFloatConstants[kMaxFloatConstants * 4] = {};
  float m_psFloatConstants[kMaxFloatConstants * 4] = {};
  int32_t m_vsIntConstants[kMaxIntConstants * 4] = {};
  int32_t m_psIntConstants[kMaxIntConstants * 4] = {};
  int32_t m_vsBoolConstants[kMaxBoolConstants] = {};
  int32_t m_psBoolConstants[kMaxBoolConstants] = {};
  bool m_vsConstantsDirty = false;
  bool m_psConstantsDirty = false;

  void create_ubo_resource();
  void destroy_ubo_resource();
  void upload_ubo_mvp();
  void upload_shader_constants();
};

} // namespace vkwind
