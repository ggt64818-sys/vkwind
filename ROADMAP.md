# VKWIND — Roadmap

D3D9 → Vulkan translation layer для запуска PC игр на Android через Wine.
Аналог DXVK, фокус на Mali GPU (Helio G99 Ultra / Mali-G57 MC2).

---

## Фаза 0: Инфраструктура ✅

- [x] Meson build system — `d3d9.dll` + `test_d3d9.exe`
- [x] COM-style интерфейсы (IDirect3D9, IDirect3DDevice9, 62+ методов)
- [x] Экспорты `Direct3DCreate9` / `Direct3DCreate9Ex`

## Фаза 1: Vulkan Backend ✅

- [x] VkInstance, VkDevice с fallback-ами
- [x] Swapchain (B8G8R8A8优先, R8G8B8A8 fallback)
- [x] Triple-buffered frame management
- [x] Render pass (color + depth, Mali TBDR subpass deps)
- [x] Command buffer manager
- [x] VkPipeline (4 dynamic states)
- [x] Persistent VkPipeline cache
- [x] Descriptor pool/layout (3 UBO + 8 textures)
- [x] Memory allocation (Mali fallback)
- [x] Buffer/Image management
- [x] Subgroup size control (VK_EXT_subgroup_size_control)

## Фаза 2: D3D9 State ✅

- [x] SetRenderState — blend, stencil front/back, depth, alpha test
- [x] StateMapper (D3D9 → PipelineState)
- [x] SetViewport / SetScissorRect
- [x] SetSamplerState — per-stage VkSampler
- [x] SetStreamSource / SetFVF / GetFVF
- [x] SetRenderTarget / SetDepthStencilSurface
- [x] FVF → VkVertexInputAttributeDescription
- [x] Dynamic: STENCIL_REFERENCE, BLEND_CONSTANTS

## Фаза 3: Push Constants + Alpha Test ✅

- [x] MVP (64B) + alphaRef (4B) + alphaFunc (4B) = 72B
- [x] Push constants во всех draw path
- [x] Alpha test в fallback FS (D3DCMP_NEVER..ALWAYS)
- [x] SetRenderState: ALPHATESTENABLE, ALPHAREF, ALPHAFUNC

## Фаза 4: Texture Pipeline ✅

- [x] D3D9Texture — GPU Image хранение
- [x] update_texture_descriptors (8 stages)
- [x] Sampler state: filter, address, anisotropy
- [x] Dirty flag

## Фаза 5: Shader Infrastructure ✅

- [x] Fallback VS + FS (HLSL → GLSL → SPIR-V → embedded)
- [x] ShaderTranslator (DXBC) — заготовка
- [x] SM3 Translator — парсинг + генерация SPIR-V
- [x] SpirvEmitter — 6 секций, 40+ операций

## Фаза 6: SM3 Instruction Coverage ✅

**Реализовано (~65 опкодов):**

| Категория | Оpcodes |
|---|---|
| Data movement | MOV, MOVA (stub), DEF, DEFB, DEFI |
| Arithmetic | ADD, SUB, MUL, MAD, DP3, DP4, DP2ADD, RCP, RSQ, ABS, NRM, FRC, EXP, LOG, EXPP, LOGP, LIT, POW, SINCOS, DST, CRS, MIN, MAX, SGN |
| Comparison | SLT, SGE, CMP, CND |
| Matrix | M3x2, M3x3, M3x4, M4x3, M4x4 |
| Texture | TEX, TEXCOORD, TEXKILL, TEXLDD, TEXLDL, TEXBEM, TEXBEML, TEXDEPTH, TEXDP3, TEXDP3TEX, TEXM3x2PAD, TEXM3x2TEX, TEXM3x2DEPTH, TEXM3x3PAD, TEXM3x3TEX, TEXM3x3SPEC, TEXM3x3VSPEC, TEXM3x3, TEXREG2AR, TEXREG2GB, TEXREG2RGB |
| Derivatives | DSX (dFdx), DSY (dFdy) |
| Flow control | IF/ELSE/ENDIF, IFC, LOOP/ENDLOOP, BREAKC, BREAK, BREAKP, SETP, REP/ENDREP (stub), CALL, CALLNZ (stub), RET, LABEL |
| Stubs | NOP, PHASE |

