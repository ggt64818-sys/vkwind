// Pipeline Integration Tests — Phases 9.4 + 10.5
// Tests SM3→SPIR-V pipeline creation, vertex input mapping,
// state mapper, and draw path integration.
//
// Phase 9.4: VS+PS pair → valid SPIR-V → spirv-val
// Phase 10.5: FVF/VertexDecl → VkVertexInputAttribute, StateMapper, DrawIntegration

#include "shader/d3d9_sm3_translator.h"
#include "d3d9/d3d9_fvf.h"
#include "d3d9/d3d9_vertex_declaration.h"
#include "d3d9/d3d9_state_mapper.h"
#include "d3d9/d3d9_types.h"
#include "vulkan/vk_pipeline.h"
#include "util/util_log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <cassert>
#include <vulkan/vulkan.h>

using namespace vkwind;

// ============================================================================
// SM3 token encoding helpers (same as test_spirv_validation.cpp)
// ============================================================================

static uint32_t encode_reg_type(uint32_t regType) {
  uint32_t hi3 = (regType >> 2) & 0x7;
  uint32_t lo2 = regType & 0x3;
  return (hi3 << 28) | (lo2 << 11);
}

static uint32_t encode_reg_num(uint32_t num) {
  return num & 0x7FF;
}

static uint32_t encode_dst_reg(uint32_t regType, uint32_t regNum, uint32_t writeMask = 0xF) {
  return encode_reg_type(regType) | encode_reg_num(regNum) | ((writeMask & 0xF) << 16);
}

static uint32_t encode_src_reg(uint32_t regType, uint32_t regNum,
                                 uint32_t sx = 0, uint32_t sy = 1, uint32_t sz = 2, uint32_t sw = 3) {
  uint32_t swizzle = (sx | (sy << 2) | (sz << 4) | (sw << 6)) << 16;
  return encode_reg_type(regType) | encode_reg_num(regNum) | swizzle;
}

static uint32_t encode_inst_token(uint32_t opcode, uint32_t length, bool predicated = false) {
  return opcode | (length << 24) | (predicated ? (1u << 31) : 0);
}

static uint32_t encode_usage_token(uint32_t usage, uint32_t usageIndex = 0) {
  return (usage & 0x1F) | ((usageIndex & 0xF) << 16);
}

static constexpr uint32_t VS_3_0_TOKEN = 0xFFFE0300;
static constexpr uint32_t PS_3_0_TOKEN = 0xFFFF0300;
static constexpr uint32_t END_TOKEN    = 0x0000FFFF;

// DCL instruction: opcode 31, length 3 (token + usage + dest)
// Usage token: (usageIndex << 16) | (usage & 0x1F)
static void emit_dcl(std::vector<uint32_t>& bc, uint32_t regType, uint32_t regNum,
                      uint32_t usage = 0, uint32_t usageIndex = 0) {
  bc.push_back(encode_inst_token(31, 3)); // DCL, length=3
  bc.push_back(encode_usage_token(usage, usageIndex)); // usage token
  bc.push_back(encode_dst_reg(regType, regNum)); // dest register
}

// ============================================================================
// spirv-val runner
// ============================================================================

static bool write_spirv_file(const char* path, const std::vector<uint32_t>& spirv) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fwrite(spirv.data(), sizeof(uint32_t), spirv.size(), f);
  fclose(f);
  return true;
}

static bool validate_spirv(const char* path) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "C:\\msys64\\mingw64\\bin\\spirv-val.exe %s 2>&1", path);
  int result = system(cmd);
  return result == 0;
}

// ============================================================================
// Phase 9.4: VS+PS Pair Translation Tests
// ============================================================================

// Test: Vertex shader with MVP transform (most common VS pattern)
static std::vector<uint32_t> build_vs_mvp() {
  std::vector<uint32_t> bc;

  // Version token
  bc.push_back(VS_3_0_TOKEN);

  // DCL v0 (POSITION input)
  emit_dcl(bc, 1, 0, 0); // INPUT v0, USAGE_POSITION

  // DCL c0 (4x4 transform matrix constant, vec4[4])
  emit_dcl(bc, 2, 0); // CONST c0

  // DCL o0 (POSITION output)
  emit_dcl(bc, 10, 0, 0); // OUTPUT o0, USAGE_POSITION

  // mov o0.xyz, v0
  bc.push_back(encode_inst_token(1, 3)); // MOV, length=3
  bc.push_back(encode_dst_reg(10, 0, 0x7)); // dest: o0.xyz
  bc.push_back(encode_src_reg(1, 0)); // src: v0

  // mov o0.w, c0.w (homogeneous)
  bc.push_back(encode_inst_token(1, 3)); // MOV, length=3
  bc.push_back(encode_dst_reg(10, 0, 0x8)); // dest: o0.w
  bc.push_back(encode_src_reg(2, 0, 3, 3, 3, 3)); // src: c0.wwww

  // END
  bc.push_back(END_TOKEN);

  return bc;
}

