#include "d3d9_device.h"
#include "d3d9_texture.h"
#include "d3d9_surface.h"
#include "d3d9_vertex_buffer.h"
#include "d3d9_shader.h"
#include "d3d9_state.h"
#include "d3d9_volume_texture.h"
#include "d3d9_cube_texture.h"
#include "../shader/shader_translator.h"
#include "../shader/d3d9_sm3_translator.h"
#include "../shader/d3d9_fixed_function.h"
#include "../shader/default_shaders.inc"
#include "../util/util_log.h"
#include <cstring>

namespace vkwind {

static const char* kTag = "D3D9Device";

bool D3D9Device::PipelineKey::operator==(const PipelineKey& o) const {
  if (vs != o.vs || ps != o.ps || fvf != o.fvf) return false;
  return memcmp(renderStates, o.renderStates, sizeof(renderStates)) == 0;
}

size_t D3D9Device::PipelineKeyHash::operator()(const PipelineKey& k) const {
  size_t h = std::hash<void*>()(k.vs) ^ (std::hash<void*>()(k.ps) << 1);
  h ^= std::hash<uint32_t>()(k.fvf) << 2;
  // Hash a subset of render states for speed (16 key states)
  static const uint32_t keyStates[] = {
    7, 8, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 34, 137, 171
  };
  for (uint32_t rs : keyStates) {
    h ^= std::hash<uint32_t>()(k.renderStates[rs & 0xFF]) << (rs & 7);
  }
  return h;
}

bool D3D9Device::SamplerCacheKey::operator==(const SamplerCacheKey& o) const {
  return magFilter == o.magFilter && minFilter == o.minFilter && mipFilter == o.mipFilter
    && addressU == o.addressU && addressV == o.addressV && addressW == o.addressW
    && maxAnisotropy == o.maxAnisotropy;
}

size_t D3D9Device::SamplerCacheKeyHash::operator()(const SamplerCacheKey& k) const {
  size_t h = std::hash<uint32_t>()(k.magFilter);
  h ^= std::hash<uint32_t>()(k.minFilter) << 1;
  h ^= std::hash<uint32_t>()(k.mipFilter) << 2;
  h ^= std::hash<uint32_t>()(k.addressU) << 3;
  h ^= std::hash<uint32_t>()(k.addressV) << 4;
  h ^= std::hash<uint32_t>()(k.addressW) << 5;
  h ^= std::hash<uint32_t>()(k.maxAnisotropy) << 6;
  return h;
}

static void fvf_to_vk_attributes(uint32_t fvf, uint32_t stride,
  std::vector<VkVertexInputBindingDescription>& bindings,
  std::vector<VkVertexInputAttributeDescription>& attributes);

static uint32_t vertex_count_per_primitive(uint32_t primitiveType) {
  switch (primitiveType) {
    case 1: return 1;
    case 2: return 2;
    case 3: return 1;
    case 4: return 3;
    case 5: return 1;
    case 6: return 1;
    default: return 3;
  }
}

static uint32_t index_count_for_prims(uint32_t primitiveType, uint32_t primCount) {
  switch (primitiveType) {
    case 1: return primCount;
    case 2: return primCount * 2;
    case 3: return primCount + 1;
    case 4: return primCount * 3;
    case 5: return primCount + 2;
    case 6: return primCount + 2;
    default: return primCount * 3;
  }
}

// --- Constructor / Destructor ---

D3D9Device::D3D9Device(IDirect3D9* d3d, const D3DPRESENT_PARAMETERS* params)
  : m_d3d(d3d), m_presParams(*params) {
  VKWIND_INFO(kTag, "Creating D3D9 device (window=%p, %ux%u, fmt=%d, windowed=%d)",
    params->hDeviceWindow, params->BackBufferWidth, params->BackBufferHeight,
    params->BackBufferFormat, params->Windowed);

  for (auto& m : m_transforms) {
    memset(&m, 0, sizeof(D3DMATRIX));
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
  }

  m_renderStates[D3DRS_ZENABLE] = 1;
  m_renderStates[D3DRS_ZWRITEENABLE] = 1;
  m_renderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
  m_renderStates[D3DRS_FILLMODE] = D3DFILL_SOLID;
  m_renderStates[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
  m_renderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
  m_renderStates[D3DRS_ALPHABLENDENABLE] = 0;
  m_renderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
  m_renderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
  m_renderStates[D3DRS_LIGHTING] = 1;
  m_renderStates[D3DRS_STENCILENABLE] = 0;
  m_renderStates[D3DRS_DITHERENABLE] = 1;
  m_renderStates[D3DRS_SPECULARENABLE] = 0;
  m_renderStates[D3DRS_COLORWRITEENABLE] = 0x0F;
  m_renderStates[D3DRS_ZVISIBLE] = 0;

  VKWIND_INFO(kTag, "Step 1: Creating VkInstance...");
  try {
    m_vkInstance = std::make_unique<vkwind::VkInstance>();
  } catch (...) {
    VKWIND_ERR(kTag, "CRASH in VkInstance constructor!");
    throw;
  }

  auto gpu = m_vkInstance->select_gpu(0);
  if (gpu) {
    VKWIND_INFO(kTag, "Selected GPU: %s", gpu->properties.deviceName);

    VKWIND_INFO(kTag, "Step 2: Creating VkDevice...");
    m_vkDevice = std::make_unique<vkwind::VkDevice>(m_vkInstance.get(), gpu);

    VKWIND_INFO(kTag, "Step 3: Creating VkPipeline...");
    m_vkPipeline = std::make_unique<vkwind::VkPipeline>(m_vkDevice.get());

    VKWIND_INFO(kTag, "Step 4: Creating VkMemory...");
    m_vkMemory = std::make_unique<vkwind::VkMemory>(m_vkDevice.get());

    VKWIND_INFO(kTag, "Step 5: Creating CommandBufferManager...");
    m_cmdManager = std::make_unique<vkwind::CommandBufferManager>(m_vkDevice.get());

    // Create persistent ring buffers for DrawPrimitiveUP / DrawIndexedPrimitiveUP
    m_upVertexBuffer = std::make_unique<vkwind::Buffer>(m_vkDevice.get(), kUpBufferSize,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_upIndexBuffer = std::make_unique<vkwind::Buffer>(m_vkDevice.get(), kUpBufferSize,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VKWIND_INFO(kTag, "Step 5b: UP ring buffers created (%llu KB each)",
      (unsigned long long)(kUpBufferSize / 1024));

    VKWIND_INFO(kTag, "Step 6: Creating D3D9TextureManager...");
    m_textureManager = std::make_unique<vkwind::D3D9TextureManager>(*m_vkDevice, *m_cmdManager);

    VKWIND_INFO(kTag, "Step 7: Creating surface...");
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (params->hDeviceWindow) {
      VkResult surfResult = m_vkInstance->create_surface(params->hDeviceWindow, &surface);
      VKWIND_INFO(kTag, "Surface creation result: %d, surface=%p", surfResult, (void*)surface);
    } else {
      VKWIND_ERR(kTag, "No hDeviceWindow provided!");
    }

    VKWIND_INFO(kTag, "Step 8: Creating swapchain...");
    if (surface) {
      SwapchainConfig sc;
      sc.width = params->BackBufferWidth;
      sc.height = params->BackBufferHeight;
      sc.vsync = params->PresentationInterval != 0;
      VKWIND_INFO(kTag, "Creating swapchain: %ux%u, vsync=%d", sc.width, sc.height, sc.vsync);
      m_vkSwapchain = std::make_unique<vkwind::VkSwapchain>(m_vkDevice.get(), surface, sc);
    }

    VKWIND_INFO(kTag, "Step 9: Creating descriptor resources...");
    create_descriptor_resources();

    VKWIND_INFO(kTag, "Step 10: Setting up default states...");

    // Create persistent semaphores (reused every frame)
    m_acquireSemaphore = m_vkDevice->create_semaphore();
    m_presentSemaphore = m_vkDevice->create_semaphore();

    // Create pipeline cache for faster pipeline creation
    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(m_vkDevice->raw(), &cacheInfo, nullptr, &m_pipelineCache);

    // Start async pipeline compilation worker (eliminates first-time hitches)
    m_asyncPipelineEnabled = (getenv("VKWIND_ASYNC_PIPELINE") == nullptr ||
                              strcmp(getenv("VKWIND_ASYNC_PIPELINE"), "0") != 0);
    if (m_asyncPipelineEnabled) {
      m_workerRunning = true;
      m_pipelineWorker = std::thread(&D3D9Device::pipeline_worker_thread, this);
      VKWIND_INFO(kTag, "Async pipeline worker started");
    }
  } else {
    VKWIND_ERR(kTag, "No GPU selected!");
  }

  setup_default_states();
}

D3D9Device::~D3D9Device() {
  VKWIND_INFO(kTag, "Destroying D3D9 device");

  // Stop async pipeline worker
  if (m_pipelineWorker.joinable()) {
    m_workerRunning = false;
    {
      std::lock_guard<std::mutex> lock(m_asyncMutex);
      // Wake worker by pushing empty job
    }
    m_pipelineWorker.join();
    VKWIND_INFO(kTag, "Async pipeline worker stopped");
  }

  // Destroy async pipeline results
  {
    std::lock_guard<std::mutex> lock(m_asyncMutex);
    for (auto& [key, pipeline] : m_asyncResults) {
      if (pipeline) vkDestroyPipeline(m_vkDevice->raw(), pipeline, nullptr);
    }
    m_asyncResults.clear();
  }

  if (m_vkDevice) m_vkDevice->wait_idle();

  destroy_descriptor_resources();

  // Destroy cached samplers
  for (auto& [key, sampler] : m_samplerCache) {
    if (sampler) vkDestroySampler(m_vkDevice->raw(), sampler, nullptr);
  }
  m_samplerCache.clear();

  // Destroy cached pipelines
  for (auto& [key, pipeline] : m_pipelineCacheMap) {
    if (pipeline) vkDestroyPipeline(m_vkDevice->raw(), pipeline, nullptr);
  }
  m_pipelineCacheMap.clear();

  if (m_pipelineCache) vkDestroyPipelineCache(m_vkDevice->raw(), m_pipelineCache, nullptr);
  if (m_acquireSemaphore) m_vkDevice->destroy_semaphore(m_acquireSemaphore);
  if (m_presentSemaphore) m_vkDevice->destroy_semaphore(m_presentSemaphore);
  if (m_pipelineLayout) vkDestroyPipelineLayout(m_vkDevice->raw(), m_pipelineLayout, nullptr);
  if (m_compiledVS.module) vkDestroyShaderModule(m_vkDevice->raw(), m_compiledVS.module, nullptr);
  if (m_compiledPS.module) vkDestroyShaderModule(m_vkDevice->raw(), m_compiledPS.module, nullptr);
  if (m_fallbackVS) vkDestroyShaderModule(m_vkDevice->raw(), m_fallbackVS, nullptr);
  if (m_fallbackFS) vkDestroyShaderModule(m_vkDevice->raw(), m_fallbackFS, nullptr);
  if (m_ffFS) vkDestroyShaderModule(m_vkDevice->raw(), m_ffFS, nullptr);

  for (auto& tex : m_textures) tex = nullptr;
  m_pixelShader = nullptr;
  m_vertexShader = nullptr;
  m_indexBuffer = nullptr;
}

// --- Async pipeline compilation worker ---

void D3D9Device::pipeline_worker_thread() {
  VKWIND_INFO(kTag, "Pipeline worker thread started");

  while (m_workerRunning) {
    AsyncPipelineJob job;
    bool hasJob = false;

    // Wait for a job
    while (m_workerRunning) {
      {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        if (!m_asyncQueue.empty()) {
          job = std::move(m_asyncQueue.front());
          m_asyncQueue.pop();
          hasJob = true;
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!hasJob || !m_workerRunning) break;

    // The expensive call: vkCreateGraphicsPipelines (5-50ms on Mali)
    // PipelineState is a value copy — safe to use from worker thread
    // Shader modules are stable as long as game holds shader objects
    ::VkPipeline result = m_vkPipeline->create_graphics_pipeline(
      job.state, job.renderPass, m_pipelineCache);

    if (result) {
      std::lock_guard<std::mutex> lock(m_asyncMutex);
      m_asyncResults[job.key] = result;
      VKWIND_INFO(kTag, "Async pipeline compiled successfully");
    } else {
      VKWIND_ERR(kTag, "Async pipeline compilation failed");
    }
  }

  VKWIND_INFO(kTag, "Pipeline worker thread stopped");
}

// --- IUnknown ---

uint32_t D3D9Device::AddRef() { return ++m_refCount; }

uint32_t D3D9Device::Release() {
  uint32_t count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

int D3D9Device::QueryInterface(const void* iid, void** obj) {
  (void)iid; *obj = nullptr; return static_cast<int>(D3DERR_NOTAVAILABLE);
}

// --- Internal helpers ---

void D3D9Device::setup_default_states() {
  m_viewport.X = 0;
  m_viewport.Y = 0;
  m_viewport.Width = m_presParams.BackBufferWidth;
  m_viewport.Height = m_presParams.BackBufferHeight;
  m_viewport.MinZ = 0.0f;
  m_viewport.MaxZ = 1.0f;

  memset(&m_material, 0, sizeof(D3DMATERIAL9));
  m_material.Diffuse[0] = m_material.Diffuse[1] = m_material.Diffuse[2] = m_material.Diffuse[3] = 1.0f;
  m_material.Ambient[0] = m_material.Ambient[1] = m_material.Ambient[2] = m_material.Ambient[3] = 1.0f;
}

void D3D9Device::create_descriptor_resources() {
  if (!m_vkDevice) return;
  auto dev = m_vkDevice->raw();

  std::vector<VkDescriptorSetLayoutBinding> bindings;

  // binding 0: legacy UBO (64 bytes, VERTEX+FRAGMENT) — kept for compat
  VkDescriptorSetLayoutBinding uboBinding = {};
  uboBinding.binding = 0;
  uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboBinding.descriptorCount = 1;
  uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(uboBinding);

  // binding 1: VS constants UBO (256 vec4 = 4096 bytes, VERTEX only)
  VkDescriptorSetLayoutBinding vsConstBinding = {};
  vsConstBinding.binding = 1;
  vsConstBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  vsConstBinding.descriptorCount = 1;
  vsConstBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  bindings.push_back(vsConstBinding);

  // binding 2: PS constants UBO (256 vec4 = 4096 bytes, FRAGMENT only)
  VkDescriptorSetLayoutBinding psConstBinding = {};
  psConstBinding.binding = 2;
  psConstBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  psConstBinding.descriptorCount = 1;
  psConstBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(psConstBinding);

  // bindings 3-10: texture samplers
  for (uint32_t i = 3; i <= 10; i++) {
    VkDescriptorSetLayoutBinding texBinding = {};
    texBinding.binding = i;
    texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount = 1;
    texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(texBinding);
  }

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();
  vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_descSetLayout);

  VkDescriptorPoolSize poolSizes[] = {
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8},
  };

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 2;
  poolInfo.pPoolSizes = poolSizes;
  vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descPool);

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_descPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_descSetLayout;
  vkAllocateDescriptorSets(dev, &allocInfo, &m_descSets[0]);

  VkSamplerCreateInfo samplerInfo = {};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = 16.0f;
  samplerInfo.maxLod = 1000.0f;
  vkCreateSampler(dev, &samplerInfo, nullptr, &m_defaultSampler);

  // Create per-stage samplers (all start as linear/repeat)
  for (uint32_t i = 0; i < 8; i++) {
    VkSamplerCreateInfo si = samplerInfo;
    vkCreateSampler(dev, &si, nullptr, &m_samplers[i]);
  }

  // Push constants: MVP (64B) + alphaRef (4B) + alphaFunc (4B) = 72 bytes
  // Well under Mali G57's 128-byte limit
  VkPushConstantRange pushRange = {};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = 72;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_descSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);

  VKWIND_INFO(kTag, "Descriptor resources created (push constants for MVP: 64 bytes)");

  create_ubo_resource();
}

void D3D9Device::destroy_descriptor_resources() {
  if (!m_vkDevice) return;
  auto dev = m_vkDevice->raw();
  destroy_ubo_resource();
  if (m_defaultSampler) { vkDestroySampler(dev, m_defaultSampler, nullptr); m_defaultSampler = VK_NULL_HANDLE; }
  for (uint32_t i = 0; i < 8; i++) {
    if (m_samplers[i]) { vkDestroySampler(dev, m_samplers[i], nullptr); m_samplers[i] = VK_NULL_HANDLE; }
  }
  if (m_descPool) { vkDestroyDescriptorPool(dev, m_descPool, nullptr); m_descPool = VK_NULL_HANDLE; }
  if (m_descSetLayout) { vkDestroyDescriptorSetLayout(dev, m_descSetLayout, nullptr); m_descSetLayout = VK_NULL_HANDLE; }
}

void D3D9Device::create_ubo_resource() {
  if (!m_vkDevice || !m_descSets[0]) return;
  auto dev = m_vkDevice->raw();
  auto memProps = m_vkDevice->memory_properties();

  // Helper lambda to create a HOST_VISIBLE+COHERENT buffer
  auto create_mapped_ubo = [&](::VkBuffer& buf, VkDeviceMemory& mem, void*& mapped, VkDeviceSize size) {
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(dev, &bufInfo, nullptr, &buf);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(dev, buf, &memReqs);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
      if ((memReqs.memoryTypeBits & (1 << i)) &&
          (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        allocInfo.memoryTypeIndex = i;
        break;
      }
    }

    vkAllocateMemory(dev, &allocInfo, nullptr, &mem);
    vkBindBufferMemory(dev, buf, mem, 0);
    vkMapMemory(dev, mem, 0, size, 0, &mapped);
  };

  // binding 0: MVP UBO (64 bytes) — kept for legacy compat
  create_mapped_ubo(m_uboBuffer, m_uboMemory, m_uboMapped, 64);
  float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  memcpy(m_uboMapped, identity, 64);

  // binding 1: VS constants UBO (4096 bytes = 256 vec4)
  create_mapped_ubo(m_vsConstBuffer, m_vsConstMemory, m_vsConstMapped, kConstantsUBOSize);
  memset(m_vsConstMapped, 0, kConstantsUBOSize);

  // binding 2: PS constants UBO (4096 bytes = 256 vec4)
  create_mapped_ubo(m_psConstBuffer, m_psConstMemory, m_psConstMapped, kConstantsUBOSize);
  memset(m_psConstMapped, 0, kConstantsUBOSize);

  // Write descriptors
  VkDescriptorBufferInfo legacyBufInfo = {};
  legacyBufInfo.buffer = m_uboBuffer;
  legacyBufInfo.offset = 0;
  legacyBufInfo.range = 64;

  VkDescriptorBufferInfo vsBufInfo = {};
  vsBufInfo.buffer = m_vsConstBuffer;
  vsBufInfo.offset = 0;
  vsBufInfo.range = kConstantsUBOSize;

  VkDescriptorBufferInfo psBufInfo = {};
  psBufInfo.buffer = m_psConstBuffer;
  psBufInfo.offset = 0;
  psBufInfo.range = kConstantsUBOSize;

  VkWriteDescriptorSet write0 = {};
  write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write0.dstSet = m_descSets[0];
  write0.dstBinding = 0;
  write0.descriptorCount = 1;
  write0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write0.pBufferInfo = &legacyBufInfo;

  VkWriteDescriptorSet write1 = {};
  write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write1.dstSet = m_descSets[0];
  write1.dstBinding = 1;
  write1.descriptorCount = 1;
  write1.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write1.pBufferInfo = &vsBufInfo;

  VkWriteDescriptorSet write2 = {};
  write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write2.dstSet = m_descSets[0];
  write2.dstBinding = 2;
  write2.descriptorCount = 1;
  write2.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write2.pBufferInfo = &psBufInfo;

  VkWriteDescriptorSet writes[3] = {write0, write1, write2};
  vkUpdateDescriptorSets(dev, 3, writes, 0, nullptr);

  VKWIND_INFO(kTag, "UBO resources created: MVP=64B (b0), VS_const=4096B (b1), PS_const=4096B (b2)");
}

void D3D9Device::destroy_ubo_resource() {
  if (!m_vkDevice) return;
  auto dev = m_vkDevice->raw();

  auto destroy_mapped_ubo = [&](::VkBuffer& buf, VkDeviceMemory& mem, void*& mapped) {
    if (mapped) { vkUnmapMemory(dev, mem); mapped = nullptr; }
    if (buf) { vkDestroyBuffer(dev, buf, nullptr); buf = VK_NULL_HANDLE; }
    if (mem) { vkFreeMemory(dev, mem, nullptr); mem = VK_NULL_HANDLE; }
  };

  destroy_mapped_ubo(m_uboBuffer, m_uboMemory, m_uboMapped);
  destroy_mapped_ubo(m_vsConstBuffer, m_vsConstMemory, m_vsConstMapped);
  destroy_mapped_ubo(m_psConstBuffer, m_psConstMemory, m_psConstMapped);
}

void D3D9Device::upload_shader_constants() {
  if (m_vsConstantsDirty && m_vsConstMapped) {
    memcpy(m_vsConstMapped, m_vsFloatConstants, kConstantsUBOSize);
    m_vsConstantsDirty = false;
  }
  if (m_psConstantsDirty && m_psConstMapped) {
    memcpy(m_psConstMapped, m_psFloatConstants, kConstantsUBOSize);
    m_psConstantsDirty = false;
  }
}

void D3D9Device::upload_ubo_mvp() {
  if (!m_uboMapped) return;

  // Compute MVP = World * View * Projection (D3D9 row-major, row-vector convention)
  // Direct memcpy to GLSL mat4 works correctly due to row/column major equivalence.

  const D3DMATRIX& world = m_transforms[256];       // D3DTS_WORLD
  const D3DMATRIX& view   = m_transforms[2];         // D3DTS_VIEW
  const D3DMATRIX& proj   = m_transforms[3];         // D3DTS_PROJECTION

  // First: temp = World * View
  float temp[16];
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      float sum = 0;
      for (int k = 0; k < 4; k++) {
        sum += world.m[row][k] * view.m[k][col];
      }
      temp[row * 4 + col] = sum;
    }
  }

  // Then: MVP = temp * Projection
  float mvp[16];
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      float sum = 0;
      for (int k = 0; k < 4; k++) {
        sum += temp[row * 4 + k] * proj.m[k][col];
      }
      mvp[row * 4 + col] = sum;
    }
  }

  memcpy(m_uboMapped, mvp, 64);
}