**Не реализовано (~10 опкодов):**

| Категория | Оpcodes | Критичность |
|---|---|---|
| Flow control | REPC/ENDREP (полная), CALLNZ (полная) | Низкая |
| Misc | DLiterals, UNDEF | Низкая |

## Фаза 7: Тесты ✅

- [x] 19 SPIR-V validation тестов (test_spirv_validation.exe) — все PASSED
- [x] 20 Pipeline integration тестов (test_pipeline_integration.exe) — все PASSED
- [x] 39 тестов итого, 39/39 PASS
- [x] Fallback shaders validated (spirv-val)
- [x] End-to-end triangle render (UBO + MVP + Y-flip)

---

# TODO — Следующие фазы

---

## Фаза 8: Flow Control ✅

> 90% SM3 шейдеров используют ветвления и циклы.
> Без этого — ни одна игра не запустится.

### 8.1 Сборка CFG (Control Flow Graph) ✅
- [x] Добавить `m_flowStack` — стек для nesting labels
- [x] `IF (cmp)`: сгенерировать `OpSelectionMerge(mergeLabel, 0)` + `OpBranchConditional(cmp, trueLabel, falseLabel)`
- [x] `ELSE`: завершить true-ветку через `OpBranch(elseLabel)`, начать else-ветку
- [x] `ENDIF`: вставить merge-метку, `OpBranch(mergeLabel)`
- [x] `ELSE` без `ENDIF` → ошибка

### 8.2 Циклы ✅
- [x] `LOOP(mergeLabel)`: `OpLoopMerge(mergeLabel, continueTarget, 0)` + `OpBranch(bodyLabel)`
- [x] `ENDLOOP`: dedicated continueTarget block, `OpBranch(header)` back-edge
- [x] `BREAKC(cmp)`: `OpBranchConditional(cmp, mergeLabel, continueTarget)` + open continueTarget label
- [x] `BREAK`: `OpBranch(mergeLabel)`

### 8.3 Подпрограммы ✅
- [x] `CALL(label)`: буферизация continuation инструкций, переход к подпрограмме
- [x] `RET`: возврат по saved return label, flush continuation buffer
- [x] `CALLNZ(cmp, label)`: stub с warning (мало используется)

### 8.4 Сравнение (SETP + bool) ✅
- [x] `DEFB` — bool constant definition
- [x] `DEFI` — int constant definition
- [x] `SETP` — set predicate (vec4 comparison → bool)
- [x] `BREAKP` — conditional break on predicate

### 8.5 Тесты flow control ✅
- [x] Тест IF/ENDIF (простой)
- [x] Тест IF/ELSE/ENDIF
- [x] Тест IFC (inline comparison)
- [x] Тест LOOP/BREAKC
- [x] Тест SETP/BREAKP
- [x] Тест CALL/RET
- [x] Тест вложенных IF + LOOP

---

## Фаза 11: Texture Operations ✅ Частично

### 11.1 Основные texture ops ✅
- [x] TEXLDD — explicit gradient
- [x] TEXLDL — explicit LOD
- [x] TEXBEM/TEXBEML — bump environment map
- [ ] TEXDEPTH — stub с warning (редко используется)

### 11.2 Matrix texture ops ✅
- [x] TEXM3x2PAD / TEXM3x2TEX
- [x] TEXM3x3PAD / TEXM3x3TEX / TEXM3x3SPEC / TEXM3x3VSPEC / TEXM3x3
- [x] TEXDP3 / TEXDP3TEX
- [x] TEXREG2AR / TEXREG2GB / TEXREG2RGB
- [ ] TEXM3x2DEPTH — stub