// Test: Vertex shader with dp4 MVP multiply
static std::vector<uint32_t> build_vs_dp4_mvp() {
  std::vector<uint32_t> bc;

  bc.push_back(VS_3_0_TOKEN);

  // DCL v0 (POSITION input)
  emit_dcl(bc, 1, 0, 0);

  // DCL c0 (4x4 matrix, first vec4)
  emit_dcl(bc, 2, 0);

  // DCL o0 (POSITION output)
  emit_dcl(bc, 10, 0, 0);

  // dp4 o0.x, c0, v0
  bc.push_back(encode_inst_token(9, 4)); // DP4, length=4
  bc.push_back(encode_dst_reg(10, 0, 0x1)); // dest: o0.x
  bc.push_back(encode_src_reg(2, 0)); // src: c0
  bc.push_back(encode_src_reg(1, 0)); // src: v0

  // dp4 o0.y, c1, v0
  bc.push_back(encode_inst_token(9, 4));
  bc.push_back(encode_dst_reg(10, 0, 0x2)); // dest: o0.y
  bc.push_back(encode_src_reg(2, 1)); // src: c1
  bc.push_back(encode_src_reg(1, 0)); // src: v0

  // dp4 o0.z, c2, v0
  bc.push_back(encode_inst_token(9, 4));
  bc.push_back(encode_dst_reg(10, 0, 0x4)); // dest: o0.z
  bc.push_back(encode_src_reg(2, 2)); // src: c2
  bc.push_back(encode_src_reg(1, 0)); // src: v0

  // dp4 o0.w, c3, v0
  bc.push_back(encode_inst_token(9, 4));
  bc.push_back(encode_dst_reg(10, 0, 0x8)); // dest: o0.w
  bc.push_back(encode_src_reg(2, 3)); // src: c3
  bc.push_back(encode_src_reg(1, 0)); // src: v0

  bc.push_back(END_TOKEN);

  return bc;
}

// Test: Pixel shader with texture sample + modulate
static std::vector<uint32_t> build_ps_tex_modulate() {
  std::vector<uint32_t> bc;

  bc.push_back(PS_3_0_TOKEN);

  // DCL t0 (TEXCOORD input, texture coordinate set 0)
  emit_dcl(bc, 1, 0, 5, 0); // INPUT t0, USAGE_TEXCOORD, index 0

  // DCL s0 (sampler)
  emit_dcl(bc, 4, 0); // TEXTURE s0

  // DCL o0 (COLOR output)
  emit_dcl(bc, 10, 0, 10, 0); // OUTPUT o0, USAGE_COLOR, index 0

  // texld o0, t0, s0  (texture sample)
  bc.push_back(encode_inst_token(51, 4)); // TEX, length=4
  bc.push_back(encode_dst_reg(10, 0)); // dest: o0
  bc.push_back(encode_src_reg(1, 0)); // src: t0
  bc.push_back(encode_src_reg(4, 0)); // src: s0

  bc.push_back(END_TOKEN);

  return bc;
}

// Test: Pixel shader with texkill (alpha test)
static std::vector<uint32_t> build_ps_texkill() {
  std::vector<uint32_t> bc;

  bc.push_back(PS_3_0_TOKEN);

  // DCL t0 (TEXCOORD input)
  emit_dcl(bc, 1, 0, 5, 0);

  // DCL s0 (sampler)
  emit_dcl(bc, 4, 0);

  // DCL o0 (COLOR output)
  emit_dcl(bc, 10, 0, 10, 0);

  // texld r0, t0, s0
  bc.push_back(encode_inst_token(51, 4));
  bc.push_back(encode_dst_reg(0, 0)); // r0
  bc.push_back(encode_src_reg(1, 0)); // t0
  bc.push_back(encode_src_reg(4, 0)); // s0

  // texkill r0 (discard if alpha < 0)
  bc.push_back(encode_inst_token(50, 2)); // TEXKILL, length=2
  bc.push_back(encode_src_reg(0, 0)); // r0

  // mov o0, r0
  bc.push_back(encode_inst_token(1, 3));
  bc.push_back(encode_dst_reg(10, 0));
  bc.push_back(encode_src_reg(0, 0));

  bc.push_back(END_TOKEN);

  return bc;
}

// Test: Vertex shader with normals + lighting math
static std::vector<uint32_t> build_vs_normal() {
  std::vector<uint32_t> bc;

  bc.push_back(VS_3_0_TOKEN);

  // DCL v0 (POSITION input)
  emit_dcl(bc, 1, 0, 0);

  // DCL v1 (NORMAL input)
  emit_dcl(bc, 1, 1, 3); // INPUT v1, USAGE_NORMAL

  // DCL c0 (world-view-proj matrix, 4 vec4s)
  emit_dcl(bc, 2, 0);

  // DCL c4 (light direction, vec4)
  emit_dcl(bc, 2, 4);

  // DCL o0 (POSITION output)
  emit_dcl(bc, 10, 0, 0);

  // DCL o1 (COLOR output, diffuse)
  emit_dcl(bc, 10, 1, 10); // OUTPUT o1, USAGE_COLOR

  // dp4 o0.x, c0, v0
  bc.push_back(encode_inst_token(9, 4));
  bc.push_back(encode_dst_reg(10, 0, 0x1));
  bc.push_back(encode_src_reg(2, 0));
  bc.push_back(encode_src_reg(1, 0));

  // dp4 o0.y, c1, v0
  bc.push_back(encode_inst_token(9, 4));
  bc.push_back(encode_dst_reg(10, 0, 0x2));
  bc.push_back(encode_src_reg(2, 1));
  bc.push_back(encode_src_reg(1, 0));

  // dp4 o0.z, c2, v0
  bc.push_back(encode_inst_token(9, 4));
  bc.push_back(encode_dst_reg(10, 0, 0x4));
  bc.push_back(encode_src_reg(2, 2));
  bc.push_back(encode_src_reg(1, 0));

  // dp4 o0.w, c3, v0
  bc.push_back(encode_inst_token(9, 4));
  bc.push_back(encode_dst_reg(10, 0, 0x8));
  bc.push_back(encode_src_reg(2, 3));
  bc.push_back(encode_src_reg(1, 0));

  // dp3 r0.x, c4, v1 (N dot L)
  bc.push_back(encode_inst_token(8, 4)); // DP3, length=4
  bc.push_back(encode_dst_reg(0, 0, 0x1));
  bc.push_back(encode_src_reg(2, 4)); // c4
  bc.push_back(encode_src_reg(1, 1)); // v1

  // max o1, r0.x, c0 (clamp to 0..1, light color)
  bc.push_back(encode_inst_token(11, 4)); // MAX, length=4
  bc.push_back(encode_dst_reg(10, 1)); // o1
  bc.push_back(encode_src_reg(0, 0, 0, 0, 0, 0)); // r0.xxxx
  bc.push_back(encode_src_reg(2, 0, 0, 0, 0, 0)); // c0.xxxx

  bc.push_back(END_TOKEN);

  return bc;
}