void D3D9Device::update_texture_descriptors() {
  if (!m_vkDevice || !m_descSets[0]) return;
  auto dev = m_vkDevice->raw();

  VkWriteDescriptorSet writes[8] = {};
  VkDescriptorImageInfo imageInfos[8] = {};
  uint32_t writeCount = 0;

  for (uint32_t i = 0; i < 8; i++) {
    VkImageView imageView = VK_NULL_HANDLE;

    if (m_textures[i]) {
      auto* tex = static_cast<D3D9Texture*>(m_textures[i]);
      imageView = tex->image_view(0);
    }

    imageInfos[i].sampler = m_samplers[i] ? m_samplers[i] : m_defaultSampler;
    imageInfos[i].imageView = imageView;
    imageInfos[i].imageLayout = imageView ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

    writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[writeCount].dstSet = m_descSets[0];
    writes[writeCount].dstBinding = i + 3; // bindings 3-10 for textures
    writes[writeCount].dstArrayElement = 0;
    writes[writeCount].descriptorCount = 1;
    writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[writeCount].pImageInfo = &imageInfos[i];
    writeCount++;
  }

  if (writeCount > 0) {
    vkUpdateDescriptorSets(dev, writeCount, writes, 0, nullptr);
  }
}

void D3D9Device::ensure_render_pass_active() {
  if (m_renderPassActive) return;
  if (!m_vkSwapchain || !m_cmdManager) return;

  float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  if (m_clearPending) {
    memcpy(clearColor, m_clearColor, sizeof(float) * 4);
    m_clearPending = false;
  }

  m_cmdManager->begin_render_pass(
    m_vkSwapchain->render_pass(),
    m_vkSwapchain->framebuffer(m_currentImageIndex),
    m_vkSwapchain->width(),
    m_vkSwapchain->height(),
    clearColor,
    m_vkSwapchain->depth_format(),
    m_clearDepth,
    m_clearStencil);

  // Auto-viewport: if app never called SetViewport, use full render target
  uint32_t vpW = m_viewport.Width ? static_cast<uint32_t>(m_viewport.Width) : m_vkSwapchain->width();
  uint32_t vpH = m_viewport.Height ? static_cast<uint32_t>(m_viewport.Height) : m_vkSwapchain->height();
  float vpX = static_cast<float>(m_viewport.X);
  float vpY = static_cast<float>(m_viewport.Y);
  float minZ = m_viewport.MinZ;
  float maxZ = m_viewport.MaxZ;
  if (maxZ <= minZ) maxZ = 1.0f;

  m_cmdManager->set_viewport(vpX, vpY, static_cast<float>(vpW), static_cast<float>(vpH), minZ, maxZ);

  if (m_scissorEnabled) {
    uint32_t scW = static_cast<uint32_t>(m_scissor.right - m_scissor.left);
    uint32_t scH = static_cast<uint32_t>(m_scissor.bottom - m_scissor.top);
    if (scW > 0 && scH > 0)
      m_cmdManager->set_scissor(m_scissor.left, m_scissor.top, scW, scH);
    else
      m_cmdManager->set_scissor(m_viewport.X, m_viewport.Y, vpW, vpH);
  } else {
    m_cmdManager->set_scissor(m_viewport.X, m_viewport.Y, vpW, vpH);
  }

  // Apply dynamic state from stored render states
  float blendConstants[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  uint32_t bf = m_renderStates[D3DRS_BLENDFACTOR];
  blendConstants[0] = ((bf >> 16) & 0xFF) / 255.0f;
  blendConstants[1] = ((bf >> 8) & 0xFF) / 255.0f;
  blendConstants[2] = (bf & 0xFF) / 255.0f;
  blendConstants[3] = ((bf >> 24) & 0xFF) / 255.0f;
  m_cmdManager->set_blend_constants(blendConstants);
  m_cmdManager->set_stencil_reference(m_renderStates[D3DRS_STENCILREF] & 0xFF);

  m_renderPassActive = true;
  m_activeRenderCmd = m_cmdManager->current_cmd();
}

void D3D9Device::end_active_render_pass() {
  if (!m_renderPassActive) return;
  m_cmdManager->end_render_pass();
  m_renderPassActive = false;
}

::VkPipeline D3D9Device::create_or_get_pipeline() {
  if (!m_pipelineDirty && m_currentPipeline) return m_currentPipeline;
  if (!m_vkPipeline || !m_vkSwapchain) return VK_NULL_HANDLE;

  auto dev = m_vkDevice->raw();

  if (m_vertexShader && !m_compiledVS.module) {
    auto* vs = static_cast<D3D9VertexShader*>(m_vertexShader);
    if (!vs->bytecode().empty()) {
      const uint32_t* data = vs->bytecode().data();
      uint32_t sizeBytes = static_cast<uint32_t>(vs->bytecode().size() * 4);
      std::vector<uint32_t> spirv;

      // Try SM3 translator first (handles SM2/SM3 bytecode from D3D9 apps)
      SM3Translator sm3;
      auto sm3Result = sm3.translate(data, vs->bytecode().size());
      if (!sm3Result.spirv.empty()) {
        spirv = std::move(sm3Result.spirv);
        VKWIND_INFO(kTag, "VS translated via SM3 path: %u words", (uint32_t)spirv.size());
      } else {
        // Fallback to old DXBC/SM4 translator
        ShaderTranslator translator;
        auto translated = translator.translate_vertex_shader(data, sizeBytes);
        spirv = std::move(translated.spirv);
        VKWIND_INFO(kTag, "VS translated via DXBC path: %u words", (uint32_t)spirv.size());
      }

      if (!spirv.empty()) {
        VkShaderModuleCreateInfo moduleInfo = {};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spirv.size() * 4;
        moduleInfo.pCode = spirv.data();

        VkResult result = vkCreateShaderModule(dev, &moduleInfo, nullptr, &m_compiledVS.module);
        if (result == VK_SUCCESS) {
          m_compiledVS.spirv = std::move(spirv);
          VKWIND_INFO(kTag, "VS module created: %u words", (uint32_t)m_compiledVS.spirv.size());
        } else {
          VKWIND_ERR(kTag, "Failed to create VS module: %d", result);
        }
      }
    }
  }

  if (m_pixelShader && !m_compiledPS.module) {
    auto* ps = static_cast<D3D9PixelShader*>(m_pixelShader);
    if (!ps->bytecode().empty()) {
      const uint32_t* data = ps->bytecode().data();
      uint32_t sizeBytes = static_cast<uint32_t>(ps->bytecode().size() * 4);
      std::vector<uint32_t> spirv;

      // Try SM3 translator first (handles SM2/SM3 bytecode from D3D9 apps)
      SM3Translator sm3;
      auto sm3Result = sm3.translate(data, ps->bytecode().size());
      if (!sm3Result.spirv.empty()) {
        spirv = std::move(sm3Result.spirv);
        VKWIND_INFO(kTag, "PS translated via SM3 path: %u words", (uint32_t)spirv.size());
      } else {
        // Fallback to old DXBC/SM4 translator
        ShaderTranslator translator;
        auto translated = translator.translate_pixel_shader(data, sizeBytes);
        spirv = std::move(translated.spirv);
        VKWIND_INFO(kTag, "PS translated via DXBC path: %u words", (uint32_t)spirv.size());
      }

      if (!spirv.empty()) {
        VkShaderModuleCreateInfo moduleInfo = {};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spirv.size() * 4;
        moduleInfo.pCode = spirv.data();

        VkResult result = vkCreateShaderModule(dev, &moduleInfo, nullptr, &m_compiledPS.module);
        if (result == VK_SUCCESS) {
          m_compiledPS.spirv = std::move(spirv);
          VKWIND_INFO(kTag, "PS module created: %u words", (uint32_t)m_compiledPS.spirv.size());
        } else {
          VKWIND_ERR(kTag, "Failed to create PS module: %d", result);
        }
      }
    }
  }

  bool useFallback = (!m_compiledVS.module || !m_compiledPS.module);

  // If no pixel shader is set, generate a fixed-function texture combiner shader
  if (!m_pixelShader && m_shadersDirty) {
    // Destroy previous FF shader
    if (m_ffFS) {
      vkDestroyShaderModule(dev, m_ffFS, nullptr);
      m_ffFS = VK_NULL_HANDLE;
    }
    m_shadersDirty = false;
  }

  if (!m_pixelShader && !m_ffFS) {
    // Build texture stage config from current state
    TextureStageConfig ffConfig;
    uint32_t activeTex = 0;
    for (uint32_t s = 0; s < 8; s++) {
      if (m_textures[s]) activeTex = s + 1;
    }
    ffConfig.activeTextureCount = activeTex;

    // Check if any stage has explicit COLOROP set (not 0/DISABLE)
    bool hasExplicitOps = false;
    for (uint32_t s = 0; s < activeTex; s++) {
      if (m_textureStageStates[s][1] > 1) { // COLOROP > DISABLE
        hasExplicitOps = true;
        break;
      }
    }

    ffConfig.textureFactor = m_renderStates[D3DRS_TEXTUREFACTOR];

    for (uint32_t s = 0; s < 8; s++) {
      ffConfig.colorOp[s]    = m_textureStageStates[s][1];  // D3DTSS_COLOROP
      ffConfig.colorArg1[s]  = m_textureStageStates[s][2];  // D3DTSS_COLORARG1
      ffConfig.colorArg2[s]  = m_textureStageStates[s][3];  // D3DTSS_COLORARG2
      ffConfig.alphaOp[s]    = m_textureStageStates[s][4];  // D3DTSS_ALPHAOP
      ffConfig.alphaArg1[s]  = m_textureStageStates[s][5];  // D3DTSS_ALPHAARG1
      ffConfig.alphaArg2[s]  = m_textureStageStates[s][6];  // D3DTSS_ALPHAARG2
      ffConfig.resultArg[s]  = m_textureStageStates[s][12]; // D3DTSS_RESULTARG (12)
      ffConfig.texCoordIndex[s] = m_textureStageStates[s][11]; // D3DTSS_TEXCOORDINDEX
    }

    // Generate the fixed-function shader
    auto ffSpirv = generate_fixed_function_pixel_shader(ffConfig);
    if (!ffSpirv.empty()) {
      VkShaderModuleCreateInfo moduleInfo = {};
      moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      moduleInfo.codeSize = ffSpirv.size() * 4;
      moduleInfo.pCode = ffSpirv.data();
      VkResult result = vkCreateShaderModule(dev, &moduleInfo, nullptr, &m_ffFS);
      if (result == VK_SUCCESS) {
        VKWIND_INFO(kTag, "Fixed-function FS generated: %u words, %u active textures",
          (uint32_t)ffSpirv.size(), activeTex);
      } else {
        VKWIND_ERR(kTag, "Failed to create FF FS module: %d", result);
      }
    }
  }

  if (useFallback) {
    if (!m_fallbackVS) {
      VkShaderModuleCreateInfo moduleInfo = {};
      moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      moduleInfo.codeSize = sizeof(kDefaultVS);
      moduleInfo.pCode = kDefaultVS;
      VkResult result = vkCreateShaderModule(dev, &moduleInfo, nullptr, &m_fallbackVS);
      if (result != VK_SUCCESS) {
        VKWIND_ERR(kTag, "Failed to create fallback VS: %d", result);
        return VK_NULL_HANDLE;
      }
      VKWIND_INFO(kTag, "Fallback VS created");
    }

    if (!m_fallbackFS) {
      VkShaderModuleCreateInfo moduleInfo = {};
      moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      moduleInfo.codeSize = sizeof(kDefaultFS);
      moduleInfo.pCode = kDefaultFS;
      VkResult result = vkCreateShaderModule(dev, &moduleInfo, nullptr, &m_fallbackFS);
      if (result != VK_SUCCESS) {
        VKWIND_ERR(kTag, "Failed to create fallback FS: %d", result);
        return VK_NULL_HANDLE;
      }
      VKWIND_INFO(kTag, "Fallback FS created");
    }
  }

  VkShaderModule vsModule = useFallback ? m_fallbackVS : m_compiledVS.module;
  VkShaderModule fsModule = m_ffFS ? m_ffFS : (useFallback ? m_fallbackFS : m_compiledPS.module);

  if (!vsModule || !fsModule) {
    VKWIND_ERR(kTag, "No shader modules available");
    return VK_NULL_HANDLE;
  }

  StateMapper::map_render_states(m_renderStates, m_pipelineState);
  m_pipelineState.vertexShader = vsModule;
  m_pipelineState.fragmentShader = fsModule;
  m_pipelineState.pipelineLayout = m_pipelineLayout;

  // Vertex input: prefer vertex declaration, then FVF, then fallback
  if (m_vertexDecl && !m_vertexDecl->elements().empty()) {
    // Build vertex input from D3D9 vertex declaration with proper multi-stream support
    m_pipelineState.vertexBindings.clear();
    m_pipelineState.vertexAttributes.clear();

    // Collect unique streams and compute their strides
    uint32_t streamMask = 0;
    for (const auto& e : m_vertexDecl->elements()) {
      streamMask |= (1u << e.Stream);
    }

    for (uint32_t s = 0; s < 16; s++) {
      if (!(streamMask & (1u << s))) continue;
      uint32_t stride = m_streamSources[s].stride;
      if (stride == 0) stride = m_vertexDecl->calculate_stride();
      m_pipelineState.vertexBindings.push_back({s, stride, VK_VERTEX_INPUT_RATE_VERTEX});
    }

    uint32_t loc = 0;
    for (const auto& e : m_vertexDecl->elements()) {
      if (e.Usage == D3DDECLUSAGE_POSITION || e.Usage == D3DDECLUSAGE_POSITIONT) {
        VkFormat fmt;
        switch (e.Type) {
          case D3DDECLTYPE_FLOAT2: fmt = VK_FORMAT_R32G32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT3: fmt = VK_FORMAT_R32G32B32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT4: fmt = VK_FORMAT_R32G32B32A32_SFLOAT; break;
          default: fmt = VK_FORMAT_R32G32B32_SFLOAT; break;
        }
        m_pipelineState.vertexAttributes.push_back({loc, e.Stream, fmt, e.Offset});
      } else if (e.Usage == D3DDECLUSAGE_NORMAL) {
        VkFormat fmt;
        switch (e.Type) {
          case D3DDECLTYPE_FLOAT3: fmt = VK_FORMAT_R32G32B32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT4: fmt = VK_FORMAT_R32G32B32A32_SFLOAT; break;
          default: fmt = VK_FORMAT_R32G32B32_SFLOAT; break;
        }
        m_pipelineState.vertexAttributes.push_back({loc, e.Stream, fmt, e.Offset});
      } else if (e.Usage == D3DDECLUSAGE_COLOR) {
        VkFormat fmt = (e.Type == D3DDECLTYPE_D3DCOLOR) ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R32G32B32A32_SFLOAT;
        m_pipelineState.vertexAttributes.push_back({loc, e.Stream, fmt, e.Offset});
      } else if (e.Usage == D3DDECLUSAGE_TEXCOORD) {
        VkFormat fmt;
        switch (e.Type) {
          case D3DDECLTYPE_FLOAT1: fmt = VK_FORMAT_R32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT2: fmt = VK_FORMAT_R32G32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT3: fmt = VK_FORMAT_R32G32B32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT4: fmt = VK_FORMAT_R32G32B32A32_SFLOAT; break;
          case D3DDECLTYPE_SHORT2: fmt = VK_FORMAT_R16G16_SINT; break;
          case D3DDECLTYPE_SHORT4: fmt = VK_FORMAT_R16G16B16A16_SINT; break;
          case D3DDECLTYPE_FLOAT16_2: fmt = VK_FORMAT_R16G16_SFLOAT; break;
          case D3DDECLTYPE_FLOAT16_4: fmt = VK_FORMAT_R16G16B16A16_SFLOAT; break;
          default: fmt = VK_FORMAT_R32G32_SFLOAT; break;
        }
        m_pipelineState.vertexAttributes.push_back({loc, e.Stream, fmt, e.Offset});
      } else if (e.Usage == D3DDECLUSAGE_BLENDWEIGHT) {
        VkFormat fmt;
        switch (e.Type) {
          case D3DDECLTYPE_FLOAT1: fmt = VK_FORMAT_R32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT2: fmt = VK_FORMAT_R32G32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT3: fmt = VK_FORMAT_R32G32B32_SFLOAT; break;
          case D3DDECLTYPE_FLOAT4: fmt = VK_FORMAT_R32G32B32A32_SFLOAT; break;
          case D3DDECLTYPE_UBYTE4N: fmt = VK_FORMAT_R8G8B8A8_UNORM; break;
          default: fmt = VK_FORMAT_R32_SFLOAT; break;
        }
        m_pipelineState.vertexAttributes.push_back({loc, e.Stream, fmt, e.Offset});
      } else if (e.Usage == D3DDECLUSAGE_BLENDINDICES) {
        VkFormat fmt = (e.Type == D3DDECLTYPE_UBYTE4) ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_UINT;
        m_pipelineState.vertexAttributes.push_back({loc, e.Stream, fmt, e.Offset});
      }
      loc++;
    }
  } else if (m_fvf != 0 && m_streamSources[0].stride > 0) {
    fvf_to_vk_attributes(m_fvf, m_streamSources[0].stride,
      m_pipelineState.vertexBindings, m_pipelineState.vertexAttributes);
  } else if (useFallback) {
    m_pipelineState.vertexBindings = {{0, 36, VK_VERTEX_INPUT_RATE_VERTEX}};
    m_pipelineState.vertexAttributes = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, 28},
    };
  } else if (m_streamSources[0].stride > 0) {
    m_pipelineState.vertexBindings = {{0, m_streamSources[0].stride, VK_VERTEX_INPUT_RATE_VERTEX}};
    m_pipelineState.vertexAttributes.clear();
  }

  // Build pipeline key for cache lookup
  PipelineKey key = {};
  key.vs = vsModule;
  key.ps = fsModule;
  key.fvf = m_fvf;
  memcpy(key.renderStates, m_renderStates, sizeof(m_renderStates));

  // Check sync cache first
  auto it = m_pipelineCacheMap.find(key);
  if (it != m_pipelineCacheMap.end()) {
    m_currentPipeline = it->second;
    m_pipelineDirty = false;
    return m_currentPipeline;
  }

  // Async path: queue vkCreateGraphicsPipelines to worker thread
  if (m_asyncPipelineEnabled && m_pipelineWorker.joinable()) {
    // Check if already compiled by worker
    {
      std::lock_guard<std::mutex> lock(m_asyncMutex);
      auto asyncIt = m_asyncResults.find(key);
      if (asyncIt != m_asyncResults.end()) {
        // Worker finished — use the compiled pipeline
        m_currentPipeline = asyncIt->second;
        m_pipelineCacheMap[key] = m_currentPipeline;
        m_asyncResults.erase(asyncIt);
        m_pipelineDirty = false;
        VKWIND_INFO(kTag, "Async pipeline ready (cached)");
        return m_currentPipeline;
      }
    }

    // Not ready yet — queue compilation, return fallback
    bool alreadyQueued = false;
    {
      std::lock_guard<std::mutex> lock(m_asyncMutex);
      // Check if this key is already in the queue (approximate check)
      auto q = m_asyncQueue;
      while (!q.empty()) {
        if (q.front().key.vs == key.vs && q.front().key.ps == key.ps &&
            q.front().key.fvf == key.fvf) {
          alreadyQueued = true;
          break;
        }
        q.pop();
      }
    }

    if (!alreadyQueued) {
      AsyncPipelineJob job;
      job.key = key;
      job.state = m_pipelineState;  // Value copy — safe for worker
      job.renderPass = m_vkSwapchain->render_pass();

      std::lock_guard<std::mutex> lock(m_asyncMutex);
      m_asyncQueue.push(std::move(job));
      VKWIND_DBG(kTag, "Pipeline queued for async compilation");
    }

    // Return fallback — game renders without hitch
    m_currentPipeline = VK_NULL_HANDLE;
    m_pipelineDirty = false;

    // Ensure fallback shaders are available
    if (!m_fallbackVS) {
      VkShaderModuleCreateInfo moduleInfo = {};
      moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      moduleInfo.codeSize = sizeof(kDefaultVS);
      moduleInfo.pCode = kDefaultVS;
      vkCreateShaderModule(dev, &moduleInfo, nullptr, &m_fallbackVS);
    }
    if (!m_fallbackFS) {
      VkShaderModuleCreateInfo moduleInfo = {};
      moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      moduleInfo.codeSize = sizeof(kDefaultFS);
      moduleInfo.pCode = kDefaultFS;
      vkCreateShaderModule(dev, &moduleInfo, nullptr, &m_fallbackFS);
    }

    // Build fallback pipeline if needed
    if (!m_currentPipeline && m_fallbackVS && m_fallbackFS) {
      PipelineState fallbackState = m_pipelineState;
      fallbackState.vertexShader = m_fallbackVS;
      fallbackState.fragmentShader = m_fallbackFS;
      m_currentPipeline = m_vkPipeline->create_graphics_pipeline(
        fallbackState, m_vkSwapchain->render_pass(), m_pipelineCache);
    }

    return m_currentPipeline;
  }

  // Sync fallback path (async disabled)
  ::VkPipeline result = m_vkPipeline->create_graphics_pipeline(m_pipelineState, m_vkSwapchain->render_pass(), m_pipelineCache);
  if (result) {
    m_currentPipeline = result;
    m_pipelineDirty = false;
    m_pipelineCacheMap[key] = result;
    VKWIND_INFO(kTag, "Pipeline created (sync): topo=%d, blend=%d, depth=%d, fallback=%d",
      m_pipelineState.topology, m_pipelineState.blendEnable, m_pipelineState.depthTestEnable, (int)useFallback);
  }

  return m_currentPipeline;
}

