// SPIR-V Validation Test
// Feeds hand-crafted SM3 bytecode through SM3Translator, dumps SPIR-V,
// runs spirv-val on it.
#include "shader/d3d9_sm3_translator.h"
#include "util/util_log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

using namespace vkwind;

// --- SM3 token encoding helpers ---
// Register type encoding (D3DSPR_*):
//   Combined 5-bit value: (hi3 << 2) | lo2
//   hi3 = bits[30:28], lo2 = bits[12:11]
static uint32_t encode_reg_type(uint32_t regType) {
  uint32_t hi3 = (regType >> 2) & 0x7;
  uint32_t lo2 = regType & 0x3;
  return (hi3 << 28) | (lo2 << 11);
}

static uint32_t encode_reg_num(uint32_t num) {
  return num & 0x7FF;
}

// Destination register token
// bits[10:0]=regnum, bits[12:11]=type_lo, bits[30:28]=type_hi, bits[19:16]=writemask
static uint32_t encode_dst_reg(uint32_t regType, uint32_t regNum, uint32_t writeMask = 0xF) {
  return encode_reg_type(regType) | encode_reg_num(regNum) | ((writeMask & 0xF) << 16);
}

// Source register token
// Same as dest + swizzle in bits[23:16] (2 bits per component)
static uint32_t encode_src_reg(uint32_t regType, uint32_t regNum,
                                 uint32_t sx = 0, uint32_t sy = 1, uint32_t sz = 2, uint32_t sw = 3) {
  uint32_t swizzle = (sx | (sy << 2) | (sz << 4) | (sw << 6)) << 16;
  return encode_reg_type(regType) | encode_reg_num(regNum) | swizzle;
}

// Instruction token
// bits[15:0]=opcode, bits[27:24]=length (DWORDs including this token), bit[31]=predicated
static uint32_t encode_inst_token(uint32_t opcode, uint32_t length, bool predicated = false) {
  return opcode | (length << 24) | (predicated ? (1u << 31) : 0);
}

// Usage token for DCL
static uint32_t encode_usage_token(uint32_t usage, uint32_t usageIndex = 0) {
  return (usage & 0x1F) | ((usageIndex & 0xF) << 16);
}

// Version tokens
static constexpr uint32_t VS_3_0_TOKEN = 0xFFFE0300;
static constexpr uint32_t PS_3_0_TOKEN = 0xFFFF0300;
static constexpr uint32_t END_TOKEN    = 0x0000FFFF;

// Helper to write SPIR-V to file
static bool write_spirv_file(const char* path, const std::vector<uint32_t>& spirv) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fwrite(spirv.data(), sizeof(uint32_t), spirv.size(), f);
  fclose(f);
  return true;
}

// Helper to run spirv-val
static bool validate_spirv(const char* path) {
  std::string cmd = std::string("spirv-val ") + path + " 2>&1";
  printf("  Running: %s\n", cmd.c_str());
  int ret = system(cmd.c_str());
  return ret == 0;
}

// ============================================================
// Test 1: Minimal vertex shader (passthrough)
//   dcl_position v0
//   dcl_color v1
//   mov oPos, v0
//   mov oD0, v1
// ============================================================
static std::vector<uint32_t> build_test_vs() {
  std::vector<uint32_t> code;

  // Version token
  code.push_back(VS_3_0_TOKEN);

  // DCL_POSITION v0
  //   length=3 (token + usage + dst_reg)
  code.push_back(encode_inst_token(31, 3));       // DCL opcode
  code.push_back(encode_usage_token(0));           // USAGE_POSITION
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // DCL_COLOR v1
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(10));          // USAGE_COLOR
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 1));

  // DCL_OUTPUT oPos (implicit — but declare anyway)
  // Actually in SM3, outputs are not DCL'd, they're just used.
  // We need DCL for oPos with USAGE_POSITION

  // mov oPos, v0
  //   length=3 (token + dst + src)
  code.push_back(encode_inst_token(1, 3));         // MOV opcode
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  // mov oD0, v1
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 1));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 1));

  // END
  code.push_back(END_TOKEN);

  return code;
}

