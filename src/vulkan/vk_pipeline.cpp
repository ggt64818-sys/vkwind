#include "vk_pipeline.h"
#include "../util/util_log.h"

#include <cstring>
#include <cstdio>

namespace vkwind {

static const char* kTag = "VkPipeline";

// D3D9 HLSL semantics to SPIR-V
struct SemanticMapping {
  const char* semantic;
  VkFormat format;
  uint32_t location;
};

static const SemanticMapping kDefaultSemantics[] = {
  {"POSITION",  VK_FORMAT_R32G32B32A32_SFLOAT, 0},
  {"COLOR",     VK_FORMAT_R32G32B32A32_SFLOAT, 1},
  {"TEXCOORD",  VK_FORMAT_R32G32_SFLOAT,       2},
  {"NORMAL",    VK_FORMAT_R32G32B32_SFLOAT,     3},
  {"TANGENT",   VK_FORMAT_R32G32B32A32_SFLOAT, 4},
  {"BINORMAL",  VK_FORMAT_R32G32B32_SFLOAT,     5},
  {"BLENDWEIGHT", VK_FORMAT_R32G32B32A32_SFLOAT, 6},
  {"BLENDINDICES", VK_FORMAT_R8G8B8A8_UINT,     7},
};

VkPipeline::VkPipeline(VkDevice* device) : m_device(device) {}

VkPipeline::~VkPipeline() {
  clear_shader_cache();
}

CompiledShader VkPipeline::compile_vertex_shader(const std::string& hlsl, const std::string& entry) {
  CompiledShader result;
  result.stage = VK_SHADER_STAGE_VERTEX_BIT;
  result.entryPoint = entry;

  VKWIND_DBG(kTag, "Compiling vertex shader (entry: %s, %zu bytes)", entry.c_str(), hlsl.size());

  // Placeholder SPIR-V
  result.spirv = {
    0x07230203, // magic
    0x00010000, // version 1.0
    0x00000008, // generator
    0x00000001, // bound
    0x00000000, // schema
  };

  return result;
}

CompiledShader VkPipeline::compile_pixel_shader(const std::string& hlsl, const std::string& entry) {
  CompiledShader result;
  result.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  result.entryPoint = entry;

  VKWIND_DBG(kTag, "Compiling pixel shader (entry: %s, %zu bytes)", entry.c_str(), hlsl.size());

  // Placeholder SPIR-V
  result.spirv = {
    0x07230203,
    0x00010000,
    0x00000008,
    0x00000001,
    0x00000000,
  };

  return result;
}

::VkPipeline VkPipeline::create_graphics_pipeline(const PipelineState& state, VkRenderPass renderPass) {
  return create_graphics_pipeline(state, renderPass, VK_NULL_HANDLE);
}

::VkPipeline VkPipeline::create_graphics_pipeline(const PipelineState& state, VkRenderPass renderPass,
                                                   VkPipelineCache cache) {
  auto dev = m_device->raw();

  // Shader stages
  std::vector<VkPipelineShaderStageCreateInfo> stages;

  VkPipelineShaderStageCreateInfo vertStage = {};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = state.vertexShader;
  vertStage.pName = "main";
  stages.push_back(vertStage);

  if (state.fragmentShader) {
    VkPipelineShaderStageCreateInfo fragStage = {};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = state.fragmentShader;
    fragStage.pName = "main";
    stages.push_back(fragStage);
  }

  // Vertex input
  VkPipelineVertexInputStateCreateInfo vertexInput = {};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(state.vertexBindings.size());
  vertexInput.pVertexBindingDescriptions = state.vertexBindings.data();
  vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(state.vertexAttributes.size());
  vertexInput.pVertexAttributeDescriptions = state.vertexAttributes.data();

  // Input assembly
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = state.topology;
  inputAssembly.primitiveRestartEnable = state.primitiveRestart;

  // Viewport/scissor (dynamic)
  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = state.viewportCount;
  viewportState.scissorCount = state.scissorCount;

  // Rasterization
  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = state.polygonMode;
  rasterizer.lineWidth = state.lineWidth;
  rasterizer.cullMode = state.cullMode;
  rasterizer.frontFace = state.frontFace;
  rasterizer.depthBiasEnable = VK_FALSE;

  // Multisampling
  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = state.rasterizationSamples;

  // Depth/stencil
  VkPipelineDepthStencilStateCreateInfo depthStencil = {};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = state.depthTestEnable;
  depthStencil.depthWriteEnable = state.depthWriteEnable;
  depthStencil.depthCompareOp = state.depthCompareOp;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = state.stencilTestEnable;
  depthStencil.front.compareOp = state.stencilCompareOp;
  depthStencil.front.failOp = state.stencilFailOp;
  depthStencil.front.depthFailOp = state.stencilDepthFailOp;
  depthStencil.front.passOp = state.stencilPassOp;
  depthStencil.front.compareMask = state.stencilReadMask;
  depthStencil.front.writeMask = state.stencilWriteMask;
  depthStencil.front.reference = state.stencilReference;
  depthStencil.back.compareOp = state.backStencilCompareOp;
  depthStencil.back.failOp = state.backStencilFailOp;
  depthStencil.back.depthFailOp = state.backStencilDepthFailOp;
  depthStencil.back.passOp = state.backStencilPassOp;
  depthStencil.back.compareMask = state.stencilReadMask;
  depthStencil.back.writeMask = state.stencilWriteMask;
  depthStencil.back.reference = state.stencilReference;

  // Color blending
  VkPipelineColorBlendAttachmentState blendAttachment = {};
  blendAttachment.colorWriteMask = state.colorWriteMask;
  blendAttachment.blendEnable = state.blendEnable;
  blendAttachment.srcColorBlendFactor = state.srcColorBlend;
  blendAttachment.dstColorBlendFactor = state.dstColorBlend;
  blendAttachment.colorBlendOp = state.colorBlendOp;
  blendAttachment.srcAlphaBlendFactor = state.srcAlphaBlend;
  blendAttachment.dstAlphaBlendFactor = state.dstAlphaBlend;
  blendAttachment.alphaBlendOp = state.alphaBlendOp;

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &blendAttachment;

  // Dynamic state
  std::vector<VkDynamicState> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_BLEND_CONSTANTS,
  };

  VkPipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // Create pipeline
  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
  pipelineInfo.pStages = stages.data();
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = state.pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  ::VkPipeline pipeline;
  VkResult result = vkCreateGraphicsPipelines(dev, cache, 1, &pipelineInfo, nullptr, &pipeline);

  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create graphics pipeline: %d", result);
    return VK_NULL_HANDLE;
  }

  VKWIND_DBG(kTag, "Graphics pipeline created: topo=%d, blend=%d, depth=%d (cache=%s)",
    state.topology, state.blendEnable, state.depthTestEnable, cache ? "yes" : "no");

  return pipeline;
}

::VkPipeline VkPipeline::create_compute_pipeline(VkShaderModule computeShader) {
  auto dev = m_device->raw();

  VkComputePipelineCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  info.stage.module = computeShader;
  info.stage.pName = "main";

  ::VkPipeline pipeline;
  VkResult result = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create compute pipeline: %d", result);
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

VkPipelineLayout VkPipeline::create_pipeline_layout(VkDescriptorSetLayout descLayout) {
  VkPipelineLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  if (descLayout) {
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descLayout;
  }

  VkPipelineLayout layout;
  VkResult result = vkCreatePipelineLayout(m_device->raw(), &layoutInfo, nullptr, &layout);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create pipeline layout: %d", result);
    return VK_NULL_HANDLE;
  }
  return layout;
}

VkPipelineLayout VkPipeline::create_pipeline_layout_with_push_constants(
    VkDescriptorSetLayout descLayout, VkShaderStageFlags stageFlags, uint32_t pushConstantSize) {
  VkPushConstantRange pushRange = {};
  pushRange.stageFlags = stageFlags;
  pushRange.offset = 0;
  pushRange.size = pushConstantSize;

  VkPipelineLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  if (descLayout) {
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descLayout;
  }
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;

  VkPipelineLayout layout;
  VkResult result = vkCreatePipelineLayout(m_device->raw(), &layoutInfo, nullptr, &layout);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create pipeline layout with push constants: %d", result);
    return VK_NULL_HANDLE;
  }

  VKWIND_DBG(kTag, "Pipeline layout created: pushConstants=%u bytes, stages=0x%x",
    pushConstantSize, stageFlags);
  return layout;
}

void VkPipeline::destroy_pipeline_layout(VkPipelineLayout layout) {
  if (layout) vkDestroyPipelineLayout(m_device->raw(), layout, nullptr);
}

VkRenderPass VkPipeline::create_render_pass(VkFormat colorFormat, VkFormat depthFormat) {
  return create_render_pass_optimized(colorFormat, depthFormat, true, false);
}