---

## Фаза 9: SM3 → Pipeline интеграция ✅

> Сейчас fallback шейдеры. Нужно подменить на реальные SM3 шейдеры.

### 9.1 Хранение скомпилированных шейдеров ✅
- [x] `D3D9VertexShader` / `D3D9PixelShader`: хранят raw D3D9 bytecode
- [x] `CompiledShaderModule` (VkShaderModule + SPIR-V) лениво создаётся в `create_or_get_pipeline()`
- [x] `CreateVertexShader` / `CreatePixelShader`: создают COM объект с bytecode

### 9.2 Pipeline creation с реальными шейдерами ✅
- [x] `create_or_get_pipeline()`: SM3Translator → ShaderTranslator → fallback
- [x] Если оба модуля скомпилированы → используются реальные шейдеры
- [x] Если любой модуль отсутствует → fallback (kDefaultVS/kDefaultFS)
- [x] Кеш pipeline: `m_currentPipeline` инвалидируется при смене шейдера/стейта

### 9.3 SetVertexShader / SetPixelShader ✅
- [x] `SetVertexShader`: уничтожает старый VkShaderModule, сбрасывает m_compiledVS, инвалидирует pipeline
- [x] `SetPixelShader`: аналогично для PS
- [x] `GetVertexShader` / `GetPixelShader` — возвращают текущие

### 9.4 Тесты ✅
- [x] 6 шейдерных тестов: VS (MVP, DP4, Normal+Lighting), PS (TEX+modulate, TEXKILL, Arithmetic)
- [x] SM3→SPIR-V→spirv-val: все 6 проходят spirv-val validation
- [x] `test_pipeline_integration.exe`: 20 тестов, 20/20 PASS

---

## Фаза 10: DrawPrimitive полный path ✅

> Сейчас заглушки. Нужен реальный draw path.

### 10.1 Vertex fetch по FVF ✅
- [x] `fvf_to_vk_attributes()` — FVF → VkVertexInputAttributeDescription (location 0-3+)
- [x] Всегда используется для FVF+stride (и fallback и real shader path)
- [x] `SetStreamSource()` с stride → инвалидация pipeline

### 10.2 DrawPrimitiveUP / DrawIndexedPrimitiveUP ✅
- [x] `DrawPrimitiveUP`: HOST_VISIBLE staging → deferred draw в Present
- [x] `DrawIndexedPrimitiveUP`: DEVICE_LOCAL staging → vkCmdCopyBuffer → draw (буферы в m_frameTempBuffers)
- [x] Оба метода реально записываютvkCmdDraw / vkCmdDrawIndexed

### 10.3 Index buffer ✅
- [x] `SetIndices()`: хранит `IDirect3DIndexBuffer9*`
- [x] `DrawIndexedPrimitive()`: IB → `vkCmdBindIndexBuffer` + `vkCmdDrawIndexed`
- [x] D3DFMT_INDEX16 и D3DFMT_INDEX32

### 10.4 Примитивы ✅
- [x] D3DPT_TRIANGLELIST, D3DPT_TRIANGLESTRIP, D3DPT_TRIANGLEFAN
- [x] D3DPT_POINTLIST, D3DPT_LINELIST, D3DPT_LINESTRIP

### 10.5 Тесты ✅
- [x] FVF → VkVertexInputAttribute: 4 FVF формата (XYZ, XYZ+DIFFUSE+TEX1, XYZ+NORMAL+DIFFUSE+TEX2, XYZRHW+DIFFUSE) + 10 stride калькуляций
- [x] VertexDeclaration stride: FLOAT3+FLOAT3+COLOR+FLOAT2, FLOAT4+SHORT4, UBYTE4+FLOAT2, empty
- [x] StateMapper: topology (6), compare (8), blend (11), stencil (8), cull (3), full render state mapping
- [x] Всё в `test_pipeline_integration.exe`: 14/20 фаза 10.5 PASS