// ============================================================
// Test 2: Minimal pixel shader (passthrough)
//   dcl_color v0
//   mov oC0, v0
// ============================================================
static std::vector<uint32_t> build_test_ps() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // DCL_COLOR v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(10));          // USAGE_COLOR
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // mov oC0, v0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 3: VS with constant buffer (MVP)
//   def c0, 1, 0, 0, 0
//   def c1, 0, 1, 0, 0
//   def c2, 0, 0, 1, 0
//   def c3, 0, 0, 0, 1
//   dcl_position v0
//   dcl_color v1
//   mad oPos.xyz, v0, c0, c3
//   mov oPos.w, c0.xxxx
//   mov oD0, v1
// ============================================================
static std::vector<uint32_t> build_test_vs_const() {
  std::vector<uint32_t> code;

  code.push_back(VS_3_0_TOKEN);

  // def c0, 1, 0, 0, 0
  //   length=6 (token + dst_reg + 4 floats)
  union { float f; uint32_t u; } fu;
  code.push_back(encode_inst_token(66, 6));        // DEF opcode
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);

  // def c1, 0, 1, 0, 0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 1));
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);

  // def c2, 0, 0, 1, 0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 2));
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);

  // def c3, 0, 0, 0, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 3));
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // DCL_POSITION v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(0));           // USAGE_POSITION
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // DCL_COLOR v1
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(10));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 1));

  // mad oPos.xyz, v0.xyz, c0.xyz, c3.xyz
  //   MAD dest, src0, src1, src2
  //   length=5 (token + dst + 3 sources)
  code.push_back(encode_inst_token(4, 5));         // MAD opcode
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0, 0x7)); // .xyz only
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 3));

  // mov oPos.w, c0.xxxx
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0, 0x8)); // .w only
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // mov oD0, v1
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 1));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 1));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 4: VS with more opcodes (add, mul, dp4, rsq, normalize)
//   def c0, 1, 1, 1, 1
//   dcl_position v0
//   dcl_normal v1
//   dp4 r0.x, v0, c0
//   add r1, v1, c0
//   mul oPos, r0.xxxx, r1
// ============================================================
static std::vector<uint32_t> build_test_vs_math() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(VS_3_0_TOKEN);

  // def c0, 1, 1, 1, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // DCL_POSITION v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(0));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // DCL_NORMAL v1
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(3));           // USAGE_NORMAL
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 1));

  // dp4 r0.x, v0, c0
  //   length=4 (token + dst + 2 sources)
  code.push_back(encode_inst_token(9, 4));         // DP4
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0, 0x1)); // .x
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // add r1, v1, c0
  code.push_back(encode_inst_token(2, 4));         // ADD
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 1));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 1));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // mul oPos, r0.xxxx, r1
  code.push_back(encode_inst_token(5, 4));         // MUL
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0)); // swizzle defaults to .xyzw
  code.push_back(encode_src_reg(SM3_REG_TEMP, 1));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 5: VS using rsq (inverse sqrt)