VkRenderPass VkPipeline::create_render_pass_optimized(VkFormat colorFormat, VkFormat depthFormat,
                                                       bool depthWrite, bool msaa) {
  std::vector<VkAttachmentDescription> attachments;
  std::vector<VkAttachmentReference> colorRefs;
  VkAttachmentReference depthRef = {};
  bool hasDepth = (depthFormat != VK_FORMAT_UNDEFINED);

  // --- Color attachment ---
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = colorFormat;
  colorAttachment.samples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  // Mali TBDR: DONT_CARE on MSAA resolve target (tile resolve is free on Valhall)
  // For non-MSAA: STORE needed for presentation
  colorAttachment.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  attachments.push_back(colorAttachment);

  VkAttachmentReference colorRef = {};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorRefs.push_back(colorRef);

  // --- MSAA resolve attachment (if MSAA) ---
  if (msaa) {
    VkAttachmentDescription resolveAttachment = {};
    resolveAttachment.format = colorFormat;
    resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments.push_back(resolveAttachment);
  }

  // --- Depth attachment ---
  uint32_t depthAttachmentIndex = hasDepth ? (msaa ? 2 : 1) : UINT32_MAX;
  if (hasDepth) {
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Mali TBDR: DONT_CARE when depth write disabled = massive bandwidth savings
    // Valhall stores tile depth to L2 only; DONT_CARE skips the write entirely
    depthAttachment.storeOp = depthWrite ? VK_ATTACHMENT_STORE_OP_STORE
                                          : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments.push_back(depthAttachment);

    depthRef.attachment = depthAttachmentIndex;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  // --- Subpass ---
  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
  subpass.pColorAttachments = colorRefs.data();
  if (hasDepth) {
    subpass.pDepthStencilAttachment = &depthRef;
  }
  VkAttachmentReference resolveRef = {};
  if (msaa) {
    resolveRef.attachment = 1;
    resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    subpass.pResolveAttachments = &resolveRef;
  }

  // --- Subpass dependencies (Mali TBDR optimization) ---
  // Dependency 1: External -> subpass 0
  //   Wait for all previous writes (swapchain acquire) before writing in tile
  VkSubpassDependency dependencies[2] = {};

  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[0].srcAccessMask = 0;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  // Dependency 1b: External -> subpass 0 (for texture sampling)
  //   Allow fragment shader to sample textures concurrently with tile rasterization
  //   Mali Valhall: subpass input attachments + concurrent sample improves perf
  dependencies[0].srcStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

  // Dependency 2: subpass 0 -> External (presentation)
  //   Ensure all tile writes complete before present
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  VkRenderPassCreateInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  rpInfo.pAttachments = attachments.data();
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 2;
  rpInfo.pDependencies = dependencies;

  VkRenderPass renderPass;
  VkResult result = vkCreateRenderPass(m_device->raw(), &rpInfo, nullptr, &renderPass);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create render pass: %d", result);
    return VK_NULL_HANDLE;
  }

  VKWIND_DBG(kTag, "Render pass created: color=%d, depth=%d, depthWrite=%d, msaa=%d, storeOps=%s",
    colorFormat, depthFormat, depthWrite, msaa,
    msaa ? "DONT_CARE(color) STORE(resolve)" : "STORE(color)");
  return renderPass;
}

void VkPipeline::destroy_render_pass(VkRenderPass rp) {
  if (rp) vkDestroyRenderPass(m_device->raw(), rp, nullptr);
}

VkFramebuffer VkPipeline::create_framebuffer(VkRenderPass renderPass, VkImageView colorView,
                                              uint32_t width, uint32_t height) {
  VkFramebufferCreateInfo fbInfo = {};
  fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbInfo.renderPass = renderPass;
  fbInfo.attachmentCount = 1;
  fbInfo.pAttachments = &colorView;
  fbInfo.width = width;
  fbInfo.height = height;
  fbInfo.layers = 1;

  VkFramebuffer fb;
  VkResult result = vkCreateFramebuffer(m_device->raw(), &fbInfo, nullptr, &fb);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create framebuffer: %d", result);
    return VK_NULL_HANDLE;
  }
  return fb;
}

void VkPipeline::destroy_framebuffer(VkFramebuffer fb) {
  if (fb) vkDestroyFramebuffer(m_device->raw(), fb, nullptr);
}

VkDescriptorSetLayout VkPipeline::create_descriptor_set_layout(uint32_t bindingCount,
  const VkDescriptorSetLayoutBinding* bindings) {
  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = bindingCount;
  layoutInfo.pBindings = bindings;

  VkDescriptorSetLayout layout;
  VkResult result = vkCreateDescriptorSetLayout(m_device->raw(), &layoutInfo, nullptr, &layout);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create descriptor set layout: %d", result);
    return VK_NULL_HANDLE;
  }
  return layout;
}

void VkPipeline::destroy_descriptor_set_layout(VkDescriptorSetLayout layout) {
  if (layout) vkDestroyDescriptorSetLayout(m_device->raw(), layout, nullptr);
}

VkDescriptorPool VkPipeline::create_descriptor_pool(uint32_t maxSets,
  uint32_t poolSizeCount, const VkDescriptorPoolSize* poolSizes) {
  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  poolInfo.maxSets = maxSets;
  poolInfo.poolSizeCount = poolSizeCount;
  poolInfo.pPoolSizes = poolSizes;

  VkDescriptorPool pool;
  VkResult result = vkCreateDescriptorPool(m_device->raw(), &poolInfo, nullptr, &pool);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create descriptor pool: %d", result);
    return VK_NULL_HANDLE;
  }
  return pool;
}