---

## Фаза 12: Depth/Stencil Completeness ✅

### 12.1 Depth formats ✅
- [x] D16 (VK_FORMAT_D16_UNORM)
- [x] D24S8 (VK_FORMAT_D24_UNORM_S8_UINT)
- [x] D32F (VK_FORMAT_D32_SFLOAT)
- [x] Автовыбор best format для устройства (find_supported_depth_format с fallback)

### 12.2 Stencil operations ✅
- [x] Front stencil:所有 8 D3DSTENCILOP → VkStencilOp
- [x] Back-face stencil (two-sided) через D3DRS_TWOSIDEDSTENCILMODE
- [x] Stencil read/write masks + reference

### 12.3 Blend operations ✅
- [x] Все 18 D3DBLEND → VkBlendFactor (включая dual-source SRCCOLOR2)
- [x] Blend ops: ADD, SUB, REVSUB, MIN, MAX
- [x] Separate alpha blend (SRCBLENDALPHA/DESTBLENDALPHA/BLENDOPALPHA)
- [x] Color write mask per-channel

### 12.4 Scissor test ✅
- [x] SetScissorRect с dynamic state

---

## Фаза 13: Miscellaneous ✅

### 13.1 Crash Prevention ✅
- [x] CreateQuery — instantiate D3D9Query (was returning nullptr → game crash)
- [x] CreateVolumeTexture — D3D9VolumeTexture stub (was returning nullptr → crash)
- [x] CreateCubeTexture — D3D9CubeTexture stub (was returning nullptr → crash)

### 13.2 Surface Operations ✅
- [x] StretchRect — CPU surface blit (rect copy with format-aware BPP)
- [x] ColorFill — CPU surface fill with solid color
- [x] CopyRect — CPU surface rect copy with src/dst offset

### 13.3 Data Transfer ✅
- [x] UpdateTexture — CPU→texture copy for all mip levels (LockRect + memcpy + unlock)
- [x] UpdateSurface — CPU→surface copy with format-aware BPP
- [x] GetRenderTargetData — CPU readback via LockRect (D3D9 surface path)
- [x] GetFrontBufferData — stub (low usage, returns D3D_OK)

### 13.4 Texture Stage State ✅
- [x] SetTextureStageState — stores state values per-stage per-type (unordered_map)
- [x] GetTextureStageState — reads back stored values (was returning 0 always)
- [x] D3DTSS_* constants defined (COLOROP, ALPHAOP, COLORARG1/2, ALPHAARG1/2, etc.)

### 13.5 D3D9 State Completeness ✅
- [x] SetLight / GetLight / LightEnable / GetLightEnable — 8 lights with state tracking
- [x] SetClipPlane / GetClipPlane — 6 clip planes stored
- [x] GetVertexShaderConstant / GetPixelShaderConstant — proper storage and retrieval
- [x] TestCooperativeLevel — returns D3DERR_DEVICELOST when device lost

### 13.6 Thread Safety ✅
- [x] std::mutex m_mutex in D3D9Device class
- [x] std::lock_guard in all public methods (~25+ methods)
- [x] Critical for Wine's multi-threaded D3D9 calls
- [x] Thread-safe: SetRenderState, DrawPrimitive, Present, SetTexture, SetSamplerState, etc.

### 13.7 Winlator Integration ✅
- [x] Winlator package structure created (installable_components/dxvk/x64/)
- [x] Android ARM64 cross-compilation toolchain (android-arm64.txt)
- [x] d3d9.dll built and ready for packaging

### Still TODO (low priority)
- [ ] Palette textures (D3D9 256-color mode)
- [ ] SetClipStatus / GetClipStatus
- [ ] Occlusion queries (real VkQuery)
- [ ] MSAA (multi-sample anti-aliasing)
- [ ] sRGB / gamma correct textures
- [ ] Cube maps / 3D textures (real GPU upload)