//   def c0, 1, 1, 1, 1
//   dcl_position v0
//   rsq r0.x, v0.x
//   mul oPos, v0, r0.xxxx
// ============================================================
static std::vector<uint32_t> build_test_vs_rsq() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(VS_3_0_TOKEN);

  // def c0, 1, 1, 1, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // DCL_POSITION v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(0));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // rsq r0.x, v0.x
  code.push_back(encode_inst_token(7, 3));         // RSQ
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0, 0x1));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0, 0, 0, 0, 0)); // .xxxx

  // mul oPos, v0, r0.xxxx
  code.push_back(encode_inst_token(5, 4));         // MUL
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 6: PS with TEX (textured pixel shader)
//   dcl_texcoord v0 (texcoord interpolated from VS)
//   tex oC0, t0
// ============================================================
static std::vector<uint32_t> build_test_ps_tex() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // DCL_TEXCOORD v0  (texcoord0, interpolated)
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));           // USAGE_TEXCOORD
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // TEX oC0, t0
  // SM3 TEX opcode = 51, length = 3 (token + dst + src)
  // Sampler is implicit (same index as texture coordinate register)
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0)); // t0 (implies sampler s0)

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 7: PS multi-texture blend
//   dcl_texcoord0 v0
//   dcl_texcoord1 v1
//   tex t0             (sample texture 0 -> r0)
//   tex t1             (sample texture 1 -> r1)
//   mul r0, r0, r1     (multiply two textures)
//   mov oC0, r0        (output)
// ============================================================
static std::vector<uint32_t> build_test_ps_multitex() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // DCL_TEXCOORD v0 (index 0)
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5, 0));       // USAGE_TEXCOORD, index 0
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // DCL_TEXCOORD v1 (index 1)
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5, 1));       // USAGE_TEXCOORD, index 1
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 1));

  // tex t0 -> r0 (sample with implicit sampler s0)
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // tex t1 -> r1
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 1));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 1));

  // mul r0, r0, r1
  code.push_back(encode_inst_token(5, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 1));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 8: VS with lighting (N dot L)
//   def c0, 0, 0, -1, 0   (light direction)
//   def c1, 0.2, 0.2, 0.2, 1   (ambient)
//   def c2, 0.8, 0.8, 0.8, 1   (diffuse)
//   dcl_position v0
//   dcl_normal v1
//   dp3 r0.x, v1, c0      (NdotL)
//   max r0, r0.xxxx, c1    (clamp to ambient minimum)
//   mad oD0, r0, c2, c1   (diffuse * diffuseColor + ambient)
//   mov oPos, v0           (passthrough position)
// ============================================================
static std::vector<uint32_t> build_test_vs_lighting() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(VS_3_0_TOKEN);

  // def c0, 0, 0, -1, 0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 0.0f;  code.push_back(fu.u);
  fu.f = 0.0f;  code.push_back(fu.u);
  fu.f = -1.0f; code.push_back(fu.u);
  fu.f = 0.0f;  code.push_back(fu.u);

  // def c1, 0.2, 0.2, 0.2, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 1));
  fu.f = 0.2f; code.push_back(fu.u);
  fu.f = 0.2f; code.push_back(fu.u);
  fu.f = 0.2f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // def c2, 0.8, 0.8, 0.8, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 2));
  fu.f = 0.8f; code.push_back(fu.u);
  fu.f = 0.8f; code.push_back(fu.u);
  fu.f = 0.8f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // DCL_POSITION v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(0));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // DCL_NORMAL v1
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(3));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 1));

  // dp3 r0.x, v1, c0
  code.push_back(encode_inst_token(8, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0, 0x1));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 1));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // max r0, r0.xxxx, c1
  code.push_back(encode_inst_token(11, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0, 0, 0, 0, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 1));

  // mad oD0, r0, c2, c1
  code.push_back(encode_inst_token(4, 5));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 1));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 2));
  code.push_back(encode_src_reg(SM3_REG_CONST, 1));

  // mov oPos, v0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 9: PS with LRP (linear interpolate) + CMP