void D3D9Device::apply_render_state(uint32_t state, uint32_t value) {
  (void)state; (void)value;

  // Dynamic states — applied immediately, no pipeline invalidation
  if (state == D3DRS_STENCILREF && m_renderPassActive) {
    m_cmdManager->set_stencil_reference(value & 0xFF);
    return;
  }
  if (state == D3DRS_BLENDFACTOR) {
    // Blend factor applied dynamically via set_blend_constants
    if (m_renderPassActive) {
      float r = ((value >> 16) & 0xFF) / 255.0f;
      float g = ((value >> 8) & 0xFF) / 255.0f;
      float b = (value & 0xFF) / 255.0f;
      float a = ((value >> 24) & 0xFF) / 255.0f;
      float rgba[4] = {r, g, b, a};
      m_cmdManager->set_blend_constants(rgba);
    }
    return;
  }

  // Alpha test via push constants — no pipeline invalidation needed
  if (state == D3DRS_ALPHATESTENABLE) {
    if (value) {
      // Enable alpha test — if no func set yet, default to ALWAYS (pass all)
      if (m_alphaFunc == 0) m_alphaFunc = 8;
    } else {
      m_alphaFunc = 0; // disabled
    }
    return;
  }
  if (state == D3DRS_ALPHAREF) {
    m_alphaRef = (value & 0xFF) / 255.0f;
    return;
  }
  if (state == D3DRS_ALPHAFUNC) {
    // D3DCMP values 1-8 map directly to shader
    m_alphaFunc = value & 0xF;
    return;
  }

  StateMapper::map_render_states(m_renderStates, m_pipelineState);
  m_pipelineDirty = true;
}