---

## Фаза 14: Дальний прицел ⚪ ПОКА НЕ НУЖНО

- [ ] SM4/DXBC translator (D3D10/11)
- [ ] D3DX routines (d3dx9_XX.dll)
- [ ] SM3 full addressing (aL, a0, indexed constants)
- [ ] Render-to-vertex-buffer

---

# Приоритеты

| Фаза | Статус | Сложность | Время |
|---|---|---|---|
| 0-7: Инфраструктура + Opcodes | ✅ Готово | — | ~50ч |
| 8: Flow Control | ✅ Готово | — | ~2 недели |
| 9: Shader→Pipeline | ✅ Готово | — | ~3-4 дня |
| 10: DrawPrimitive | ✅ Готово | — | ~4-5 дней |
| 11: Texture Ops | ✅ Частично | Низкая | ~1 день |
| 12: Depth/Stencil | ✅ Готово | — | ~3-4 дня |
| **13: Misc** | ✅ Частично | Низкая | ~2-3 дня |
| 12: Depth/Stencil | ✅ Готово | — | ~3-4 дня |
| 13: Misc | ✅ Готово | — | ~2-3 дня |
| 14: DXBC/D3DX | ⚪ Далеко | Очень высокая | месяцы |

---

# Оценка

| Веха | Время |
|---|---|
| **Первая простая игра** (2D) | ~2-3 недели |
| **50% D3D9 игр** | ~1.5-2 месяца |
| **Конкурентоспособность с DXVK** | ~4-6 месяцев |

DXVK: ~200K строк + 10+ лет community fixes.
VKWIND: ~9500 строк (backend + SM3 translator + 65 opcodes + pipeline + draw + thread safety + lights). **~40% готовности.**

---

# Архитектура

```
vkwind/
  src/
    d3d9/
      d3d9_device.cpp/h       # 69KB — D3D9 интерфейс
      d3d9_types.h             # 33KB — типы и интерфейсы
      d3d9_state_mapper.cpp/h  # D3D9 state → PipelineState
      d3d9_texture.cpp/h       # GPU textures
      d3d9_shader.cpp/h        # VS/PS объекты
      d3d9_fvf.cpp/h           # FVF parser
      d3d9_surface.cpp/h       # Render targets
      d3d9_vertex_buffer.cpp/h # Vertex/Index buffers
      d3d9_swapchain.cpp/h     # Swapchain
    shader/
      d3d9_sm3_translator.cpp/h # SM3 → SPIR-V (~1800 строк)
      default_shaders.inc       # Embedded fallback
    vulkan/
      vk_pipeline.cpp/h        # Pipeline creation & cache
      vk_cmd_buffer.cpp/h      # Command buffers
      vk_device.cpp/h          # Vulkan device
      vk_swapchain.cpp/h       # Swapchain + render pass
      vk_instance.cpp/h        # Instance
      vk_memory.cpp/h          # Memory
      vk_buffer.cpp/h          # Buffers
      vk_image.cpp/h           # Images
  test/
    test_spirv_validation.cpp  # 9 SPIR-V tests (all pass)
    test_d3d9.cpp              # D3D9 smoke test
```

---

# Ключевые решения

1. **Push constants > UBO для MVP** — Mali G57: 128B max, fast path
2. **Subpass dependencies** — TBDR оптимизация (BY_REGION_BIT)
3. **Persistent pipeline cache** — Mali shader compilation ~100ms
4. **Push constants для alpha test** — без pipeline invalidation
5. **Section-based SpirvEmitter** — types/constants/variables/functions
6. **UBO constants (binding 1/2)** — float constants через OpAccessChain + OpLoad
7. **`m_bvec4` type** — для float comparison results (SLT/SGE/CMP)

---

*Последнее обновление: 2026-09-16*
