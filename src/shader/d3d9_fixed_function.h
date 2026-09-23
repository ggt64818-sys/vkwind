#pragma once

#include <cstdint>
#include <vector>

namespace vkwind {

// ============================================================================
// Fixed-Function Texture Combiner Shader Generator
// ============================================================================
//
// Generates SPIR-V fragment shader implementing D3D9 texture stage states
// (D3DTSS_COLOROP / D3DTSS_ALPHAOP) for the fixed-function pipeline.
//
// D3D9 fixed-function flow per stage:
//   arg1 = COLORARG1 (DIFFUSE / CURRENT / TEXTURE / TFACTOR / SPECULAR)
//   arg2 = COLORARG2 (same sources)
//   result = COLOROP(arg1, arg2)
//   CURRENT = result (for next stage)
//
// Supported ops (covers ~95% of real D3D9 games):
//   SELECTARG1, SELECTARG2, MODULATE, MODULATE2X, MODULATE4X,
//   ADD, ADDSIGNED, ADDSIGNED2X, SUBTRACT, ADDSMOOTH,
//   BLENDDIFFUSEALPHA, BLENDCURRENTALPHA, LERP,
//   MODULATEALPHA_ADDCOLOR, MODULATECOLOR_ADDALPHA,
//   MODULATEINVALPHA_ADDCOLOR, MODULATEINVCOLOR_ADDALPHA,
//   DOTPRODUCT3, MULTIPLYADD
//
// SPIR-V layout:
//   set=0, binding=0   push_constant (mvp + alphaTest + textureFactor)
//   set=0, binding=3+i sampler2D tex[i] for i in 0..activeTextures-1
//   location=0 in  vec4 vertexColor
//   location=1 in  vec2 texCoord
//   location=0 out vec4 fragColor
//

struct TextureStageConfig {
  // Raw D3DTSS values (indexed by stage)
  uint32_t colorOp[8] = {};     // D3DTSS_COLOROP
  uint32_t colorArg1[8] = {};   // D3DTSS_COLORARG1
  uint32_t colorArg2[8] = {};   // D3DTSS_COLORARG2
  uint32_t alphaOp[8] = {};     // D3DTSS_ALPHAOP
  uint32_t alphaArg1[8] = {};   // D3DTSS_ALPHAARG1
  uint32_t alphaArg2[8] = {};   // D3DTSS_ALPHAARG2
  uint32_t resultArg[8] = {};   // D3DTSS_RESULTARG
  uint32_t texCoordIndex[8] = {}; // D3DTSS_TEXCOORDINDEX

  uint32_t activeTextureCount = 0;  // How many stages have textures bound
  uint32_t textureFactor = 0xFFFFFFFF; // D3DRS_TEXTUREFACTOR (RGBA)
};

// Generate SPIR-V fragment shader for the fixed-function pipeline.
// Returns empty vector on failure.
std::vector<uint32_t> generate_fixed_function_pixel_shader(
    const TextureStageConfig& config);

} // namespace vkwind