void D3D9Device::apply_viewport_scissor() {
  if (!m_cmdManager) return;

  uint32_t vpW = m_viewport.Width ? static_cast<uint32_t>(m_viewport.Width) : (m_vkSwapchain ? m_vkSwapchain->width() : 800);
  uint32_t vpH = m_viewport.Height ? static_cast<uint32_t>(m_viewport.Height) : (m_vkSwapchain ? m_vkSwapchain->height() : 600);
  float minZ = m_viewport.MinZ;
  float maxZ = m_viewport.MaxZ;
  if (maxZ <= minZ) maxZ = 1.0f;

  m_cmdManager->set_viewport(static_cast<float>(m_viewport.X), static_cast<float>(m_viewport.Y),
    static_cast<float>(vpW), static_cast<float>(vpH), minZ, maxZ);

  if (m_scissorEnabled) {
    uint32_t scW = static_cast<uint32_t>(m_scissor.right - m_scissor.left);
    uint32_t scH = static_cast<uint32_t>(m_scissor.bottom - m_scissor.top);
    if (scW > 0 && scH > 0)
      m_cmdManager->set_scissor(m_scissor.left, m_scissor.top, scW, scH);
  } else {
    m_cmdManager->set_scissor(m_viewport.X, m_viewport.Y, vpW, vpH);
  }
}

void D3D9Device::push_mvp_constants() {
  if (!m_cmdManager || !m_pipelineLayout) return;

  // Upload dirty shader constants to UBO
  upload_shader_constants();

  const D3DMATRIX& world = m_transforms[256];
  const D3DMATRIX& view   = m_transforms[2];
  const D3DMATRIX& proj   = m_transforms[3];

  // Check if MVP needs recomputation (compare transform timestamps)
  if (m_transformDirty) {
    m_lastWorldTS++;
    m_lastViewTS++;
    m_lastProjTS++;
  }
  m_curWorldTS = m_lastWorldTS;
  m_curViewTS = m_lastViewTS;
  m_curProjTS = m_lastProjTS;
  m_transformDirty = false;

  float mvp[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  float temp[16];
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += world.m[r][k] * view.m[k][c];
      temp[r*4+c] = s;
    }
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += temp[r*4+k] * proj.m[k][c];
      mvp[r*4+c] = s;
    }
  m_cmdManager->push_constants(m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp);

  // Push alpha test constants (FRAGMENT stage, offset 64)
  struct { float alphaRef; int32_t alphaFunc; } alphaPC = {};
  alphaPC.alphaRef = m_alphaRef;
  alphaPC.alphaFunc = m_alphaFunc;
  m_cmdManager->push_constants(m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 64, 8, &alphaPC);
}

// --- Device management ---

int D3D9Device::TestCooperativeLevel() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_deviceLost ? static_cast<int>(D3DERR_DEVICELOST) : static_cast<int>(D3D_OK);
}

int D3D9Device::GetAvailablePoolMem(uint32_t Usage) { (void)Usage; return 64 * 1024 * 1024; }

int D3D9Device::EvictManagedResources() { return static_cast<int>(D3D_OK); }

int D3D9Device::GetDirect3D(IDirect3D9** ppD3D9) {
  if (!ppD3D9) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppD3D9 = m_d3d;
  if (m_d3d) m_d3d->AddRef();
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetDeviceCaps(D3DCAPS9* caps) {
  if (!caps) return static_cast<int>(D3DERR_INVALIDCALL);
  memset(caps, 0, sizeof(D3DCAPS9));

  caps->MaxTextureWidth = 16384;
  caps->MaxTextureHeight = 16384;
  caps->MaxVolumeExtent = 2048;
  caps->MaxTextureRepeat = 8192;
  caps->MaxTextureAspectRatio = 8192;
  caps->MaxAnisotropy = 16;
  caps->MaxVertexW = 1e10f;
  caps->GuardBandLeft = -16384.0f;
  caps->GuardBandTop = -16384.0f;
  caps->GuardBandRight = 16384.0f;
  caps->GuardBandBottom = 16384.0f;
  caps->MaxPointSize = 256.0f;
  caps->MaxPrimitiveCount = 0x00ffffff;
  caps->MaxVertexIndex = 0x00ffffff;
  caps->MaxStreams = 16;
  caps->MaxVertexStride = 256;
  caps->VertexShaderVersion = 0xfffe0300;
  caps->PixelShaderVersion = 0xfffe0300;
  caps->MaxVertexShaderConst = 256;
  caps->MaxPixelShaderValue = 65504.0f;
  caps->MaxTextureBlendStages = 8;
  caps->MaxSimultaneousTextures = 8;
  caps->MaxActiveLights = 8;
  caps->MaxUserClipPlanes = 6;
  caps->MaxVertexBlendMatrices = 4;
  caps->StencilCaps = 0x00001ff0;
  caps->FVFCaps = 0x001fffff;
  caps->TextureOpCaps = 0x00001edf;
  caps->VertexProcessingCaps = 0x0000003b;

  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetDisplayMode(uint32_t iSwapChain, D3DDISPLAYMODE* pMode) {
  (void)iSwapChain;
  if (!pMode) return static_cast<int>(D3DERR_INVALIDCALL);
  pMode->Width = m_presParams.BackBufferWidth;
  pMode->Height = m_presParams.BackBufferHeight;
  pMode->RefreshRate = 60;
  pMode->Format = m_presParams.BackBufferFormat;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetCreationParameters(void* pParameters) { (void)pParameters; return static_cast<int>(D3D_OK); }

int D3D9Device::SetCursorProperties(uint32_t XHotSpot, uint32_t YHotSpot, IDirect3DSurface9* pCursorBitmap) {
  (void)XHotSpot; (void)YHotSpot; (void)pCursorBitmap; return static_cast<int>(D3D_OK);
}

void D3D9Device::SetCursorPosition(int X, int Y, uint32_t Flags) { (void)X; (void)Y; (void)Flags; }

int D3D9Device::ShowCursor(bool bShow) { (void)bShow; return static_cast<int>(D3D_OK); }

int D3D9Device::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) {
  (void)pPresentationParameters; *pSwapChain = nullptr; return static_cast<int>(D3D_OK);
}

int D3D9Device::GetSwapChain(uint32_t iSwapChain, IDirect3DSwapChain9** pSwapChain) {
  (void)iSwapChain; *pSwapChain = nullptr; return static_cast<int>(D3D_OK);
}

uint32_t D3D9Device::GetNumberOfSwapChains() { return 1; }

int D3D9Device::Reset(D3DPRESENT_PARAMETERS* params) {
  std::lock_guard<std::mutex> lock(m_mutex);
  VKWIND_INFO(kTag, "Reset: %ux%u, windowed=%d",
    params->BackBufferWidth, params->BackBufferHeight, params->Windowed);

  m_presParams = *params;
  m_inScene = false;

  // Mark pipeline dirty — render pass format may change on resize
  m_pipelineDirty = true;
  m_currentPipeline = VK_NULL_HANDLE;

  if (m_vkSwapchain) {
    m_vkSwapchain->resize(params->BackBufferWidth, params->BackBufferHeight);
  }

  return static_cast<int>(D3D_OK);
}

int D3D9Device::Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, const void* pDirtyRegion) {
  (void)pSourceRect; (void)pDestRect; (void)hDestWindowOverride; (void)pDirtyRegion;

  if (!m_vkSwapchain || !m_cmdManager) return static_cast<int>(D3DERR_DEVICELOST);

  if (m_textureManager) m_textureManager->begin_frame();
  m_cmdManager->begin_frame();
  m_frameTempBuffers.clear();
  m_upVertexWriteOffset = 0; // Reset UP ring buffer (GPU done with previous frame)
  m_upIndexWriteOffset = 0;
  m_frameDraws[m_cmdManager->current_frame()].clear();
  m_frameDraws[m_cmdManager->current_frame()] = std::move(m_pendingDraws);
  m_pendingDraws.clear();

  uint32_t imageIndex = 0;
  VkResult result = m_vkSwapchain->acquire_next_image(m_acquireSemaphore, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    m_vkSwapchain->resize(m_presParams.BackBufferWidth, m_presParams.BackBufferHeight);
    m_cmdManager->advance_frame();
    return static_cast<int>(D3D_OK);
  }

  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to acquire next image: %d", result);
    m_cmdManager->advance_frame();
    return static_cast<int>(D3DERR_DEVICELOST);
  }

  m_currentImageIndex = imageIndex;
  ensure_render_pass_active();

  // Update and bind descriptors ONLY if textures changed
  if (m_texturesDirty) {
    update_texture_descriptors();
    m_texturesDirty = false;
  }

  if (m_descSets[0] && m_pipelineLayout) {
    m_cmdManager->bind_descriptor_set(m_pipelineLayout, m_descSets[0], 0);
  }

  for (auto& draw : m_frameDraws[m_cmdManager->current_frame()]) {
    if (draw.pipeline) {
      m_cmdManager->bind_pipeline(draw.pipeline);
    }
    if (draw.vertexBufferDirect) {
      m_cmdManager->bind_vertex_buffer(draw.vertexBufferDirect, 0, draw.vertexBufferOffset);
    } else if (draw.vertexBuffer) {
      m_cmdManager->bind_vertex_buffer(draw.vertexBuffer->handle(), 0, 0);
    }
    if (draw.hasIndexBuffer) {
      m_cmdManager->bind_index_buffer(draw.indexBufferDirect, draw.indexType);
      push_mvp_constants();
      m_cmdManager->draw_indexed(draw.indexCount, 0, 0);
    } else {
      push_mvp_constants();
      m_cmdManager->draw(draw.vertexCount, draw.firstVertex);
    }
  }

  end_active_render_pass();

  m_cmdManager->end_frame();

  // Submit the command buffer to the graphics queue
  // Wait on acquireSem (image is ready), signal presentSem (rendering done)
  m_cmdManager->submit_frame(m_acquireSemaphore, m_presentSemaphore);

  // Present the image, waiting on presentSem
  m_vkSwapchain->present(m_presentSemaphore, imageIndex);

  // Advance to next frame
  m_cmdManager->advance_frame();

  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetBackBuffer(uint32_t iSwapChain, uint32_t iBackBuffer, uint32_t Type, IDirect3DSurface9** ppBackBuffer) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)iSwapChain; (void)iBackBuffer; (void)Type;
  if (!ppBackBuffer) return static_cast<int>(D3DERR_INVALIDCALL);
  // Return a surface representing the current back buffer
  // For now return nullptr - games should use GetRenderTarget instead
  *ppBackBuffer = nullptr;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetRasterStatus(uint32_t iSwapChain, void* pRasterStatus) {
  (void)iSwapChain; (void)pRasterStatus; return static_cast<int>(D3D_OK);
}

