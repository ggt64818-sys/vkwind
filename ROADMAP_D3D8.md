# VKWIND D3D8 Support Plan

## Context

GTA San Andreas (Windows) uses **D3D9** (RenderWare 3.6) — already supported by VKWIND.
D3D8 is needed for **GTA III** and **GTA Vice City** (RenderWare 3.3/3.4).

## D3D8 vs D3D9 Key Differences

| Feature | D3D8 | D3D9 |
|---------|------|------|
| Interface | `IDirect3D8` / `IDirect3DDevice8` | `IDirect3D9` / `IDirect3DDevice9` |
| Vertex shaders | Combined with declaration | Separate `IDirect3DVertexDeclaration9` |
| Pixel shaders | Register combiner model (PS1.x) | SM1.x-3.0 bytecode |
| SetIndices | Takes BaseVertexIndex param | BaseVertexIndex in DrawIndexedPrimitive |
| Texture stages | Combined TSS for sampler+texture | Split SetTextureStageState + SetSamplerState |
| CreateImageSurface | Exists | Renamed to CreateOffscreenPlainSurface |
| Render states | D3DRS_SOFTWAREVP for SW vertex proc | SetSoftwareVertexProcessing() |
| Formats | D3DFORMAT (same values) | D3DFORMAT (same values) |

## Architecture Decision: d3d8to9 Translation Layer

**Recommended approach**: Translate D3D8 → D3D9, then use existing VKWIND D3D9 → Vulkan pipeline.

Why:
1. D3D8 and D3D9 share ~80% of concepts (formats, render states, textures, FVF)
2. D3D9 pipeline in VKWIND is battle-tested (NFS U2 at 60 FPS)
3. d3d8to9 is a proven approach (used by dgVoodoo, DXVK-wine)
4. Shader translation: D3D8 PS1.x register combiners → D3D9 SM1.x bytecode is well-documented
5. Less code to maintain than a full native D3D8 → Vulkan pipeline

## Implementation Plan

### Phase 1: d3d8to9 Wrapper Layer (~500 lines)

Create `src/d3d8/` directory with thin D3D8 API implementation that translates to D3D9:

**Files to create:**
- `src/d3d8/d3d8.h` — D3D8 type definitions (D3D8 enums, structs)
- `src/d3d8/d3d8_interface.h/cpp` — `IDirect3D8` implementation (wraps `IDirect3D9`)
- `src/d3d8/d3d8_device.h/cpp` — `IDirect3DDevice8` implementation (wraps `IDirect3DDevice9`)
- `src/d3d8/d3d8_resource.h` — Common resource wrapper types
- `src/d3d8/d3d8_main.cpp` — `Direct3DCreate8` export

**Translation mapping:**
```
Direct3DCreate8()           → Direct3DCreate9() + adapter info mapping
IDirect3D8::CreateDevice()  → IDirect3D9::CreateDevice() + param conversion
IDirect3DDevice8::DrawPrimitive() → IDirect3DDevice9::DrawPrimitive()
IDirect3DDevice8::DrawIndexedPrimitive() → IDirect3DDevice9::DrawIndexedPrimitive() (move BaseVertexIndex)
IDirect3DDevice8::SetTextureStageState() → IDirect3DDevice9::SetTextureStageState() + SetSamplerState()
IDirect3DDevice8::CreateTexture() → IDirect3DDevice9::CreateTexture()
IDirect3DDevice8::SetStreamSource() → IDirect3DDevice9::SetStreamSource()
IDirect3DDevice8::SetVertexShader() → IDirect3DDevice9::SetVertexShader() + SetFVF()
IDirect3DDevice8::SetPixelShader() → IDirect3DDevice9::SetPixelShader()
IDirect3DDevice8::CreateVertexShader() → IDirect3DDevice9::CreateVertexShader()
IDirect3DDevice8::CreatePixelShader() → shader conversion → IDirect3DDevice9::CreatePixelShader()
IDirect3DDevice8::SetRenderState() → IDirect3DDevice9::SetRenderState() (most states same)
IDirect3DDevice8::Reset() → IDirect3DDevice9::Reset()
IDirect3DDevice8::Present() → IDirect3DDevice9::Present()
```

### Phase 2: Pixel Shader Combiner → SM1.x Translator (~400 lines)

D3D8 pixel shaders use register combiner model, not SM bytecode. Need to translate:

**D3D8 PS1.x Combiner Model:**
- Up to 8 texture stages
- Each stage has RGB and Alpha combiners
- Inputs: texture, constant, previous output, temporary registers
- Operations: multiply, add, dot product, lerp
- Final combiner: combines all stage outputs

**Translation to D3D9 SM1.x bytecode:**
- Each combiner stage → TEX instruction + arithmetic ops
- Register mapping: T0-T7 → texture registers, C0-C1 → constants, R0-R1 → temp
- Final combiner → final MOV/MAD instruction

### Phase 3: DLL Export & Integration (~100 lines)

- Export `Direct3DCreate8` from `d3d8.dll`
- Add D3D8 targets to `meson.build`
- Update Winlator package with d3d8.dll

### Phase 4: Testing (~200 lines)

- Unit tests for D3D8→D3D9 translation
- Test with GTA III / Vice City D3D8 shaders
- spirv-val validation of generated shaders

## Total estimated: ~1200 lines new code

## Files Modified
- `meson.build` — add d3d8 targets
- `src/dllmain.cpp` — add D3D8 export (or separate dllmain for d3d8.dll)

## Files Created
- `src/d3d8/d3d8.h`
- `src/d3d8/d3d8_interface.h`
- `src/d3d8/d3d8_interface.cpp`
- `src/d3d8/d3d8_device.h`
- `src/d3d8/d3d8_device.cpp`
- `src/d3d8/d3d8_resource.h`
- `src/d3d8/d3d8_main.cpp`
- `src/d3d8/d3d8_ps_combiner.h`
- `src/d3d8/d3d8_ps_combiner.cpp`
- `tests/test_d3d8_translation.cpp`

## Verification
1. `ninja` — clean build with 0 errors
2. `test_d3d8_translation.exe` — all tests pass
3. GTA III / Vice City launch via Winlator with VKWIND d3d8.dll