void VkPipeline::destroy_descriptor_pool(VkDescriptorPool pool) {
  if (pool) vkDestroyDescriptorPool(m_device->raw(), pool, nullptr);
}

VkDescriptorSet VkPipeline::allocate_descriptor_set(VkDescriptorPool pool, VkDescriptorSetLayout layout) {
  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  VkDescriptorSet set;
  VkResult result = vkAllocateDescriptorSets(m_device->raw(), &allocInfo, &set);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to allocate descriptor set: %d", result);
    return VK_NULL_HANDLE;
  }
  return set;
}

void VkPipeline::update_descriptor_set(VkDescriptorSet set, uint32_t binding,
  VkDescriptorType type, uint32_t count, const VkDescriptorImageInfo* imageInfo) {
  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = binding;
  write.dstArrayElement = 0;
  write.descriptorType = type;
  write.descriptorCount = count;
  write.pImageInfo = imageInfo;

  vkUpdateDescriptorSets(m_device->raw(), 1, &write, 0, nullptr);
}

void VkPipeline::update_descriptor_set(VkDescriptorSet set, uint32_t binding,
  VkDescriptorType type, uint32_t count, const VkDescriptorBufferInfo* bufferInfo) {
  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = binding;
  write.dstArrayElement = 0;
  write.descriptorType = type;
  write.descriptorCount = count;
  write.pBufferInfo = bufferInfo;

  vkUpdateDescriptorSets(m_device->raw(), 1, &write, 0, nullptr);
}

void VkPipeline::update_descriptor_set_batch(VkDescriptorSet set,
  uint32_t writeCount, const VkWriteDescriptorSet* writes) {
  if (writeCount == 0) return;
  vkUpdateDescriptorSets(m_device->raw(), writeCount, writes, 0, nullptr);
}

// --- Pipeline cache (Mali: shader recompilation takes 0.5-2s per pipeline) ---

VkPipelineCache VkPipeline::create_pipeline_cache(const void* initialData, size_t dataSize) {
  VkPipelineCacheCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  if (initialData && dataSize > 0) {
    info.initialDataSize = dataSize;
    info.pInitialData = initialData;
  }

  VkPipelineCache cache;
  VkResult result = vkCreatePipelineCache(m_device->raw(), &info, nullptr, &cache);
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to create pipeline cache: %d", result);
    return VK_NULL_HANDLE;
  }
  VKWIND_DBG(kTag, "Pipeline cache created (%zu bytes initial)", dataSize);
  return cache;
}

bool VkPipeline::save_pipeline_cache(VkPipelineCache cache, const char* path) {
  size_t dataSize = 0;
  VkResult result = vkGetPipelineCacheData(m_device->raw(), cache, &dataSize, nullptr);
  if (result != VK_SUCCESS || dataSize == 0) {
    VKWIND_ERR(kTag, "Failed to get pipeline cache size: %d", result);
    return false;
  }

  std::vector<uint8_t> data(dataSize);
  result = vkGetPipelineCacheData(m_device->raw(), cache, &dataSize, data.data());
  if (result != VK_SUCCESS) {
    VKWIND_ERR(kTag, "Failed to get pipeline cache data: %d", result);
    return false;
  }

  FILE* f = fopen(path, "wb");
  if (!f) {
    VKWIND_ERR(kTag, "Failed to open pipeline cache file: %s", path);
    return false;
  }
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);

  VKWIND_INFO(kTag, "Pipeline cache saved: %s (%zu bytes)", path, dataSize);
  return true;
}

bool VkPipeline::load_pipeline_cache(const char* path, std::vector<uint8_t>& data) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    VKWIND_WARN(kTag, "Pipeline cache file not found: %s", path);
    return false;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return false;
  }

  data.resize(static_cast<size_t>(size));
  size_t read = fread(data.data(), 1, data.size(), f);
  fclose(f);

  if (read != data.size()) {
    VKWIND_WARN(kTag, "Pipeline cache read incomplete: %zu / %zu bytes", read, data.size());
    data.clear();
    return false;
  }

  VKWIND_INFO(kTag, "Pipeline cache loaded: %s (%zu bytes)", path, data.size());
  return true;
}

void VkPipeline::destroy_pipeline_cache(VkPipelineCache cache) {
  if (cache) vkDestroyPipelineCache(m_device->raw(), cache, nullptr);
}

void VkPipeline::clear_shader_cache() {
  for (auto& entry : m_shaderCache) {
    if (entry.module) {
      vkDestroyShaderModule(m_device->raw(), entry.module, nullptr);
    }
  }
  m_shaderCache.clear();
}

} // namespace vkwind