int D3D9Device::SetDialogBoxMode(bool bEnableDialogs) { (void)bEnableDialogs; return static_cast<int>(D3D_OK); }

void D3D9Device::SetGammaRamp(uint32_t iSwapChain, uint32_t Flags, const void* pRamp) {
  (void)iSwapChain; (void)Flags; (void)pRamp;
}

void D3D9Device::GetGammaRamp(uint32_t iSwapChain, void* pRamp) {
  (void)iSwapChain; (void)pRamp;
}

// --- Textures ---

int D3D9Device::CreateTexture(uint32_t Width, uint32_t Height, uint32_t Levels, uint32_t Usage,
                               D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, void* pSharedHandle) {
  (void)pSharedHandle;
  if (!ppTexture) return static_cast<int>(D3DERR_INVALIDCALL);
  VKWIND_DBG(kTag, "CreateTexture %ux%u, levels=%u, fmt=%d, pool=%d", Width, Height, Levels, Format, Pool);
  *ppTexture = new D3D9Texture(this, Width, Height, Levels, Usage, Format, Pool);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::CreateVolumeTexture(uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t Levels,
                                     uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                                     IDirect3DVolumeTexture9** ppVolumeTexture, void* pSharedHandle) {
  (void)pSharedHandle;
  if (!ppVolumeTexture) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppVolumeTexture = new D3D9VolumeTexture(this, Width, Height, Depth, Levels, Usage, Format, Pool);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::CreateCubeTexture(uint32_t EdgeLength, uint32_t Levels, uint32_t Usage,
                                   D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, void* pSharedHandle) {
  (void)pSharedHandle;
  if (!ppCubeTexture) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppCubeTexture = new D3D9CubeTexture(this, EdgeLength, Levels, Usage, Format, Pool);
  return static_cast<int>(D3D_OK);
}

// --- Buffers ---

int D3D9Device::CreateVertexBuffer(uint32_t Length, uint32_t Usage, uint32_t FVF, D3DPOOL Pool,
                                    IDirect3DVertexBuffer9** ppVertexBuffer, void* pSharedHandle) {
  (void)pSharedHandle;
  if (!ppVertexBuffer) return static_cast<int>(D3DERR_INVALIDCALL);
  VKWIND_DBG(kTag, "CreateVertexBuffer len=%u, usage=0x%x, fvf=0x%x, pool=%d", Length, Usage, FVF, Pool);
  *ppVertexBuffer = new D3D9VertexBuffer(this, Length, Usage, FVF, Pool);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::CreateIndexBuffer(uint32_t Length, uint32_t Usage, D3DFORMAT Format, D3DPOOL Pool,
                                   IDirect3DIndexBuffer9** ppIndexBuffer, void* pSharedHandle) {
  (void)pSharedHandle;
  if (!ppIndexBuffer) return static_cast<int>(D3DERR_INVALIDCALL);
  VKWIND_DBG(kTag, "CreateIndexBuffer len=%u, usage=0x%x, fmt=%d, pool=%d", Length, Usage, Format, Pool);
  *ppIndexBuffer = new D3D9IndexBuffer(this, Length, Usage, Format, Pool);
  return static_cast<int>(D3D_OK);
}

// --- Surfaces ---

int D3D9Device::CreateRenderTarget(uint32_t Width, uint32_t Height, D3DFORMAT Format,
                                    D3DMULTISAMPLE_TYPE MultiSample, uint32_t MultisampleQuality,
                                    bool Lockable, IDirect3DSurface9** ppSurface, void* pSharedHandle) {
  (void)MultiSample; (void)MultisampleQuality; (void)Lockable; (void)pSharedHandle;
  if (!ppSurface) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppSurface = new D3D9Surface(this, Width, Height, Format);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::CreateDepthStencilSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format,
                                           D3DMULTISAMPLE_TYPE MultiSample, uint32_t MultisampleQuality,
                                           bool Discard, IDirect3DSurface9** ppSurface, void* pSharedHandle) {
  (void)MultiSample; (void)MultisampleQuality; (void)Discard; (void)pSharedHandle;
  if (!ppSurface) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppSurface = new D3D9Surface(this, Width, Height, Format);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::CreateImageSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format, IDirect3DSurface9** ppSurface) {
  if (!ppSurface) return static_cast<int>(D3DERR_INVALIDCALL);
  VKWIND_DBG(kTag, "CreateImageSurface %ux%u, fmt=%d", Width, Height, Format);
  *ppSurface = new D3D9Surface(this, Width, Height, Format);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::CopyRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                          IDirect3DSurface9* pDestSurface, void* pDestPoint) {
  if (!pSourceSurface || !pDestSurface) return static_cast<int>(D3DERR_INVALIDCALL);

  D3DSURFACE_DESC srcDesc = {};
  pSourceSurface->GetDesc(&srcDesc);
  D3DSURFACE_DESC dstDesc = {};
  pDestSurface->GetDesc(&dstDesc);

  uint32_t bpp = 4;
  if (srcDesc.Format == D3DFMT_R8G8B8 || srcDesc.Format == D3DFMT_X8R8G8B8) bpp = 3;
  else if (srcDesc.Format == D3DFMT_R5G6B5 || srcDesc.Format == D3DFMT_X1R5G5B5 || srcDesc.Format == D3DFMT_A1R5G5B5) bpp = 2;

  uint32_t srcX = 0, srcY = 0;
  uint32_t dstX = 0, dstY = 0;
  uint32_t copyW = srcDesc.Width;
  uint32_t copyH = srcDesc.Height;

  // RECT layout: {left, top, right, bottom} as 4 longs
  if (pSourceRect) {
    const uint32_t* r = static_cast<const uint32_t*>(pSourceRect);
    srcX = r[0]; srcY = r[1];
    copyW = r[2] - r[0];
    copyH = r[3] - r[1];
  }
  // POINT layout: {x, y} as 2 longs
  if (pDestPoint) {
    const uint32_t* pt = static_cast<const uint32_t*>(pDestPoint);
    dstX = pt[0]; dstY = pt[1];
  }

  copyW = std::min(copyW, srcDesc.Width - srcX);
  copyH = std::min(copyH, srcDesc.Height - srcY);

  D3DLOCKED_RECT srcLocked = {};
  D3DLOCKED_RECT dstLocked = {};
  int srcRes = pSourceSurface->LockRect(&srcLocked, pSourceRect, D3DLOCK_READONLY);
  int dstRes = pDestSurface->LockRect(&dstLocked, nullptr, 0);

  if (SUCCEEDED(srcRes) && SUCCEEDED(dstRes)) {
    uint32_t rowBytes = copyW * bpp;
    for (uint32_t row = 0; row < copyH; row++) {
      memcpy(static_cast<uint8_t*>(dstLocked.pBits) + (dstY + row) * dstLocked.Pitch + dstX * bpp,
             static_cast<const uint8_t*>(srcLocked.pBits) + row * srcLocked.Pitch,
             rowBytes);
    }
    pDestSurface->UnlockRect();
  }
  if (SUCCEEDED(srcRes)) pSourceSurface->UnlockRect();

  return static_cast<int>(D3D_OK);
}

int D3D9Device::UpdateSurface(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                               IDirect3DSurface9* pDestSurface, void* pDestPoint) {
  if (!pSourceSurface || !pDestSurface) return static_cast<int>(D3DERR_INVALIDCALL);

  D3DSURFACE_DESC srcDesc = {};
  pSourceSurface->GetDesc(&srcDesc);
  D3DSURFACE_DESC dstDesc = {};
  pDestSurface->GetDesc(&dstDesc);

  uint32_t bpp = 4;
  if (srcDesc.Format == D3DFMT_R8G8B8 || srcDesc.Format == D3DFMT_X8R8G8B8) bpp = 3;
  else if (srcDesc.Format == D3DFMT_R5G6B5 || srcDesc.Format == D3DFMT_X1R5G5B5 || srcDesc.Format == D3DFMT_A1R5G5B5) bpp = 2;

  uint32_t copyWidth = std::min(srcDesc.Width, dstDesc.Width);
  uint32_t copyHeight = std::min(srcDesc.Height, dstDesc.Height);

  D3DLOCKED_RECT srcLocked = {};
  D3DLOCKED_RECT dstLocked = {};
  int srcResult = pSourceSurface->LockRect(&srcLocked, nullptr, D3DLOCK_READONLY);
  int dstResult = pDestSurface->LockRect(&dstLocked, nullptr, 0);

  if (SUCCEEDED(srcResult) && SUCCEEDED(dstResult)) {
    uint32_t rowBytes = copyWidth * bpp;
    for (uint32_t row = 0; row < copyHeight; row++) {
      memcpy(static_cast<uint8_t*>(dstLocked.pBits) + row * dstLocked.Pitch,
             static_cast<const uint8_t*>(srcLocked.pBits) + row * srcLocked.Pitch,
             rowBytes);
    }
    pDestSurface->UnlockRect();
  }
  if (SUCCEEDED(srcResult)) pSourceSurface->UnlockRect();

  VKWIND_DBG(kTag, "UpdateSurface: copied %ux%u", copyWidth, copyHeight);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) {
  if (!pSourceTexture || !pDestinationTexture) return static_cast<int>(D3DERR_INVALIDCALL);
  if (pSourceTexture->GetType() != D3DRTYPE_TEXTURE || pDestinationTexture->GetType() != D3DRTYPE_TEXTURE)
    return static_cast<int>(D3DERR_INVALIDCALL);

  auto* src = static_cast<D3D9Texture*>(pSourceTexture);
  auto* dst = static_cast<D3D9Texture*>(pDestinationTexture);

  uint32_t levels = std::min(src->levels(), dst->levels());
  for (uint32_t level = 0; level < levels; level++) {
    uint32_t w = std::max(1u, src->width() >> level);
    uint32_t h = std::max(1u, src->height() >> level);

    D3DLOCKED_RECT srcLocked = {};
    if (SUCCEEDED(src->LockRect(level, &srcLocked, nullptr, D3DLOCK_READONLY))) {
      D3DLOCKED_RECT dstLocked = {};
      if (SUCCEEDED(dst->LockRect(level, &dstLocked, nullptr, 0))) {
        uint32_t bpp = 4;
        D3DFORMAT fmt = src->format();
        if (fmt == D3DFMT_R8G8B8 || fmt == D3DFMT_X8R8G8B8) bpp = 3;
        else if (fmt == D3DFMT_R5G6B5 || fmt == D3DFMT_X1R5G5B5 || fmt == D3DFMT_A1R5G5B5) bpp = 2;

        uint32_t rowBytes = w * bpp;
        for (uint32_t row = 0; row < h; row++) {
          memcpy(static_cast<uint8_t*>(dstLocked.pBits) + row * dstLocked.Pitch,
                 static_cast<const uint8_t*>(srcLocked.pBits) + row * srcLocked.Pitch,
                 rowBytes);
        }
        dst->UnlockRect(level);
      }
      src->UnlockRect(level);
    }
  }

  VKWIND_DBG(kTag, "UpdateTexture: copied %u levels from %p to %p", levels, src, dst);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pRenderTarget || !pDestSurface) return static_cast<int>(D3DERR_INVALIDCALL);

  // Read render target pixels to CPU via surface Lock/Unlock
  D3DSURFACE_DESC rtDesc = {};
  pRenderTarget->GetDesc(&rtDesc);
  D3DSURFACE_DESC dstDesc = {};
  pDestSurface->GetDesc(&dstDesc);

  D3DLOCKED_RECT srcLocked = {};
  D3DLOCKED_RECT dstLocked = {};
  int srcRes = pRenderTarget->LockRect(&srcLocked, nullptr, D3DLOCK_READONLY);
  int dstRes = pDestSurface->LockRect(&dstLocked, nullptr, 0);

  if (SUCCEEDED(srcRes) && SUCCEEDED(dstRes)) {
    uint32_t bpp = 4;
    if (rtDesc.Format == D3DFMT_R8G8B8 || rtDesc.Format == D3DFMT_X8R8G8B8) bpp = 3;
    else if (rtDesc.Format == D3DFMT_R5G6B5 || rtDesc.Format == D3DFMT_X1R5G5B5 || rtDesc.Format == D3DFMT_A1R5G5B5) bpp = 2;

    uint32_t copyW = std::min(rtDesc.Width, dstDesc.Width);
    uint32_t copyH = std::min(rtDesc.Height, dstDesc.Height);
    uint32_t rowBytes = copyW * bpp;
    for (uint32_t row = 0; row < copyH; row++) {
      memcpy(static_cast<uint8_t*>(dstLocked.pBits) + row * dstLocked.Pitch,
             static_cast<const uint8_t*>(srcLocked.pBits) + row * srcLocked.Pitch,
             rowBytes);
    }
    pDestSurface->UnlockRect();
  }
  if (SUCCEEDED(srcRes)) pRenderTarget->UnlockRect();

  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetFrontBufferData(uint32_t iSwapChain, IDirect3DSurface9* pDestSurface) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)iSwapChain;
  if (!pDestSurface) return static_cast<int>(D3DERR_INVALIDCALL);

  // Copy current render target contents to destination surface
  // This is a CPU-side copy from the back buffer's locked data
  if (m_renderTargets[0]) {
    D3DSURFACE_DESC srcDesc = {};
    m_renderTargets[0]->GetDesc(&srcDesc);

    D3DLOCKED_RECT srcLocked = {};
    int srcRes = m_renderTargets[0]->LockRect(&srcLocked, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(srcRes)) {
      D3DLOCKED_RECT dstLocked = {};
      int dstRes = pDestSurface->LockRect(&dstLocked, nullptr, 0);
      if (SUCCEEDED(dstRes)) {
        uint32_t bpp = 4;
        if (srcDesc.Format == D3DFMT_R8G8B8 || srcDesc.Format == D3DFMT_X8R8G8B8) bpp = 3;
        else if (srcDesc.Format == D3DFMT_R5G6B5 || srcDesc.Format == D3DFMT_X1R5G5B5 || srcDesc.Format == D3DFMT_A1R5G5B5) bpp = 2;

        uint32_t width = srcDesc.Width;
        uint32_t height = srcDesc.Height;
        uint32_t srcPitch = srcLocked.Pitch;
        uint32_t dstPitch = dstLocked.Pitch;
        uint32_t rowBytes = width * bpp;

        const uint8_t* srcRow = static_cast<const uint8_t*>(srcLocked.pBits);
        uint8_t* dstRow = static_cast<uint8_t*>(dstLocked.pBits);
        for (uint32_t y = 0; y < height; ++y) {
          memcpy(dstRow, srcRow, rowBytes);
          srcRow += srcPitch;
          dstRow += dstPitch;
        }
        pDestSurface->UnlockRect();
      }
      m_renderTargets[0]->UnlockRect();
    }
  } else {
    // No render target bound — fill destination with black
    D3DLOCKED_RECT dstLocked = {};
    int dstRes = pDestSurface->LockRect(&dstLocked, nullptr, 0);
    if (SUCCEEDED(dstRes)) {
      D3DSURFACE_DESC dstDesc = {};
      pDestSurface->GetDesc(&dstDesc);
      uint32_t height = dstDesc.Height;
      for (uint32_t y = 0; y < height; ++y) {
        memset(static_cast<uint8_t*>(dstLocked.pBits) + y * dstLocked.Pitch, 0, dstLocked.Pitch);
      }
      pDestSurface->UnlockRect();
    }
  }

  VKWIND_DBG(kTag, "GetFrontBufferData: copied front buffer");
  return static_cast<int>(D3D_OK);
}

int D3D9Device::StretchRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                             IDirect3DSurface9* pDestSurface, const void* pDestRect, uint32_t Filter) {
  if (!pSourceSurface || !pDestSurface) return static_cast<int>(D3DERR_INVALIDCALL);
  (void)Filter;

  D3DSURFACE_DESC srcDesc = {};
  pSourceSurface->GetDesc(&srcDesc);
  D3DSURFACE_DESC dstDesc = {};
  pDestSurface->GetDesc(&dstDesc);

  uint32_t bpp = 4;
  if (srcDesc.Format == D3DFMT_R8G8B8 || srcDesc.Format == D3DFMT_X8R8G8B8) bpp = 3;
  else if (srcDesc.Format == D3DFMT_R5G6B5 || srcDesc.Format == D3DFMT_X1R5G5B5 || srcDesc.Format == D3DFMT_A1R5G5B5) bpp = 2;

  D3DLOCKED_RECT srcLocked = {};
  D3DLOCKED_RECT dstLocked = {};
  int srcRes = pSourceSurface->LockRect(&srcLocked, nullptr, D3DLOCK_READONLY);
  int dstRes = pDestSurface->LockRect(&dstLocked, nullptr, 0);

  if (SUCCEEDED(srcRes) && SUCCEEDED(dstRes)) {
    uint32_t copyW = std::min(srcDesc.Width, dstDesc.Width);
    uint32_t copyH = std::min(srcDesc.Height, dstDesc.Height);
    uint32_t rowBytes = copyW * bpp;
    for (uint32_t row = 0; row < copyH; row++) {
      memcpy(static_cast<uint8_t*>(dstLocked.pBits) + row * dstLocked.Pitch,
             static_cast<const uint8_t*>(srcLocked.pBits) + row * srcLocked.Pitch,
             rowBytes);
    }
    pDestSurface->UnlockRect();
  }
  if (SUCCEEDED(srcRes)) pSourceSurface->UnlockRect();

  return static_cast<int>(D3D_OK);
}