//   def c0, 0.5, 0.5, 0.5, 1
//   dcl_texcoord0 v0
//   tex t0
//   lrp r0, c0, r0, v0   (r0 = c0*r0 + (1-c0)*v0)
//   cmp r1, r0, r0, c0   (if r0 >= 0 then r0 else c0)
//   mov oC0, r1
// ============================================================
static std::vector<uint32_t> build_test_ps_lrp_cmp() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(PS_3_0_TOKEN);

  // def c0, 0.5, 0.5, 0.5, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // DCL_TEXCOORD v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // tex t0 -> r0
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // lrp r0, c0, r0, v0
  code.push_back(encode_inst_token(18, 5));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  // cmp r1, r0, r0, c0
  code.push_back(encode_inst_token(73, 5));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 1));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // mov oC0, r1
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 1));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 10: PS with IF/ENDIF flow control
//   defb b0, true
//   def c0, 1.0, 1.0, 1.0, 1.0
//   dcl_texcoord v0
//   dcl_2d s0
//   tex t0, t0, s0
//   mov r0, t0
//   if b0
//     add r0, r0, c0
//   endif
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_if_endif() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // defb b0, true
  code.push_back(encode_inst_token(47, 3));
  code.push_back(encode_dst_reg(SM3_REG_CONST_BOOL, 0));
  code.push_back(1);

  // def c0, 1.0, 1.0, 1.0, 1.0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // dcl_2d s0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(14));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));

  // tex t0, t0, s0
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // mov r0, t0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // if b0
  code.push_back(encode_inst_token(40, 2));
  code.push_back(encode_src_reg(SM3_REG_CONST_BOOL, 0));

  // add r0, r0, c0
  code.push_back(encode_inst_token(2, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // endif
  code.push_back(encode_inst_token(43, 1));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 11: PS with IF/ELSE/ENDIF flow control
//   defb b0, true
//   def c0, 1.0, 1.0, 1.0, 1.0
//   def c1, 0.5, 0.5, 0.5, 0.5
//   dcl_texcoord v0
//   dcl_2d s0
//   tex t0, t0, s0
//   mov r0, t0
//   if b0
//     add r0, r0, c0
//   else
//     mov r0, c1
//   endif
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_if_else_endif() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // defb b0, true
  code.push_back(encode_inst_token(47, 3));
  code.push_back(encode_dst_reg(SM3_REG_CONST_BOOL, 0));
  code.push_back(1);

  // def c0, 1.0, 1.0, 1.0, 1.0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);

  // def c1, 0.5, 0.5, 0.5, 0.5
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 1));
  code.push_back(0x3F000000);
  code.push_back(0x3F000000);
  code.push_back(0x3F000000);
  code.push_back(0x3F000000);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // dcl_2d s0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(14));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));

  // tex t0, t0, s0
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // mov r0, t0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // if b0
  code.push_back(encode_inst_token(40, 2));
  code.push_back(encode_src_reg(SM3_REG_CONST_BOOL, 0));

  // add r0, r0, c0
  code.push_back(encode_inst_token(2, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // else
  code.push_back(encode_inst_token(42, 1));

  // mov r0, c1
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 1));

  // endif
  code.push_back(encode_inst_token(43, 1));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 12: PS with IFC (compare src0 against zero)
//   dcl_texcoord v0
//   dcl_2d s0
//   tex t0, t0, s0
//   mov r0, t0
//   ifc_gt c0.x, l0  -- if c0.x > 0.0 then goto merge, else goto skip
//   add r0, r0, c0
//   endif
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_ifc() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // def c0, 1.0, 1.0, 1.0, 1.0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);
  code.push_back(0x3F800000);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // dcl_2d s0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(14));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));

  // tex t0, t0, s0
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // mov r0, t0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // ifc_gt c0.x, l0  -- IFC opcode 41, length=3, comparison=GT(1)
  // Token: opcode=41, length=3 → (3 << 24) | 41
  // Token 2: packed: comparison(31:24) | reserved(23:16) | label(15:0)
  // label = 100 (merge label, arbitrarily chosen)
  uint32_t mergeLabel = 100;
  uint32_t cmpPacked = (1 << 24) | (mergeLabel & 0xFFFF);  // GT=1
  code.push_back(encode_inst_token(41, 3));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));
  code.push_back(cmpPacked);

  // add r0, r0, c0
  code.push_back(encode_inst_token(2, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // endif
  code.push_back(encode_inst_token(43, 1));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 13: PS with LOOP/BREAKC/ENDLOOP
//   dcl_texcoord v0
//   dcl_2d s0
//   tex t0, t0, s0
//   mov r0, t0
//   loop l0, l1  -- label 200 = body, label 201 = continue
//   tex t1, t1, s0
//   add r0, r0, t1
//   breakc_gt t0.x, l0  -- if t0.x > 0.0, goto merge (label 100)
//   endloop
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_loop_breakc() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // dcl_2d s0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(14));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));

  // tex t0, t0, s0
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // mov r0, t0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // loop l0, l1  -- opcode 27, length=3, label=200
  code.push_back(encode_inst_token(27, 3));
  code.push_back(encode_src_reg(SM3_REG_ADDR, 0));
  code.push_back(200);  // loop body label

  // tex t1, t1, s0
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 1));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 1));

  // add r0, r0, t1
  code.push_back(encode_inst_token(2, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 1));

  // breakc_gt t0.x, l0  -- opcode 45, length=3, comparison=GT(1)
  // merge label = 100 (will be resolved by translator as loop merge)
  uint32_t mergeLabel = 100;
  uint32_t cmpPacked = (1 << 24) | (mergeLabel & 0xFFFF);
  code.push_back(encode_inst_token(45, 3));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));
  code.push_back(cmpPacked);

  // endloop -- opcode 29, length=1
  code.push_back(encode_inst_token(29, 1));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 14: PS with SETP/BREAKP/LOOP