// Test: Pixel shader with arithmetic (add, mul, mad)
static std::vector<uint32_t> build_ps_arithmetic() {
  std::vector<uint32_t> bc;

  bc.push_back(PS_3_0_TOKEN);

  // DCL t0 (TEXCOORD input)
  emit_dcl(bc, 1, 0, 5, 0);

  // DCL c0 (constant color)
  emit_dcl(bc, 2, 0);

  // DCL o0 (COLOR output)
  emit_dcl(bc, 10, 0, 10, 0);

  // add r0, t0, c0
  bc.push_back(encode_inst_token(2, 4)); // ADD, length=4
  bc.push_back(encode_dst_reg(0, 0)); // r0
  bc.push_back(encode_src_reg(1, 0)); // t0
  bc.push_back(encode_src_reg(2, 0)); // c0

  // mul o0, r0, c1
  bc.push_back(encode_inst_token(5, 4)); // MUL, length=4
  bc.push_back(encode_dst_reg(10, 0)); // o0
  bc.push_back(encode_src_reg(0, 0)); // r0
  bc.push_back(encode_src_reg(2, 1)); // c1

  bc.push_back(END_TOKEN);

  return bc;
}

// ============================================================================
// Phase 10.5: Vertex Input Mapping Tests
// ============================================================================

static int test_fvf_xyz() {
  printf("--- FVF: D3DFVF_XYZ (position only) ---\n");
  auto desc = FVFParser::parse(D3DFVF_XYZ);

  int errors = 0;
  if (desc.stride != 12) { printf("  FAIL: stride=%u, expected 12\n", desc.stride); errors++; }
  if (desc.elements.size() != 1) { printf("  FAIL: elements=%zu, expected 1\n", desc.elements.size()); errors++; }
  if (desc.elements.size() >= 1) {
    if (desc.elements[0].format != VK_FORMAT_R32G32B32_SFLOAT) { printf("  FAIL: position format wrong\n"); errors++; }
    if (desc.elements[0].offset != 0) { printf("  FAIL: position offset=%u, expected 0\n", desc.elements[0].offset); errors++; }
  }

  printf("  stride=%u, elements=%zu %s\n", desc.stride, desc.elements.size(), errors ? "FAIL" : "PASS");
  return errors;
}