int D3D9Device::ColorFill(IDirect3DSurface9* pSurface, const void* pRect, uint32_t Color) {
  if (!pSurface) return static_cast<int>(D3DERR_INVALIDCALL);

  D3DSURFACE_DESC desc = {};
  pSurface->GetDesc(&desc);

  uint32_t bpp = 4;
  if (desc.Format == D3DFMT_R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) bpp = 3;
  else if (desc.Format == D3DFMT_R5G6B5 || desc.Format == D3DFMT_X1R5G5B5 || desc.Format == D3DFMT_A1R5G5B5) bpp = 2;

  D3DLOCKED_RECT locked = {};
  int res = pSurface->LockRect(&locked, pRect, 0);
  if (FAILED(res)) return res;

  uint32_t fillW = desc.Width;
  uint32_t fillH = desc.Height;
  if (pRect) {
    const uint32_t* r = static_cast<const uint32_t*>(pRect);
    fillW = r[2] - r[0];
    fillH = r[3] - r[1];
  }

  for (uint32_t row = 0; row < fillH; row++) {
    uint8_t* dst = static_cast<uint8_t*>(locked.pBits) + row * locked.Pitch;
    for (uint32_t col = 0; col < fillW; col++) {
      memcpy(dst + col * bpp, &Color, bpp);
    }
  }

  pSurface->UnlockRect();
  return static_cast<int>(D3D_OK);
}

int D3D9Device::CreateOffscreenPlainSurface(uint32_t Width, uint32_t Height, D3DFORMAT Format,
                                             D3DPOOL Pool, IDirect3DSurface9** ppSurface, void* pSharedHandle) {
  (void)Pool; (void)pSharedHandle;
  if (!ppSurface) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppSurface = new D3D9Surface(this, Width, Height, Format);
  return static_cast<int>(D3D_OK);
}

// --- Render targets ---

void D3D9Device::SetRenderTarget(uint32_t RenderTargetIndex, IDirect3DSurface9* pRenderTarget) {
  if (RenderTargetIndex > 3) return;
  m_renderTargets[RenderTargetIndex] = pRenderTarget;
  if (RenderTargetIndex == 0 && m_renderPassActive) {
    end_active_render_pass();
  }
  VKWIND_DBG(kTag, "SetRenderTarget index=%u surf=%p", RenderTargetIndex, (void*)pRenderTarget);
}

void D3D9Device::GetRenderTarget(uint32_t RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) {
  if (RenderTargetIndex > 3 || !ppRenderTarget) return;
  *ppRenderTarget = m_renderTargets[RenderTargetIndex];
}

void D3D9Device::SetDepthStencilSurface(IDirect3DSurface9* pZStencilSurface) {
  m_depthStencilSurface = pZStencilSurface;
  VKWIND_DBG(kTag, "SetDepthStencilSurface surf=%p", (void*)pZStencilSurface);
}

void D3D9Device::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) {
  if (ppZStencilSurface) *ppZStencilSurface = m_depthStencilSurface;
}

// --- Scene ---

int D3D9Device::BeginScene() {
  std::lock_guard<std::mutex> lock(m_mutex);
  VKWIND_DBG(kTag, "BeginScene");
  m_inScene = true;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::EndScene() {
  std::lock_guard<std::mutex> lock(m_mutex);
  VKWIND_DBG(kTag, "EndScene");
  m_inScene = false;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::Clear(uint32_t Count, const void* pRects, uint32_t Flags, uint32_t Color, float Z, uint32_t Stencil) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)Count; (void)pRects;

  // Store the clear color for the next render pass
  m_clearFlags = Flags;
  m_clearColor[0] = ((Color >> 16) & 0xFF) / 255.0f;
  m_clearColor[1] = ((Color >> 8) & 0xFF) / 255.0f;
  m_clearColor[2] = (Color & 0xFF) / 255.0f;
  m_clearColor[3] = ((Color >> 24) & 0xFF) / 255.0f;
  m_clearDepth = Z;
  m_clearStencil = Stencil;
  m_clearPending = true;

  // If render pass is active, end it so next ensure starts fresh with new clear color
  if (m_renderPassActive) {
    end_active_render_pass();
  }

  return static_cast<int>(D3D_OK);
}

// --- Transforms ---

int D3D9Device::SetTransform(uint32_t State, const D3DMATRIX* pMatrix) {
  if (!pMatrix) return static_cast<int>(D3DERR_INVALIDCALL);
  if (State < 300) {
    m_transforms[State] = *pMatrix;
    m_transformDirty = true;
  }
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetTransform(uint32_t State, D3DMATRIX* pMatrix) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pMatrix) return static_cast<int>(D3DERR_INVALIDCALL);
  if (State < 300) *pMatrix = m_transforms[State];
  return static_cast<int>(D3D_OK);
}

int D3D9Device::MultiplyTransform(uint32_t State, const D3DMATRIX* pMatrix) {
  if (!pMatrix || State >= 300) return static_cast<int>(D3DERR_INVALIDCALL);
  auto& cur = m_transforms[State];
  D3DMATRIX result = {};
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        sum += cur.m[r][k] * pMatrix->m[k][c];
      }
      result.m[r][c] = sum;
    }
  }
  cur = result;
  m_transformDirty = true;
  return static_cast<int>(D3D_OK);
}

// --- Viewport ---

void D3D9Device::SetViewport(const D3DVIEWPORT9* pViewport) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pViewport) return;
  m_viewport = *pViewport;

  if (m_renderPassActive && m_cmdManager) {
    uint32_t vpW = pViewport->Width ? static_cast<uint32_t>(pViewport->Width) : m_vkSwapchain->width();
    uint32_t vpH = pViewport->Height ? static_cast<uint32_t>(pViewport->Height) : m_vkSwapchain->height();
    float minZ = pViewport->MinZ;
    float maxZ = pViewport->MaxZ;
    if (maxZ <= minZ) maxZ = 1.0f;
    m_cmdManager->set_viewport(static_cast<float>(pViewport->X), static_cast<float>(pViewport->Y),
      static_cast<float>(vpW), static_cast<float>(vpH), minZ, maxZ);
  }

  VKWIND_DBG(kTag, "SetViewport (%u,%u) %ux%u", pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height);
}

void D3D9Device::GetViewport(D3DVIEWPORT9* pViewport) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (pViewport) *pViewport = m_viewport;
}

// --- Material ---

void D3D9Device::SetMaterial(const D3DMATERIAL9* pMaterial) {
  if (pMaterial) m_material = *pMaterial;
}

void D3D9Device::GetMaterial(D3DMATERIAL9* pMaterial) {
  if (pMaterial) *pMaterial = m_material;
}

// --- Lights ---

void D3D9Device::SetLight(uint32_t Index, const D3DLIGHT9* pLight) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (Index >= 8 || !pLight) return;
  m_lights[Index].light = *pLight;
  m_lights[Index].defined = true;
  VKWIND_DBG(kTag, "SetLight %u, type=%d", Index, pLight->Type);
}

void D3D9Device::GetLight(uint32_t Index, D3DLIGHT9* pLight) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (Index >= 8 || !pLight) return;
  if (m_lights[Index].defined) {
    *pLight = m_lights[Index].light;
  } else {
    memset(pLight, 0, sizeof(D3DLIGHT9));
    pLight->Type = 1; // D3DLIGHT_DIRECTIONAL
    pLight->Diffuse[0] = pLight->Diffuse[1] = pLight->Diffuse[2] = 1.0f;
    pLight->Ambient[0] = pLight->Ambient[1] = pLight->Ambient[2] = 0.2f;
    pLight->Direction[2] = 1.0f;
    pLight->Range = 1000.0f;
  }
}

int D3D9Device::LightEnable(uint32_t Index, bool Enable) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (Index >= 8) return static_cast<int>(D3DERR_INVALIDCALL);
  m_lights[Index].enabled = Enable;
  VKWIND_DBG(kTag, "LightEnable %u = %d", Index, Enable);
  return static_cast<int>(D3D_OK);
}

void D3D9Device::GetLightEnable(uint32_t Index, bool* pEnable) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (Index >= 8) return;
  if (pEnable) *pEnable = m_lights[Index].enabled;
}

// --- Clip planes ---

int D3D9Device::SetClipPlane(uint32_t Index, const float* pPlane) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (Index >= 6 || !pPlane) return static_cast<int>(D3DERR_INVALIDCALL);
  memcpy(m_clipPlanes[Index], pPlane, 4 * sizeof(float));
  VKWIND_DBG(kTag, "SetClipPlane %u: [%f %f %f %f]", Index, pPlane[0], pPlane[1], pPlane[2], pPlane[3]);
  return static_cast<int>(D3D_OK);
}

void D3D9Device::GetClipPlane(uint32_t Index, float* pPlane) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (Index >= 6 || !pPlane) return;
  memcpy(pPlane, m_clipPlanes[Index], 4 * sizeof(float));
}

// --- Render state ---

int D3D9Device::SetRenderState(uint32_t State, uint32_t Value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (State >= 256) return static_cast<int>(D3DERR_INVALIDCALL);
  m_renderStates[State] = Value;
  apply_render_state(State, Value);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetRenderState(uint32_t State, uint32_t* pValue) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pValue || State >= 256) return static_cast<int>(D3DERR_INVALIDCALL);
  *pValue = m_renderStates[State];
  return static_cast<int>(D3D_OK);
}