//   dcl_texcoord v0
//   def c0, 1, 0, 0, 0
//   def c1, 0.5, 0.5, 0.5, 1
//   tex t0, t0, s0
//   mov r0, v0
//   setp_gt p0, r0, c1   -- p0 = (r0 > c1) component-wise
//   loop l0
//   breakp p0            -- break if p0 is true
//   add r0, r0, c0       -- body: r0 += 1
//   endloop
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_setp_breakp() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(PS_3_0_TOKEN);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // dcl_2d s0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(14));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));

  // def c0, 1, 0, 0, 0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);

  // def c1, 0.5, 0.5, 0.5, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 1));
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // tex t0, t0, s0
  code.push_back(encode_inst_token(51, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEXTURE, 0));
  code.push_back(encode_src_reg(SM3_REG_TEXTURE, 0));

  // mov r0, v0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  // setp_gt p0, r0, c1  -- opcode 79, length=5
  // dest: predicate 0
  // comparison: GT=1 packed in bits[31:24]
  // src0: r0, src1: c1
  code.push_back(encode_inst_token(79, 5));
  code.push_back(encode_dst_reg(SM3_REG_PREDICATE, 0));
  code.push_back((1u << 24));  // GT comparison
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 1));

  // loop l0  -- opcode 27, length=3
  code.push_back(encode_inst_token(27, 3));
  code.push_back(encode_src_reg(SM3_REG_ADDR, 0));
  code.push_back(200);  // body label

  // breakp p0  -- opcode 81, length=2
  code.push_back(encode_inst_token(81, 2));
  code.push_back(encode_src_reg(SM3_REG_PREDICATE, 0));

  // add r0, r0, c0
  code.push_back(encode_inst_token(2, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // endloop -- opcode 29, length=1
  code.push_back(encode_inst_token(29, 1));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 15: PS with CALL/RET (subroutine)
//   dcl_texcoord v0
//   def c0, 0.5, 0.5, 0.5, 1
//   call sub0        -- call subroutine at label 200
//   mov oC0, r0
//   label sub0       -- subroutine body
//   mov r0, v0
//   ret
// ============================================================
static std::vector<uint32_t> build_test_ps_call_ret() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(PS_3_0_TOKEN);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // def c0, 0.5, 0.5, 0.5, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // call sub0  -- opcode 25, length=2, label=200
  code.push_back(encode_inst_token(25, 2));
  code.push_back(200);

  // mov oC0, r0  (after return)
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  // label sub0:  -- opcode 30, length=1
  code.push_back(encode_inst_token(30, 1) | (200 << 16));

  // mov r0, v0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  // ret  -- opcode 28, length=1
  code.push_back(encode_inst_token(28, 1));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 16: PS with nested IF inside LOOP
//   dcl_texcoord v0
//   def c0, 0.5, 0.5, 0.5, 1
//   def c1, 1, 0, 0, 0
//   loop l0
//   if c0.x GT 0     -- IFC
//   add r0, r0, c1
//   else
//   add r0, r0, c0
//   endif
//   endloop
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_nested_if_loop() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(PS_3_0_TOKEN);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // def c0, 0.5, 0.5, 0.5, 1
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 1.0f; code.push_back(fu.u);

  // def c1, 1, 0, 0, 0
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 1));
  fu.f = 1.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);
  fu.f = 0.0f; code.push_back(fu.u);

  // mov r0, c0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // loop l0  -- opcode 27, length=3, label=200
  code.push_back(encode_inst_token(27, 3));
  code.push_back(encode_src_reg(SM3_REG_ADDR, 0));
  code.push_back(200);

  // if c0.x GT 0  -- IFC opcode 41, length=3
  // comparison=GT(1), label=300
  uint32_t ifcCmpPacked = (1 << 24) | 300;
  code.push_back(encode_inst_token(41, 3));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));
  code.push_back(ifcCmpPacked);

  // add r0, r0, c1
  code.push_back(encode_inst_token(2, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 1));

  // else  -- opcode 42, length=1
  code.push_back(encode_inst_token(42, 1));

  // add r0, r0, c0
  code.push_back(encode_inst_token(2, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // endif  -- opcode 43, length=1
  code.push_back(encode_inst_token(43, 1));

  // endloop  -- opcode 29, length=1
  code.push_back(encode_inst_token(29, 1));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 17: PS with SGN, CND, DP2ADD
//   dcl_texcoord v0
//   def c0, 0.5, 0.5, 0.5, 0.5
//   sgn r0, v0, c0, c0     -- sign of each component
//   cnd r0, r0, v0, c0     -- conditional select
//   dp2add r0, v0, v0, c0  -- 2-dot + add
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_sgn_cnd_dp2add() {
  std::vector<uint32_t> code;
  union { float f; uint32_t u; } fu;

  code.push_back(PS_3_0_TOKEN);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // def c0, 0.5, 0.5, 0.5, 0.5
  code.push_back(encode_inst_token(66, 6));
  code.push_back(encode_dst_reg(SM3_REG_CONST, 0));
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);
  fu.f = 0.5f; code.push_back(fu.u);

  // sgn r0, v0, c0  -- opcode 34, length=4 (header + dst + 2 src)
  // In SM3, SGN has 2 sources: value and unused clamp range
  code.push_back(encode_inst_token(34, 4));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // cnd r0, r0, v0, c0  -- opcode 65, length=5 (header + dst + 3 src)
  code.push_back(encode_inst_token(65, 5));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // dp2add r0, v0, v0, c0  -- opcode 75, length=5 (header + dst + 3 src)
  code.push_back(encode_inst_token(75, 5));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_CONST, 0));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 18: PS with TEXKILL