static int test_fvf_xyz_diffuse_tex1() {
  printf("--- FVF: XYZ + DIFFUSE + TEX1 (common game format) ---\n");
  auto desc = FVFParser::parse(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

  int errors = 0;
  // XYZ(12) + DIFFUSE(4) + TEX1(8) = 24
  if (desc.stride != 24) { printf("  FAIL: stride=%u, expected 24\n", desc.stride); errors++; }
  if (desc.elements.size() != 3) { printf("  FAIL: elements=%zu, expected 3\n", desc.elements.size()); errors++; }

  // Verify semantic mapping
  if (desc.elements.size() >= 3) {
    if (strcmp(desc.elements[0].semantic, "POSITION") != 0) { printf("  FAIL: elem0 not POSITION\n"); errors++; }
    if (strcmp(desc.elements[1].semantic, "COLOR") != 0) { printf("  FAIL: elem1 not COLOR\n"); errors++; }
    if (desc.elements[1].semanticIndex != 0) { printf("  FAIL: COLOR semanticIndex=%u, expected 0\n", desc.elements[1].semanticIndex); errors++; }
    if (strcmp(desc.elements[2].semantic, "TEXCOORD") != 0) { printf("  FAIL: elem2 not TEXCOORD\n"); errors++; }
    if (desc.elements[2].semanticIndex != 0) { printf("  FAIL: TEXCOORD semanticIndex=%u, expected 0\n", desc.elements[2].semanticIndex); errors++; }
  }

  printf("  stride=%u, elements=%zu %s\n", desc.stride, desc.elements.size(), errors ? "FAIL" : "PASS");
  return errors;
}

static int test_fvf_xyz_normal_diffuse_tex2() {
  printf("--- FVF: XYZ + NORMAL + DIFFUSE + TEX2 ---\n");
  auto desc = FVFParser::parse(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2);

  int errors = 0;
  // XYZ(12) + NORMAL(12) + DIFFUSE(4) + TEX2(16) = 44
  if (desc.stride != 44) { printf("  FAIL: stride=%u, expected 44\n", desc.stride); errors++; }
  if (desc.elements.size() != 5) { printf("  FAIL: elements=%zu, expected 5\n", desc.elements.size()); errors++; }

  if (desc.elements.size() >= 5) {
    if (strcmp(desc.elements[0].semantic, "POSITION") != 0) { printf("  FAIL: elem0 not POSITION\n"); errors++; }
    if (strcmp(desc.elements[1].semantic, "NORMAL") != 0) { printf("  FAIL: elem1 not NORMAL\n"); errors++; }
    if (strcmp(desc.elements[2].semantic, "COLOR") != 0) { printf("  FAIL: elem2 not COLOR\n"); errors++; }
    if (strcmp(desc.elements[3].semantic, "TEXCOORD") != 0 || desc.elements[3].semanticIndex != 0) {
      printf("  FAIL: elem3 wrong TEXCOORD\n"); errors++;
    }
    if (strcmp(desc.elements[4].semantic, "TEXCOORD") != 0 || desc.elements[4].semanticIndex != 1) {
      printf("  FAIL: elem4 wrong TEXCOORD\n"); errors++;
    }
  }

  printf("  stride=%u, elements=%zu %s\n", desc.stride, desc.elements.size(), errors ? "FAIL" : "PASS");
  return errors;
}

static int test_fvf_xyzrhw_diffuse() {
  printf("--- FVF: XYZRHW + DIFFUSE (pre-transformed) ---\n");
  auto desc = FVFParser::parse(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

  int errors = 0;
  // XYZRHW(16) + DIFFUSE(4) = 20
  if (desc.stride != 20) { printf("  FAIL: stride=%u, expected 20\n", desc.stride); errors++; }
  if (desc.elements.size() != 2) { printf("  FAIL: elements=%zu, expected 2\n", desc.elements.size()); errors++; }

  if (desc.elements.size() >= 2) {
    if (desc.elements[0].format != VK_FORMAT_R32G32B32A32_SFLOAT) { printf("  FAIL: XYZRHW format wrong\n"); errors++; }
    if (strcmp(desc.elements[0].semantic, "POSITIONT") != 0) { printf("  FAIL: elem0 not POSITIONT\n"); errors++; }
  }

  printf("  stride=%u, elements=%zu %s\n", desc.stride, desc.elements.size(), errors ? "FAIL" : "PASS");
  return errors;
}

static int test_fvf_to_vk_vertex_input() {
  printf("--- FVF → VkVertexInputAttribute mapping ---\n");
  auto desc = FVFParser::parse(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);

  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  FVFParser::to_vk_vertex_input(desc, bindings, attributes, 0);

  int errors = 0;

  // Should have 1 binding
  if (bindings.size() != 1) { printf("  FAIL: bindings=%zu, expected 1\n", bindings.size()); errors++; }
  else if (bindings[0].stride != 36) { // XYZ(12)+NORMAL(12)+DIFFUSE(4)+TEX1(8)=36
    printf("  FAIL: binding stride=%u, expected 36\n", bindings[0].stride); errors++;
  }

  // Should have 4 attributes (POSITION, NORMAL, COLOR, TEXCOORD)
  if (attributes.size() != 4) { printf("  FAIL: attributes=%zu, expected 4\n", attributes.size()); errors++; }
  else {
    // Verify location assignment
    for (uint32_t i = 0; i < attributes.size(); i++) {
      if (attributes[i].location != i) {
        printf("  FAIL: attribute[%u] location=%u, expected %u\n", i, attributes[i].location, i);
        errors++;
      }
      if (attributes[i].binding != 0) {
        printf("  FAIL: attribute[%u] binding=%u, expected 0\n", i, attributes[i].binding);
        errors++;
      }
    }

    // Verify formats
    if (attributes[0].format != VK_FORMAT_R32G32B32_SFLOAT) { printf("  FAIL: attr0 format wrong\n"); errors++; }
    if (attributes[1].format != VK_FORMAT_R32G32B32_SFLOAT) { printf("  FAIL: attr1 format wrong\n"); errors++; }
    if (attributes[2].format != VK_FORMAT_R8G8B8A8_UNORM) { printf("  FAIL: attr2 format wrong\n"); errors++; }
    if (attributes[3].format != VK_FORMAT_R32G32_SFLOAT) { printf("  FAIL: attr3 format wrong\n"); errors++; }

    // Verify offsets
    if (attributes[0].offset != 0) { printf("  FAIL: attr0 offset=%u, expected 0\n", attributes[0].offset); errors++; }
    if (attributes[1].offset != 12) { printf("  FAIL: attr1 offset=%u, expected 12\n", attributes[1].offset); errors++; }
    if (attributes[2].offset != 24) { printf("  FAIL: attr2 offset=%u, expected 24\n", attributes[2].offset); errors++; }
    if (attributes[3].offset != 28) { printf("  FAIL: attr3 offset=%u, expected 28\n", attributes[3].offset); errors++; }
  }

  printf("  bindings=%zu, attributes=%zu %s\n", bindings.size(), attributes.size(), errors ? "FAIL" : "PASS");
  return errors;
}

static int test_vertex_declaration() {
  printf("--- VertexDeclaration → VkVertexInputAttribute mapping ---\n");

  D3DVERTEXELEMENT9 elements[] = {
    {0, 0,  D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0},
    {0, 24, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},
    {0, 28, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
    {0xFF, 0, 0, 0, 0, 0}, // END marker
  };

  D3D9VertexDeclaration decl(elements);

  int errors = 0;
  if (decl.elements().size() != 4) {
    printf("  FAIL: elements=%zu, expected 4\n", decl.elements().size());
    errors++;
  }

  uint32_t stride = decl.calculate_stride();
  if (stride != 36) { // 12+12+4+8=36
    printf("  FAIL: stride=%u, expected 36\n", stride);
    errors++;
  }

  // Verify decl_type_size
  if (D3D9VertexDeclaration::decl_type_size(D3DDECLTYPE_FLOAT3) != 12) {
    printf("  FAIL: FLOAT3 size wrong\n"); errors++;
  }
  if (D3D9VertexDeclaration::decl_type_size(D3DDECLTYPE_D3DCOLOR) != 4) {
    printf("  FAIL: D3DCOLOR size wrong\n"); errors++;
  }
  if (D3D9VertexDeclaration::decl_type_size(D3DDECLTYPE_FLOAT2) != 8) {
    printf("  FAIL: FLOAT2 size wrong\n"); errors++;
  }

  printf("  elements=%zu, stride=%u %s\n", decl.elements().size(), stride, errors ? "FAIL" : "PASS");
  return errors;
}

// ============================================================================
// Phase 10.5: State Mapper Tests
// ============================================================================

static int test_state_mapper_topology() {
  printf("--- StateMapper: primitive topology ---\n");

  int errors = 0;

  // Test all 6 D3D9 primitive types
  struct { uint32_t d3d; VkPrimitiveTopology vk; const char* name; } cases[] = {
    {1, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, "POINTLIST"},
    {2, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, "LINELIST"},
    {3, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, "LINESTRIP"},
    {4, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, "TRIANGLELIST"},
    {5, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, "TRIANGLESTRIP"},
    {6, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, "TRIANGLEFAN"},
  };

  for (auto& c : cases) {
    VkPrimitiveTopology result = StateMapper::map_primitive_type(c.d3d);
    if (result != c.vk) {
      printf("  FAIL: D3DPT_%s -> %d, expected %d\n", c.name, (int)result, (int)c.vk);
      errors++;
    }
  }

  printf("  6 topology mappings tested %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_state_mapper_compare() {
  printf("--- StateMapper: compare functions ---\n");

  int errors = 0;
  struct { D3DCMPFUNC d3d; VkCompareOp vk; const char* name; } cases[] = {
    {D3DCMP_NEVER, VK_COMPARE_OP_NEVER, "NEVER"},
    {D3DCMP_LESS, VK_COMPARE_OP_LESS, "LESS"},
    {D3DCMP_EQUAL, VK_COMPARE_OP_EQUAL, "EQUAL"},
    {D3DCMP_LESSEQUAL, VK_COMPARE_OP_LESS_OR_EQUAL, "LESSEQUAL"},
    {D3DCMP_GREATER, VK_COMPARE_OP_GREATER, "GREATER"},
    {D3DCMP_NOTEQUAL, VK_COMPARE_OP_NOT_EQUAL, "NOTEQUAL"},
    {D3DCMP_GREATEREQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL, "GREATEREQUAL"},
    {D3DCMP_ALWAYS, VK_COMPARE_OP_ALWAYS, "ALWAYS"},
  };

  for (auto& c : cases) {
    VkCompareOp result = StateMapper::map_compare_func(c.d3d);
    if (result != c.vk) {
      printf("  FAIL: D3DCMP_%s -> %d, expected %d\n", c.name, (int)result, (int)c.vk);
      errors++;
    }
  }

  printf("  8 compare mappings tested %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_state_mapper_blend() {
  printf("--- StateMapper: blend factors ---\n");

  int errors = 0;
  struct { D3DBLEND d3d; VkBlendFactor vk; const char* name; } cases[] = {
    {D3DBLEND_ZERO, VK_BLEND_FACTOR_ZERO, "ZERO"},
    {D3DBLEND_ONE, VK_BLEND_FACTOR_ONE, "ONE"},
    {D3DBLEND_SRCCOLOR, VK_BLEND_FACTOR_SRC_COLOR, "SRCCOLOR"},
    {D3DBLEND_INVSRCCOLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, "INVSRCOLOR"},
    {D3DBLEND_SRCALPHA, VK_BLEND_FACTOR_SRC_ALPHA, "SRCALPHA"},
    {D3DBLEND_INVSRCALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, "INVSRCALPHA"},
    {D3DBLEND_DESTALPHA, VK_BLEND_FACTOR_DST_ALPHA, "DESTALPHA"},
    {D3DBLEND_INVDESTALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, "INVDESTALPHA"},
    {D3DBLEND_DESTCOLOR, VK_BLEND_FACTOR_DST_COLOR, "DESTCOLOR"},
    {D3DBLEND_INVDESTCOLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, "INVDESTCOLOR"},
    {D3DBLEND_SRCALPHASAT, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE, "SRCALPHASAT"},
  };

  for (auto& c : cases) {
    VkBlendFactor result = StateMapper::map_blend_factor(c.d3d);
    if (result != c.vk) {
      printf("  FAIL: D3DBLEND_%s -> %d, expected %d\n", c.name, (int)result, (int)c.vk);
      errors++;
    }
  }

  printf("  11 blend factor mappings tested %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_state_mapper_stencil() {
  printf("--- StateMapper: stencil ops ---\n");

  int errors = 0;
  struct { D3DSTENCILOP d3d; VkStencilOp vk; const char* name; } cases[] = {
    {D3DSTENCILOP_KEEP, VK_STENCIL_OP_KEEP, "KEEP"},
    {D3DSTENCILOP_ZERO, VK_STENCIL_OP_ZERO, "ZERO"},
    {D3DSTENCILOP_REPLACE, VK_STENCIL_OP_REPLACE, "REPLACE"},
    {D3DSTENCILOP_INCRSAT, VK_STENCIL_OP_INCREMENT_AND_CLAMP, "INCRSAT"},
    {D3DSTENCILOP_DECRSAT, VK_STENCIL_OP_DECREMENT_AND_CLAMP, "DECRSAT"},
    {D3DSTENCILOP_INVERT, VK_STENCIL_OP_INVERT, "INVERT"},
    {D3DSTENCILOP_INCR, VK_STENCIL_OP_INCREMENT_AND_WRAP, "INCR"},
    {D3DSTENCILOP_DECR, VK_STENCIL_OP_DECREMENT_AND_WRAP, "DECR"},
  };

  for (auto& c : cases) {
    VkStencilOp result = StateMapper::map_stencil_op(c.d3d);
    if (result != c.vk) {
      printf("  FAIL: D3DSTENCILOP_%s -> %d, expected %d\n", c.name, (int)result, (int)c.vk);
      errors++;
    }
  }

  printf("  8 stencil op mappings tested %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_state_mapper_cull() {
  printf("--- StateMapper: cull mode ---\n");

  int errors = 0;
  struct { D3DCULL d3d; VkCullModeFlags vk; const char* name; } cases[] = {
    {D3DCULL_NONE, VK_CULL_MODE_NONE, "NONE"},
    {D3DCULL_CW, VK_CULL_MODE_FRONT_BIT, "CW"},
    {D3DCULL_CCW, VK_CULL_MODE_BACK_BIT, "CCW"},
  };

  for (auto& c : cases) {
    VkCullModeFlags result = StateMapper::map_cull_mode(c.d3d);
    if (result != c.vk) {
      printf("  FAIL: D3DCULL_%s -> %d, expected %d\n", c.name, (int)result, (int)c.vk);
      errors++;
    }
  }

  printf("  3 cull mode mappings tested %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_state_mapper_render_states() {
  printf("--- StateMapper: full render state mapping ---\n");

  uint32_t renderStates[256] = {};
  renderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
  renderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
  renderStates[D3DRS_ZENABLE] = TRUE;
  renderStates[D3DRS_ZWRITEENABLE] = TRUE;
  renderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
  renderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
  renderStates[D3DRS_STENCILMASK] = 1;

  PipelineState state;
  StateMapper::map_render_states(renderStates, state);

  int errors = 0;
  if (!state.depthTestEnable) { printf("  FAIL: depthTestEnable=false\n"); errors++; }
  if (!state.depthWriteEnable) { printf("  FAIL: depthWriteEnable=false\n"); errors++; }
  if (state.depthCompareOp != VK_COMPARE_OP_LESS_OR_EQUAL) {
    printf("  FAIL: depthCompareOp=%d, expected LESS_OR_EQUAL\n", (int)state.depthCompareOp);
    errors++;
  }
  if (state.cullMode != VK_CULL_MODE_BACK_BIT) {
    printf("  FAIL: cullMode=%d, expected BACK_BIT\n", (int)state.cullMode);
    errors++;
  }
  if (state.blendEnable) { printf("  FAIL: blendEnable=true (should be false with ZERO dest)\n"); errors++; }

  printf("  render states mapped %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

// ============================================================================
// Phase 10.5: FVF Stride Calculation Tests
// ============================================================================

static int test_fvf_stride_calculations() {
  printf("--- FVF stride calculations ---\n");

  int errors = 0;
  struct { uint32_t fvf; uint32_t expectedStride; const char* name; } cases[] = {
    {D3DFVF_XYZ, 12, "XYZ"},
    {D3DFVF_XYZRHW, 16, "XYZRHW"},
    {D3DFVF_XYZ | D3DFVF_DIFFUSE, 16, "XYZ+DIFFUSE"},
    {D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, 24, "XYZ+DIFFUSE+TEX1"},
    {D3DFVF_XYZ | D3DFVF_NORMAL, 24, "XYZ+NORMAL"},
    {D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE, 28, "XYZ+NORMAL+DIFFUSE"},
    {D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR, 32, "XYZ+NORMAL+DIFFUSE+SPECULAR"},
    {D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2, 44, "XYZ+NORMAL+DIFFUSE+TEX2"},
    {D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, 28, "XYZRHW+DIFFUSE+TEX1"},
    {D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2, 36, "XYZRHW+DIFFUSE+TEX2"},
  };

  for (auto& c : cases) {
    uint32_t stride = FVFParser::calculate_stride(c.fvf);
    if (stride != c.expectedStride) {
      printf("  FAIL: FVF(0x%x) %s stride=%u, expected %u\n", c.fvf, c.name, stride, c.expectedStride);
      errors++;
    }
  }

  printf("  10 stride calculations tested %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

// ============================================================================
// Phase 10.5: Vertex Declaration Stride Tests
// ============================================================================

static int test_vertex_decl_strides() {
  printf("--- VertexDeclaration stride calculations ---\n");

  int errors = 0;

  // Test 1: FLOAT3 + FLOAT3 + D3DCOLOR + FLOAT2
  {
    D3DVERTEXELEMENT9 elems[] = {
      {0, 0,  D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0},
      {0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0},
      {0, 24, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},
      {0, 28, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
      {0xFF, 0, 0, 0, 0, 0},
    };
    D3D9VertexDeclaration decl(elems);
    uint32_t s = decl.calculate_stride();
    if (s != 36) { printf("  FAIL: FLOAT3+FLOAT3+COLOR+FLOAT2 stride=%u, expected 36\n", s); errors++; }
  }

  // Test 2: FLOAT4 + SHORT4
  {
    D3DVERTEXELEMENT9 elems[] = {
      {0, 0,  D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0},
      {0, 16, D3DDECLTYPE_SHORT4, 0, D3DDECLUSAGE_TEXCOORD, 0},
      {0xFF, 0, 0, 0, 0, 0},
    };
    D3D9VertexDeclaration decl(elems);
    uint32_t s = decl.calculate_stride();
    if (s != 24) { printf("  FAIL: FLOAT4+SHORT4 stride=%u, expected 24\n", s); errors++; }
  }

  // Test 3: UBYTE4 + FLOAT2
  {
    D3DVERTEXELEMENT9 elems[] = {
      {0, 0,  D3DDECLTYPE_UBYTE4, 0, D3DDECLUSAGE_BLENDINDICES, 0},
      {0, 4,  D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
      {0xFF, 0, 0, 0, 0, 0},
    };
    D3D9VertexDeclaration decl(elems);
    uint32_t s = decl.calculate_stride();
    if (s != 12) { printf("  FAIL: UBYTE4+FLOAT2 stride=%u, expected 12\n", s); errors++; }
  }

  // Test 4: Empty declaration
  {
    D3DVERTEXELEMENT9 elems[] = {{0xFF, 0, 0, 0, 0, 0}};
    D3D9VertexDeclaration decl(elems);
    uint32_t s = decl.calculate_stride();
    if (s != 0) { printf("  FAIL: empty decl stride=%u, expected 0\n", s); errors++; }
  }

  printf("  4 stride tests %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

// ============================================================================
// Additional State Mapper Tests
// ============================================================================

static int test_state_mapper_alpha_blend() {
  printf("--- StateMapper: alpha blend enable/disable ---\n");

  int errors = 0;

  // Test 1: Alpha blend disabled (default)
  {
    uint32_t renderStates[256] = {};
    renderStates[D3DRS_ALPHABLENDENABLE] = FALSE;
    PipelineState state;
    StateMapper::map_render_states(renderStates, state);
    if (state.blendEnable) { printf("  FAIL: blendEnable=true when ALPHABLENDENABLE=0\n"); errors++; }
  }

  // Test 2: Alpha blend enabled
  {
    uint32_t renderStates[256] = {};
    renderStates[D3DRS_ALPHABLENDENABLE] = TRUE;
    renderStates[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA;
    renderStates[D3DRS_DESTBLEND] = D3DBLEND_INVSRCALPHA;
    PipelineState state;
    StateMapper::map_render_states(renderStates, state);
    if (!state.blendEnable) { printf("  FAIL: blendEnable=false when ALPHABLENDENABLE=1\n"); errors++; }
    if (state.srcColorBlend != VK_BLEND_FACTOR_SRC_ALPHA) { printf("  FAIL: srcColorBlend != SRC_ALPHA\n"); errors++; }
    if (state.dstColorBlend != VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) { printf("  FAIL: dstColorBlend != INV_SRC_ALPHA\n"); errors++; }
  }

  // Test 3: Separate alpha blend
  {
    uint32_t renderStates[256] = {};
    renderStates[D3DRS_ALPHABLENDENABLE] = TRUE;
    renderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    renderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    renderStates[D3DRS_SEPARATEALPHABLENDENABLE] = TRUE;
    renderStates[D3DRS_SRCBLENDALPHA] = D3DBLEND_SRCALPHA;
    renderStates[D3DRS_DESTBLENDALPHA] = D3DBLEND_INVSRCALPHA;
    PipelineState state;
    StateMapper::map_render_states(renderStates, state);
    if (state.srcColorBlend != VK_BLEND_FACTOR_ONE) { printf("  FAIL: srcColorBlend != ONE\n"); errors++; }
    if (state.srcAlphaBlend != VK_BLEND_FACTOR_SRC_ALPHA) { printf("  FAIL: srcAlphaBlend != SRC_ALPHA\n"); errors++; }
    if (state.dstAlphaBlend != VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) { printf("  FAIL: dstAlphaBlend != INV_SRC_ALPHA\n"); errors++; }
  }

  printf("  alpha blend %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_state_mapper_all_defaults() {
  printf("--- StateMapper: all defaults (empty render states) ---\n");

  uint32_t renderStates[256] = {};
  PipelineState state;
  StateMapper::map_render_states(renderStates, state);

  int errors = 0;
  if (state.depthTestEnable) { printf("  FAIL: depthTestEnable=true (should be false)\n"); errors++; }
  if (state.depthWriteEnable) { printf("  FAIL: depthWriteEnable=true (should be false)\n"); errors++; }
  if (state.cullMode != VK_CULL_MODE_BACK_BIT) { printf("  FAIL: cullMode=%d, expected BACK_BIT (default)\n", (int)state.cullMode); errors++; }
  if (state.blendEnable) { printf("  FAIL: blendEnable=true (should be false)\n"); errors++; }
  if (state.stencilTestEnable) { printf("  FAIL: stencilTestEnable=true (should be false)\n"); errors++; }

  printf("  all defaults %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_state_mapper_two_sided_stencil() {
  printf("--- StateMapper: two-sided stencil ---\n");

  uint32_t renderStates[256] = {};
  renderStates[D3DRS_STENCILENABLE] = TRUE;
  renderStates[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
  renderStates[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
  renderStates[D3DRS_STENCILZFAIL] = D3DSTENCILOP_INCR;
  renderStates[D3DRS_STENCILPASS] = D3DSTENCILOP_REPLACE;
  renderStates[D3DRS_TWOSIDEDSTENCILMODE] = TRUE;
  renderStates[D3DRS_CCW_STENCILFUNC] = D3DCMP_LESS;
  renderStates[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_ZERO;
  renderStates[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_DECR;
  renderStates[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;

  PipelineState state;
  StateMapper::map_render_states(renderStates, state);

  int errors = 0;
  if (!state.stencilTestEnable) { printf("  FAIL: stencilTestEnable=false\n"); errors++; }
  if (state.backStencilCompareOp != VK_COMPARE_OP_LESS) { printf("  FAIL: backStencilCompareOp != LESS\n"); errors++; }
  if (state.backStencilFailOp != VK_STENCIL_OP_ZERO) { printf("  FAIL: backStencilFailOp != ZERO\n"); errors++; }
  if (state.backStencilDepthFailOp != VK_STENCIL_OP_DECREMENT_AND_WRAP) { printf("  FAIL: backStencilDepthFailOp != DECR_WRAP\n"); errors++; }
  if (state.backStencilPassOp != VK_STENCIL_OP_KEEP) { printf("  FAIL: backStencilPassOp != KEEP\n"); errors++; }

  printf("  two-sided stencil %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

static int test_d3d9_format_mapping() {
  printf("--- D3D9 format mapping ---\n");

  int errors = 0;
  struct { D3DFORMAT d3d; VkFormat vk; const char* name; } cases[] = {
    {D3DFMT_A8R8G8B8,    VK_FORMAT_B8G8R8A8_UNORM,           "A8R8G8B8"},
    {D3DFMT_X8R8G8B8,    VK_FORMAT_B8G8R8A8_UNORM,           "X8R8G8B8"},
    {D3DFMT_A8B8G8R8,    VK_FORMAT_R8G8B8A8_UNORM,           "A8B8G8R8"},
    {D3DFMT_X8B8G8R8,    VK_FORMAT_R8G8B8A8_UNORM,           "X8B8G8R8"},
    {D3DFMT_R5G6B5,      VK_FORMAT_R5G6B5_UNORM_PACK16,      "R5G6B5"},
    {D3DFMT_X1R5G5B5,    VK_FORMAT_A1R5G5B5_UNORM_PACK16,    "X1R5G5B5"},
    {D3DFMT_A1R5G5B5,    VK_FORMAT_A1R5G5B5_UNORM_PACK16,    "A1R5G5B5"},
    {D3DFMT_A4R4G4B4,    VK_FORMAT_A4B4G4R4_UNORM_PACK16,    "A4R4G4B4"},
    {D3DFMT_A2R10G10B10, VK_FORMAT_A2B10G10R10_UNORM_PACK32, "A2R10G10B10"},
    {D3DFMT_D16,         VK_FORMAT_D16_UNORM,                 "D16"},
    {D3DFMT_D24S8,       VK_FORMAT_D24_UNORM_S8_UINT,         "D24S8"},
    {D3DFMT_D32,         VK_FORMAT_D32_SFLOAT,                "D32"},
    {D3DFMT_R32F,        VK_FORMAT_R32_SFLOAT,                "R32F"},
    {D3DFMT_A16B16G16R16F, VK_FORMAT_R16G16B16A16_SFLOAT,    "A16B16G16R16F"},
  };

  for (auto& c : cases) {
    VkFormat result = StateMapper::map_format(c.d3d);
    if (result != c.vk) {
      printf("  FAIL: %s -> %d, expected %d\n", c.name, (int)result, (int)c.vk);
      errors++;
    }
  }

  // Unknown format returns UNDEFINED
  VkFormat unknown = StateMapper::map_format(static_cast<D3DFORMAT>(0xDEAD));
  if (unknown != VK_FORMAT_UNDEFINED) {
    printf("  FAIL: unknown format -> %d, expected UNDEFINED\n", (int)unknown);
    errors++;
  }

  printf("  format mapping %s\n", errors ? "FAIL" : "PASS");
  return errors;
}

// ============================================================================
// Main
// ============================================================================

int main() {
  printf("=== Pipeline Integration Tests (Phases 9.4 + 10.5) ===\n\n");

  int totalPassed = 0;
  int totalFailed = 0;

  // ---- Phase 9.4: SM3→SPIR-V Pipeline Translation ----
  printf("=== Phase 9.4: SM3→SPIR-V Pipeline Translation ===\n\n");

  struct ShaderTest {
    const char* name;
    std::vector<uint32_t> bytecode;
  };

  ShaderTest shaderTests[] = {
    {"VS: MVP passthrough",         build_vs_mvp()},
    {"VS: DP4 MVP multiply",        build_vs_dp4_mvp()},
    {"VS: Normal + lighting",       build_vs_normal()},
    {"PS: TEX + modulate",          build_ps_tex_modulate()},
    {"PS: TEXKILL (alpha test)",    build_ps_texkill()},
    {"PS: Arithmetic (add+mul)",    build_ps_arithmetic()},
  };

  for (auto& st : shaderTests) {
    printf("--- %s ---\n", st.name);
    printf("  Bytecode: %u DWORDs\n", (uint32_t)st.bytecode.size());

    SM3Translator translator;
    auto result = translator.translate(st.bytecode.data(), (uint32_t)st.bytecode.size());

    if (result.spirv.empty()) {
      printf("  FAILED: empty SPIR-V\n\n");
      totalFailed++;
      continue;
    }

    printf("  SPIR-V: %u words, type: %s, temps: %u, samplers: %u\n",
      (uint32_t)result.spirv.size(),
      result.isVertexShader ? "vertex" : "pixel",
      result.tempCount, result.samplerCount);

    char path[256];
    snprintf(path, sizeof(path), "test_pipeline_%s.spv", st.name);
    for (char* p = path; *p; p++) {
      if (*p == ' ' || *p == ':' || *p == '(' || *p == ')' || *p == '/') *p = '_';
    }

    if (!write_spirv_file(path, result.spirv)) {
      printf("  FAILED: could not write %s\n\n", path);
      totalFailed++;
      continue;
    }

    if (validate_spirv(path)) {
      printf("  PASSED: spirv-val OK\n\n");
      totalPassed++;
    } else {
      printf("  FAILED: spirv-val errors\n\n");
      totalFailed++;
    }
  }

  // ---- Phase 10.5: Vertex Input Mapping ----
  printf("\n=== Phase 10.5: Vertex Input Mapping ===\n\n");

  totalFailed += test_fvf_xyz();
  totalPassed++;
  totalFailed += test_fvf_xyz_diffuse_tex1();
  totalPassed++;
  totalFailed += test_fvf_xyz_normal_diffuse_tex2();
  totalPassed++;
  totalFailed += test_fvf_xyzrhw_diffuse();
  totalPassed++;
  totalFailed += test_fvf_to_vk_vertex_input();
  totalPassed++;
  totalFailed += test_vertex_declaration();
  totalPassed++;
  totalFailed += test_fvf_stride_calculations();
  totalPassed++;
  totalFailed += test_vertex_decl_strides();
  totalPassed++;

  // ---- Phase 10.5: State Mapper ----
  printf("\n=== Phase 10.5: State Mapper ===\n\n");

  totalFailed += test_state_mapper_topology();
  totalPassed++;
  totalFailed += test_state_mapper_compare();
  totalPassed++;
  totalFailed += test_state_mapper_blend();
  totalPassed++;
  totalFailed += test_state_mapper_stencil();
  totalPassed++;
  totalFailed += test_state_mapper_cull();
  totalPassed++;
  totalFailed += test_state_mapper_render_states();
  totalPassed++;

  // ---- Additional state mapper tests ----
  printf("\n=== Additional State Mapper Tests ===\n\n");

  totalFailed += test_state_mapper_alpha_blend();
  totalPassed++;
  totalFailed += test_state_mapper_all_defaults();
  totalPassed++;
  totalFailed += test_state_mapper_two_sided_stencil();
  totalPassed++;
  totalFailed += test_d3d9_format_mapping();
  totalPassed++;

  // ---- Summary ----
  printf("\n=== Results: %d passed, %d failed ===\n", totalPassed, totalFailed);
  return totalFailed > 0 ? 1 : 0;
}