// --- State blocks ---

int D3D9Device::BeginStateBlock() { return static_cast<int>(D3D_OK); }
int D3D9Device::EndStateBlock(uint32_t* pToken) { if (pToken) *pToken = 0; return static_cast<int>(D3D_OK); }

// --- Clip status ---

int D3D9Device::CreateClipStatus(void** ppClipStatus) { (void)ppClipStatus; return static_cast<int>(D3D_OK); }
int D3D9Device::GetClipStatus(void* pClipStatus) { (void)pClipStatus; return static_cast<int>(D3D_OK); }

// --- Textures ---

int D3D9Device::GetTexture(uint32_t Stage, IDirect3DBaseTexture9** ppTexture) {
  if (Stage >= 8 || !ppTexture) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppTexture = m_textures[Stage];
  if (*ppTexture) (*ppTexture)->AddRef();
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetTexture(uint32_t Stage, IDirect3DBaseTexture9* pTexture) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (Stage >= 8) return static_cast<int>(D3DERR_INVALIDCALL);
  m_textures[Stage] = pTexture;
  m_texturesDirty = true;
  m_shadersDirty = true;
  m_pipelineDirty = true;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t* pValue) {
  if (Stage >= 8 || !pValue || Type >= 32) return static_cast<int>(D3DERR_INVALIDCALL);
  *pValue = m_textureStageStates[Stage][Type];
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetTextureStageState(uint32_t Stage, uint32_t Type, uint32_t Value) {
  if (Stage >= 8 || Type >= 32) return static_cast<int>(D3DERR_INVALIDCALL);
  VKWIND_DBG(kTag, "SetTextureStageState s%d, type=%u, value=%u", Stage, Type, Value);
  m_textureStageStates[Stage][Type] = Value;
  m_shadersDirty = true;
  m_pipelineDirty = true;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetSamplerState(uint32_t Sampler, uint32_t Type, uint32_t* pValue) {
  if (Sampler >= 8 || !pValue) return static_cast<int>(D3DERR_INVALIDCALL);
  auto& ss = m_samplerStates[Sampler];
  switch (Type) {
    case 5: *pValue = ss.magFilter; break;   // D3DSAMP_MAGFILTER
    case 6: *pValue = ss.minFilter; break;   // D3DSAMP_MINFILTER
    case 7: *pValue = ss.mipFilter; break;   // D3DSAMP_MIPFILTER
    case 1: *pValue = ss.addressU; break;    // D3DSAMP_ADDRESSU
    case 2: *pValue = ss.addressV; break;    // D3DSAMP_ADDRESSV
    case 3: *pValue = ss.addressW; break;    // D3DSAMP_ADDRESSW
    case 9: *pValue = ss.maxMipLevel; break; // D3DSAMP_MAXMIPLEVEL
    case 10: *pValue = ss.maxAnisotropy; break; // D3DSAMP_MAXANISOTROPY
    default: *pValue = 0; break;
  }
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetSamplerState(uint32_t Sampler, uint32_t Type, uint32_t Value) {
  if (Sampler >= 8) return static_cast<int>(D3DERR_INVALIDCALL);
  auto& ss = m_samplerStates[Sampler];
  switch (Type) {
    case 5: ss.magFilter = Value; break;     // D3DSAMP_MAGFILTER
    case 6: ss.minFilter = Value; break;     // D3DSAMP_MINFILTER
    case 7: ss.mipFilter = Value; break;     // D3DSAMP_MIPFILTER
    case 1: ss.addressU = Value; break;      // D3DSAMP_ADDRESSU
    case 2: ss.addressV = Value; break;      // D3DSAMP_ADDRESSV
    case 3: ss.addressW = Value; break;      // D3DSAMP_ADDRESSW
    case 9: ss.maxMipLevel = Value; break;   // D3DSAMP_MAXMIPLEVEL
    case 10: ss.maxAnisotropy = Value; break; // D3DSAMP_MAXANISOTROPY
  }

  // Look up or create sampler in cache
  SamplerCacheKey key = {ss.magFilter, ss.minFilter, ss.mipFilter,
    ss.addressU, ss.addressV, ss.addressW, ss.maxAnisotropy};
  auto it = m_samplerCache.find(key);
  if (it != m_samplerCache.end()) {
    m_samplers[Sampler] = it->second;
  } else {
    auto dev = m_vkDevice->raw();
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = StateMapper::map_filter(static_cast<D3DTEXTUREFILTERTYPE>(ss.magFilter));
    samplerInfo.minFilter = StateMapper::map_filter(static_cast<D3DTEXTUREFILTERTYPE>(ss.minFilter));
    samplerInfo.mipmapMode = (ss.mipFilter == 1) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = StateMapper::map_address_mode(static_cast<D3DTEXTUREADDRESS>(ss.addressU));
    samplerInfo.addressModeV = StateMapper::map_address_mode(static_cast<D3DTEXTUREADDRESS>(ss.addressV));
    samplerInfo.addressModeW = StateMapper::map_address_mode(static_cast<D3DTEXTUREADDRESS>(ss.addressW));
    samplerInfo.anisotropyEnable = (ss.maxAnisotropy > 1) ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = static_cast<float>(ss.maxAnisotropy);
    samplerInfo.maxLod = 1000.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler newSampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(dev, &samplerInfo, nullptr, &newSampler);
    if (result == VK_SUCCESS) {
      m_samplerCache[key] = newSampler;
      m_samplers[Sampler] = newSampler;
    } else {
      VKWIND_ERR(kTag, "Failed to create sampler for stage %u: %d", Sampler, result);
    }
  }

  m_texturesDirty = true;

  VKWIND_DBG(kTag, "SetSamplerState s%d, type=%u, value=%u", Sampler, Type, Value);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::ValidateDevice(uint32_t* pNumPasses) {
  if (pNumPasses) *pNumPasses = 1;
  return static_cast<int>(D3D_OK);
}

// --- Palette ---

int D3D9Device::SetPaletteEntries(uint32_t PaletteNumber, const void* pEntries) {
  (void)PaletteNumber; (void)pEntries; return static_cast<int>(D3D_OK);
}

int D3D9Device::GetPaletteEntries(uint32_t PaletteNumber, void* pEntries) {
  (void)PaletteNumber; (void)pEntries; return static_cast<int>(D3D_OK);
}

int D3D9Device::SetCurrentTexturePalette(uint32_t PaletteNumber) {
  (void)PaletteNumber; return static_cast<int>(D3D_OK);
}

int D3D9Device::GetCurrentTexturePalette(uint32_t* PaletteNumber) {
  if (PaletteNumber) *PaletteNumber = 0;
  return static_cast<int>(D3D_OK);
}

// --- Scissor ---

int D3D9Device::SetScissorRect(const void* pRect) {
  if (!pRect) return static_cast<int>(D3D_OK);
  const int32_t* r = static_cast<const int32_t*>(pRect);
  m_scissor.left   = r[0];
  m_scissor.top    = r[1];
  m_scissor.right  = r[2];
  m_scissor.bottom = r[3];
  m_scissorEnabled = true;

  if (m_renderPassActive && m_cmdManager) {
    uint32_t w = static_cast<uint32_t>(m_scissor.right - m_scissor.left);
    uint32_t h = static_cast<uint32_t>(m_scissor.bottom - m_scissor.top);
    if (w > 0 && h > 0) {
      m_cmdManager->set_scissor(m_scissor.left, m_scissor.top, w, h);
    }
  }
  return static_cast<int>(D3D_OK);
}

void D3D9Device::GetScissorRect(void* pRect) {
  if (!pRect) return;
  int32_t* r = static_cast<int32_t*>(pRect);
  if (m_scissorEnabled) {
    r[0] = m_scissor.left;
    r[1] = m_scissor.top;
    r[2] = m_scissor.right;
    r[3] = m_scissor.bottom;
  } else {
    r[0] = r[1] = r[2] = r[3] = 0;
  }
}

// --- Software vertex processing ---

int D3D9Device::SetSoftwareVertexProcessing(bool bSoftware) { (void)bSoftware; return static_cast<int>(D3D_OK); }
bool D3D9Device::GetSoftwareVertexProcessing() { return false; }

// --- NPatch ---

int D3D9Device::SetNPatchMode(float nSegments) { (void)nSegments; return static_cast<int>(D3D_OK); }
float D3D9Device::GetNPatchMode() { return 0.0f; }

// --- Drawing ---

int D3D9Device::DrawPrimitive(uint32_t PrimitiveType, uint32_t StartVertex, uint32_t PrimitiveCount) {
  if (!m_cmdManager) return static_cast<int>(D3DERR_DEVICELOST);

  ensure_render_pass_active();

  ::VkPipeline pipeline = create_or_get_pipeline();
  if (pipeline) {
    m_cmdManager->bind_pipeline(pipeline);
  }

  auto& ss0 = m_streamSources[0];
  if (ss0.buffer) {
    auto* vb = static_cast<D3D9VertexBuffer*>(ss0.buffer);
    vb->ensure_gpu_buffer(*m_cmdManager);
    if (vb->gpu_buffer()) {
      m_cmdManager->bind_vertex_buffer(vb->gpu_buffer(), 0, ss0.offset);
    }
  }

  // Bind additional vertex streams (1-15)
  for (uint32_t s = 1; s < 16; s++) {
    auto& ss = m_streamSources[s];
    if (ss.buffer) {
      auto* vb = static_cast<D3D9VertexBuffer*>(ss.buffer);
      vb->ensure_gpu_buffer(*m_cmdManager);
      if (vb->gpu_buffer()) {
        m_cmdManager->bind_vertex_buffer(vb->gpu_buffer(), s, ss.offset);
      }
    }
  }

  push_mvp_constants();
  m_cmdManager->draw(PrimitiveCount * vertex_count_per_primitive(PrimitiveType), StartVertex);

  VKWIND_DBG(kTag, "DrawPrimitive type=%u, start=%u, count=%u", PrimitiveType, StartVertex, PrimitiveCount);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::DrawIndexedPrimitive(uint32_t PrimitiveType, int BaseVertexIndex, uint32_t MinVertexIndex,
                                     uint32_t NumVertexIndices, uint32_t StartIndex, uint32_t PrimitiveCount) {
  if (!m_cmdManager) return static_cast<int>(D3DERR_DEVICELOST);

  ensure_render_pass_active();

  ::VkPipeline pipeline = create_or_get_pipeline();
  if (pipeline) {
    m_cmdManager->bind_pipeline(pipeline);
  }

  auto& ss0 = m_streamSources[0];
  if (ss0.buffer) {
    auto* vb = static_cast<D3D9VertexBuffer*>(ss0.buffer);
    vb->ensure_gpu_buffer(*m_cmdManager);
    if (vb->gpu_buffer()) {
      m_cmdManager->bind_vertex_buffer(vb->gpu_buffer(), 0, ss0.offset);
    }
  }

  // Bind additional vertex streams (1-15)
  for (uint32_t s = 1; s < 16; s++) {
    auto& ss = m_streamSources[s];
    if (ss.buffer) {
      auto* vb = static_cast<D3D9VertexBuffer*>(ss.buffer);
      vb->ensure_gpu_buffer(*m_cmdManager);
      if (vb->gpu_buffer()) {
        m_cmdManager->bind_vertex_buffer(vb->gpu_buffer(), s, ss.offset);
      }
    }
  }

  if (m_indexBuffer) {
    auto* ib = static_cast<D3D9IndexBuffer*>(m_indexBuffer);
    ib->ensure_gpu_buffer(*m_cmdManager);
    if (ib->gpu_buffer()) {
      m_cmdManager->bind_index_buffer(ib->gpu_buffer(), ib->index_type());
    }
  }

  uint32_t indexCount = index_count_for_prims(PrimitiveType, PrimitiveCount);
  push_mvp_constants();
  m_cmdManager->draw_indexed(indexCount, StartIndex, BaseVertexIndex);

  VKWIND_DBG(kTag, "DrawIndexedPrimitive type=%u, indexCount=%u, start=%u", PrimitiveType, indexCount, StartIndex);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::DrawPrimitiveUP(uint32_t PrimitiveType, uint32_t PrimitiveCount,
                                const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) {
  if (!m_cmdManager) return static_cast<int>(D3DERR_DEVICELOST);

  ensure_render_pass_active();

  uint32_t vertsPerPrim = vertex_count_per_primitive(PrimitiveType);
  uint32_t vertexCount = PrimitiveCount * vertsPerPrim;
  uint32_t dataSize = vertexCount * VertexStreamZeroStride;

  if (m_vkDevice && dataSize > 0 && m_upVertexBuffer) {
    // Ring buffer sub-allocation
    VkDeviceSize alignedSize = (dataSize + kUpAlign - 1) & ~(kUpAlign - 1);
    if (m_upVertexWriteOffset + alignedSize > kUpBufferSize) {
      m_upVertexWriteOffset = 0; // Wrap (safe: frame fence waited before next frame)
    }
    VkDeviceSize writeOffset = m_upVertexWriteOffset;
    m_upVertexWriteOffset += alignedSize;

    // Write directly into ring buffer (HOST_VISIBLE | HOST_COHERENT)
    void* mapped = m_upVertexBuffer->map();
    if (mapped) {
      memcpy(static_cast<char*>(mapped) + writeOffset, pVertexStreamZeroData, dataSize);
      m_upVertexBuffer->unmap();

      PendingDrawUP draw;
      draw.pipeline = create_or_get_pipeline();
      draw.vertexBufferDirect = m_upVertexBuffer->handle();
      draw.vertexBufferOffset = writeOffset;
      draw.vertexCount = vertexCount;
      draw.firstVertex = 0;
      m_pendingDraws.push_back(std::move(draw));
    }
  }

  VKWIND_DBG(kTag, "DrawPrimitiveUP type=%u, count=%u, stride=%u, dataSize=%u",
    PrimitiveType, PrimitiveCount, VertexStreamZeroStride, dataSize);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::DrawIndexedPrimitiveUP(uint32_t PrimitiveType, uint32_t MinVertexIndex, uint32_t NumVertexIndices,
                                       uint32_t PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat,
                                       const void* pVertexStreamZeroData, uint32_t VertexStreamZeroStride) {
  if (!m_cmdManager) return static_cast<int>(D3DERR_DEVICELOST);

  ensure_render_pass_active();

  ::VkPipeline pipeline = create_or_get_pipeline();

  uint32_t indexCount = index_count_for_prims(PrimitiveType, PrimitiveCount);
  uint32_t indexSize = (IndexDataFormat == D3DFMT_INDEX16) ? 2 : 4;
  uint32_t indexDataSize = indexCount * indexSize;
  uint32_t vertexDataSize = NumVertexIndices * VertexStreamZeroStride;

  if (m_vkDevice && m_upVertexBuffer && m_upIndexBuffer) {
    // Upload vertex data via ring buffer
    VkDeviceSize vAligned = (vertexDataSize + kUpAlign - 1) & ~(kUpAlign - 1);
    if (m_upVertexWriteOffset + vAligned > kUpBufferSize) {
      m_upVertexWriteOffset = 0;
    }
    VkDeviceSize vWriteOffset = m_upVertexWriteOffset;
    m_upVertexWriteOffset += vAligned;

    void* vMapped = m_upVertexBuffer->map();
    if (vMapped) {
      memcpy(static_cast<char*>(vMapped) + vWriteOffset, pVertexStreamZeroData, vertexDataSize);
      m_upVertexBuffer->unmap();

      // Upload index data via ring buffer
      VkDeviceSize iAligned = (indexDataSize + kUpAlign - 1) & ~(kUpAlign - 1);
      if (m_upIndexWriteOffset + iAligned > kUpBufferSize) {
        m_upIndexWriteOffset = 0;
      }
      VkDeviceSize iWriteOffset = m_upIndexWriteOffset;
      m_upIndexWriteOffset += iAligned;

      void* iMapped = m_upIndexBuffer->map();
      if (iMapped) {
        memcpy(static_cast<char*>(iMapped) + iWriteOffset, pIndexData, indexDataSize);
        m_upIndexBuffer->unmap();

        PendingDrawUP draw;
        draw.pipeline = pipeline;
        draw.vertexBufferDirect = m_upVertexBuffer->handle();
        draw.vertexBufferOffset = vWriteOffset;
        draw.vertexCount = NumVertexIndices;
        draw.firstVertex = 0;
        draw.indexBufferDirect = m_upIndexBuffer->handle();
        draw.indexBufferOffset = iWriteOffset;
        draw.indexCount = indexCount;
        draw.indexType = (IndexDataFormat == D3DFMT_INDEX16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        draw.hasIndexBuffer = true;
        m_pendingDraws.push_back(std::move(draw));
      }
    }
  }

  VKWIND_DBG(kTag, "DrawIndexedPrimitiveUP type=%u, primCount=%u", PrimitiveType, PrimitiveCount);
  return static_cast<int>(D3D_OK);
}

// --- Process vertices ---

int D3D9Device::ProcessVertices(uint32_t SrcStartIndex, uint32_t DestIndex, uint32_t VertexCount,
                                 IDirect3DVertexBuffer9* pDestBuffer, void* pVertexDecl, uint32_t Flags) {
  (void)SrcStartIndex; (void)DestIndex; (void)VertexCount;
  (void)pDestBuffer; (void)pVertexDecl; (void)Flags;
  return static_cast<int>(D3D_OK);
}

// --- Stream source ---

int D3D9Device::SetStreamSource(uint32_t StreamNumber, IDirect3DVertexBuffer9* pStreamData,
                                 uint32_t OffsetInBytes, uint32_t Stride) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (StreamNumber >= 16) return static_cast<int>(D3DERR_INVALIDCALL);
  m_streamSources[StreamNumber].buffer = pStreamData;
  m_streamSources[StreamNumber].offset = OffsetInBytes;
  m_streamSources[StreamNumber].stride = Stride;

  if (StreamNumber == 0) {
    m_pipelineDirty = true;
  }

  return static_cast<int>(D3D_OK);
}

void D3D9Device::GetStreamSource(uint32_t StreamNumber, IDirect3DVertexBuffer9** ppStreamData,
                                  uint32_t* pOffsetInBytes, uint32_t* pStride) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (StreamNumber < 16) {
    if (ppStreamData) *ppStreamData = m_streamSources[StreamNumber].buffer;
    if (pOffsetInBytes) *pOffsetInBytes = m_streamSources[StreamNumber].offset;
    if (pStride) *pStride = m_streamSources[StreamNumber].stride;
  }
}

int D3D9Device::SetStreamSourceFreq(uint32_t StreamNumber, uint32_t Setting) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)StreamNumber; (void)Setting; return static_cast<int>(D3D_OK);
}

void D3D9Device::GetStreamSourceFreq(uint32_t StreamNumber, uint32_t* pSetting) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)StreamNumber; if (pSetting) *pSetting = 1;
}

int D3D9Device::SetIndices(IDirect3DIndexBuffer9* pIndexData) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_indexBuffer = pIndexData;
  return static_cast<int>(D3D_OK);
}

void D3D9Device::GetIndices(IDirect3DIndexBuffer9** ppIndexData) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (ppIndexData) *ppIndexData = m_indexBuffer;
}