//   dcl_texcoord v0
//   mov r0, v0
//   texkill r0             -- discard if any component < 0
//   mov oC0, r0
// ============================================================
static std::vector<uint32_t> build_test_ps_texkill() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // mov r0, v0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  // texkill r0  -- opcode 50, length=2 (dst)
  code.push_back(encode_inst_token(50, 2));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));

  // mov oC0, r0
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Test 19: PS with EXPP, LOGP
//   dcl_texcoord v0
//   expp r0, v0           -- partial precision exp2
//   logp r1, r0           -- partial precision log2
//   mov oC0, r1
// ============================================================
static std::vector<uint32_t> build_test_ps_expp_logp() {
  std::vector<uint32_t> code;

  code.push_back(PS_3_0_TOKEN);

  // dcl_texcoord v0
  code.push_back(encode_inst_token(31, 3));
  code.push_back(encode_usage_token(5));
  code.push_back(encode_dst_reg(SM3_REG_INPUT, 0));

  // expp r0, v0  -- opcode 63, length=3
  code.push_back(encode_inst_token(63, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 0));
  code.push_back(encode_src_reg(SM3_REG_INPUT, 0));

  // logp r1, r0  -- opcode 64, length=3
  code.push_back(encode_inst_token(64, 3));
  code.push_back(encode_dst_reg(SM3_REG_TEMP, 1));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 0));

  // mov oC0, r1
  code.push_back(encode_inst_token(1, 3));
  code.push_back(encode_dst_reg(SM3_REG_OUTPUT, 0));
  code.push_back(encode_src_reg(SM3_REG_TEMP, 1));

  code.push_back(END_TOKEN);
  return code;
}