// --- Vertex shaders ---

int D3D9Device::CreateVertexShader(const uint32_t* pDeclaration, const uint32_t* pFunction, void** ppVertexShader, uint32_t Flags) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)pDeclaration; (void)Flags;
  if (!ppVertexShader) return static_cast<int>(D3DERR_INVALIDCALL);
  VKWIND_DBG(kTag, "CreateVertexShader");
  auto* shader = new D3D9VertexShader(this, pFunction);
  *ppVertexShader = static_cast<void*>(shader);
  m_shadersDirty = true;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetVertexShader(void* pShader) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto* newShader = static_cast<IDirect3DVertexShader9*>(pShader);
  if (m_vertexShader != newShader) {
    m_vertexShader = newShader;
    m_shadersDirty = true;
    if (m_compiledVS.module) { vkDestroyShaderModule(m_vkDevice->raw(), m_compiledVS.module, nullptr); m_compiledVS = {}; }
    m_pipelineDirty = true;
  }
  return static_cast<int>(D3D_OK);
}

void* D3D9Device::GetVertexShader() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return static_cast<void*>(m_vertexShader);
}

int D3D9Device::SetVertexShaderConstant(uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pConstantData || StartRegister + Vector4fCount > kMaxFloatConstants) {
    return static_cast<int>(D3DERR_INVALIDCALL);
  }
  memcpy(&m_vsFloatConstants[StartRegister * 4], pConstantData, Vector4fCount * 4 * sizeof(float));
  m_vsConstantsDirty = true;
  VKWIND_DBG(kTag, "SetVertexShaderConstant r%d, %u vec4s", StartRegister, Vector4fCount);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetVertexShaderConstant(uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pConstantData || StartRegister + Vector4fCount > kMaxFloatConstants) {
    return static_cast<int>(D3DERR_INVALIDCALL);
  }
  memcpy(pConstantData, &m_vsFloatConstants[StartRegister * 4], Vector4fCount * 4 * sizeof(float));
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetVertexShaderDecl(void* pDecl) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_vertexShaderDecl = pDecl;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetVertexShaderFunction(const uint32_t* pFunction) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)pFunction;
  return static_cast<int>(D3D_OK);
}

// --- Vertex format (FVF) ---

static void fvf_to_vk_attributes(uint32_t fvf, uint32_t stride,
  std::vector<VkVertexInputBindingDescription>& bindings,
  std::vector<VkVertexInputAttributeDescription>& attributes)
{
  bindings.clear();
  attributes.clear();
  bindings.push_back({0, stride, VK_VERTEX_INPUT_RATE_VERTEX});

  uint32_t offset = 0;
  uint32_t posType = fvf & D3DFVF_POSITION_MASK;

  // Position
  switch (posType) {
    case D3DFVF_XYZ:
      attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offset});
      offset += 12;
      break;
    case D3DFVF_XYZRHW:
      attributes.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offset});
      offset += 16;
      break;
    case D3DFVF_XYZB1:
    case D3DFVF_XYZB2:
      attributes.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offset});
      offset += 16;
      break;
    default:
      attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offset});
      offset += 12;
      break;
  }

  // Normal
  if (fvf & D3DFVF_NORMAL) {
    attributes.push_back({2, 0, VK_FORMAT_R32G32B32_SFLOAT, offset});
    offset += 12;
  }

  // Point size
  if (fvf & D3DFVF_PSIZE) {
    attributes.push_back({3, 0, VK_FORMAT_R32_SFLOAT, offset});
    offset += 4;
  }

  // Diffuse color
  if (fvf & D3DFVF_DIFFUSE) {
    attributes.push_back({1, 0, VK_FORMAT_B8G8R8A8_UNORM, offset});
    offset += 4;
  }

  // Specular color
  if (fvf & D3DFVF_SPECULAR) {
    attributes.push_back({4, 0, VK_FORMAT_B8G8R8A8_UNORM, offset});
    offset += 4;
  }

  // Texture coordinates — use locations 2..9 to match fallback shader (location 2 = tex0)
  uint32_t texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  for (uint32_t i = 0; i < texCount && i < 8; i++) {
    attributes.push_back({2 + i, 0, VK_FORMAT_R32G32_SFLOAT, offset});
    offset += 8;
  }
}

int D3D9Device::SetFVF(uint32_t FVF) {
  m_fvf = FVF;
  m_pipelineDirty = true;
  VKWIND_DBG(kTag, "SetFVF 0x%04X", FVF);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetFVF(uint32_t* pFVF) {
  if (pFVF) *pFVF = m_fvf;
  return static_cast<int>(D3D_OK);
}

// --- Vertex declarations ---

int D3D9Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9* pVertexElements, void** ppDecl) {
  if (!ppDecl) return static_cast<int>(D3DERR_INVALIDCALL);
  auto* decl = new D3D9VertexDeclaration(pVertexElements);
  *ppDecl = decl;
  VKWIND_DBG(kTag, "CreateVertexDeclaration: %zu elements", decl->elements().size());
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetVertexDeclaration(void* pDecl) {
  m_vertexDecl = static_cast<D3D9VertexDeclaration*>(pDecl);
  m_pipelineDirty = true;
  return static_cast<int>(D3D_OK);
}

void D3D9Device::GetVertexDeclaration(void** ppDecl) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (ppDecl) *ppDecl = m_vertexDecl;
}

// --- Pixel shaders ---

int D3D9Device::CreatePixelShader(const uint32_t* pFunction, IDirect3DPixelShader9** ppShader) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!ppShader) return static_cast<int>(D3DERR_INVALIDCALL);
  VKWIND_DBG(kTag, "CreatePixelShader");
  *ppShader = new D3D9PixelShader(this, pFunction);
  m_shadersDirty = true;
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetPixelShader(IDirect3DPixelShader9* pShader) {
  if (m_pixelShader != pShader) {
    m_pixelShader = pShader;
    m_shadersDirty = true;
    if (m_compiledPS.module) { vkDestroyShaderModule(m_vkDevice->raw(), m_compiledPS.module, nullptr); m_compiledPS = {}; }
    m_pipelineDirty = true;
  }
  return static_cast<int>(D3D_OK);
}

IDirect3DPixelShader9* D3D9Device::GetPixelShader() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_pixelShader;
}

int D3D9Device::SetPixelShaderConstant(uint32_t StartRegister, const float* pConstantData, uint32_t Vector4fCount) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pConstantData || StartRegister + Vector4fCount > kMaxFloatConstants) {
    return static_cast<int>(D3DERR_INVALIDCALL);
  }
  memcpy(&m_psFloatConstants[StartRegister * 4], pConstantData, Vector4fCount * 4 * sizeof(float));
  m_psConstantsDirty = true;
  VKWIND_DBG(kTag, "SetPixelShaderConstant r%d, %u vec4s", StartRegister, Vector4fCount);
  return static_cast<int>(D3D_OK);
}

int D3D9Device::GetPixelShaderConstant(uint32_t StartRegister, float* pConstantData, uint32_t Vector4fCount) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!pConstantData || StartRegister + Vector4fCount > kMaxFloatConstants) {
    return static_cast<int>(D3DERR_INVALIDCALL);
  }
  memcpy(pConstantData, &m_psFloatConstants[StartRegister * 4], Vector4fCount * 4 * sizeof(float));
  return static_cast<int>(D3D_OK);
}

int D3D9Device::SetPixelShaderFunction(const uint32_t* pFunction) {
  std::lock_guard<std::mutex> lock(m_mutex);
  (void)pFunction;
  return static_cast<int>(D3D_OK);
}

// --- Patches ---

int D3D9Device::DrawRectPatch(uint32_t Handle, const float* pNumSegs, const void* pRectPatchInfo) {
  (void)Handle; (void)pNumSegs; (void)pRectPatchInfo; return static_cast<int>(D3D_OK);
}

int D3D9Device::DrawTriPatch(uint32_t Handle, const float* pNumSegs, const void* pTriPatchInfo) {
  (void)Handle; (void)pNumSegs; (void)pTriPatchInfo; return static_cast<int>(D3D_OK);
}

int D3D9Device::DeletePatch(uint32_t Handle) { (void)Handle; return static_cast<int>(D3D_OK); }

// --- Query ---

int D3D9Device::CreateQuery(uint32_t Type, IDirect3DQuery9** ppQuery) {
  if (!ppQuery) return static_cast<int>(D3DERR_INVALIDCALL);
  *ppQuery = new D3D9Query(this, static_cast<D3DQUERYTYPE>(Type));
  return static_cast<int>(D3D_OK);
}

} // namespace vkwind