// ============================================================
// Main
// ============================================================
int main() {
  printf("=== SPIR-V Validation Test ===\n\n");

  struct TestCase {
    const char* name;
    std::vector<uint32_t> bytecode;
  };

  TestCase tests[] = {
    {"VS passthrough (mov)",    build_test_vs()},
    {"PS passthrough (mov)",    build_test_ps()},
    {"VS with constants (mad)", build_test_vs_const()},
    {"VS math ops (dp4,add,mul)", build_test_vs_math()},
    {"VS rsq",                  build_test_vs_rsq()},
    {"PS with TEX",             build_test_ps_tex()},
    {"PS multi-tex blend (tex+mul)", build_test_ps_multitex()},
    {"VS lighting (dp3,max,mad)",    build_test_vs_lighting()},
    {"PS LRP+CMP",              build_test_ps_lrp_cmp()},
    {"PS IF/ENDIF",             build_test_ps_if_endif()},
    {"PS IF/ELSE/ENDIF",        build_test_ps_if_else_endif()},
    {"PS IFC (c0.x GT 0)",      build_test_ps_ifc()},
    {"PS LOOP BREAKC",          build_test_ps_loop_breakc()},
    {"PS SETP BREAKP",          build_test_ps_setp_breakp()},
    {"PS CALL RET",             build_test_ps_call_ret()},
    {"PS nested IF+LOOP",       build_test_ps_nested_if_loop()},
    {"PS SGN+CND+DP2ADD",       build_test_ps_sgn_cnd_dp2add()},
    {"PS TEXKILL",              build_test_ps_texkill()},
    {"PS EXPP+LOGP",            build_test_ps_expp_logp()},
  };

  int passed = 0;
  int failed = 0;

  for (auto& tc : tests) {
    printf("--- Test: %s ---\n", tc.name);
    printf("  Bytecode: %u DWORDs\n", (uint32_t)tc.bytecode.size());

    SM3Translator translator;
    auto result = translator.translate(tc.bytecode.data(), (uint32_t)tc.bytecode.size());

    if (result.spirv.empty()) {
      printf("  FAILED: translation returned empty SPIR-V\n\n");
      failed++;
      continue;
    }

    printf("  SPIR-V: %u words\n", (uint32_t)result.spirv.size());
    printf("  Type: %s\n", result.isVertexShader ? "vertex" : "pixel");
    printf("  Temps: %u, Samplers: %u\n", result.tempCount, result.samplerCount);

    // Write to file
    char path[256];
    snprintf(path, sizeof(path), "test_%s.spv", tc.name);
    // Replace spaces in filename
    for (char* p = path; *p; p++) {
      if (*p == ' ' || *p == '(' || *p == ')' || *p == '/') *p = '_';
    }

    if (!write_spirv_file(path, result.spirv)) {
      printf("  FAILED: could not write %s\n\n", path);
      failed++;
      continue;
    }
    printf("  Written to: %s\n", path);

    // Validate
    if (validate_spirv(path)) {
      printf("  PASSED: spirv-val OK\n\n");
      passed++;
    } else {
      printf("  FAILED: spirv-val errors\n\n");
      failed++;
    }
  }

  printf("=== Results: %d passed, %d failed ===\n", passed, failed);
  return failed > 0 ? 1 : 0;
}
