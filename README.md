# VKWIND

A Vulkan-based translation layer for Direct3D 9 which allows running Windows 3D applications on Android using Wine/Winlator. Designed for ARM64 Mali GPUs (MediaTek Helio G99 Ultra / Mali-G57 MC2).

> **VKWIND11 just released!** D3D10/11/12 support: https://github.com/ggt64818-sys/vkwind11

## Status

**v0.5.2** — Texture combiners and critical bugfixes.

| Component | Status |
|---|---|
| D3D9 Device | 60+ methods implemented |
| Vulkan Backend | Instance, device, swapchain, pipeline, command buffers |
| SM3 Shader Translator | **83/83 opcodes (100%)** |
| Texture Combiners | **D3DTSS_COLOROP/ALPHAOP — 26 operations, runtime SPIR-V generation** |
| Draw Calls | DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP |
| State Mapping | Depth, Stencil, Blend — full mapping |
| Multi-stream Vertices | Streams 0–15 |
| Pipeline Cache | Ring buffer staging, thread-safe |
| Tests | 48/48 passing |
| Winlator Integration | APK built and ready |

### Not yet implemented

- SM3 addressing modes (aL, a0, indexed constants)
- D3DX routines
- MSAA, cube maps, 3D textures
- Palette textures (256-color)

## What's new in v0.5.2

- **Texture combiners** — D3DTSS_COLOROP / D3DTSS_ALPHAOP now generate SPIR-V shaders at runtime for the fixed-function pipeline. Supports 26 D3DTOP operations (MODULATE, ADD, SUBTRACT, LERP, DOTPRODUCT3, etc.)
- Fixed AddRef/Release static refcount bug
- Fixed async pipeline fallback returning VK_NULL_HANDLE
- Fixed Clear() depth/stencil values passed to render pass
- Multi-stream vertex binding (streams 0–15)

## How to use

### With Winlator (Android)

1. Install [Winlator](https://github.com/ggt64818-sys/winlator/releases/tag/winlator-vkwind) on your Android device
2. In container settings, select DXVK `vkwind-0.5.2`
3. Launch your D3D9 game through Winlator

### With Wine (Linux / Windows)

Copy `d3d9.dll` (or `libd3d9.so`) next to the game executable.

## Build instructions

### Requirements

- [Meson](https://mesonbuild.com/) build system (>= 0.58)
- [Ninja](https://ninja-build.org/) backend
- [MinGW-w64](https://www.mingw-w64.org/) cross-compiler (for Windows/Android builds)
- [Vulkan SDK](https://vulkan.lunarg.com/) headers and libraries

### Windows (testing)

```bash
meson setup build
meson compile -C build
# d3d9.dll is in build/
```

### Android (ARM64)

```bash
meson setup build-android --cross-file android-arm64.txt
ninja -C build-android
# libd3d9.so is in build-android/
```

## Configuration

Environment variables control runtime behavior:

| Variable | Default | Description |
|---|---|---|
| `VKWIND_LOG_LEVEL` | `2` | 0=Off, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Trace |
| `VKWIND_LOG_FILE` | — | Log to file |
| `VKWIND_VALIDATION` | `false` | Enable Vulkan validation layers |
| `VKWIND_VSYNC` | `true` | Enable VSync |

## Project structure

```
vkwind/
├── src/
│   ├── d3d9/             Direct3D 9 API implementation
│   │   ├── d3d9_device.*       IDirect3DDevice9
│   │   ├── d3d9_types.h        D3D9 type definitions
│   │   ├── d3d9_texture.*      Texture management
│   │   ├── d3d9_shader.*       Shader handling
│   │   ├── d3d9_state.*        Render state
│   │   └── d3d9_swapchain.*    Swap chain
│   ├── vulkan/           Vulkan backend
│   │   ├── vk_device.*         Vulkan device management
│   │   ├── vk_pipeline.*       Graphics pipeline + cache
│   │   ├── vk_cmd_buffer.*     Command buffers + ring staging
│   │   ├── vk_swapchain.*      Swap chain management
│   │   ├── vk_memory.*         Memory allocation
│   │   ├── vk_buffer.*         Buffer management
│   │   └── vk_image.*          Image/texture management
│   ├── shader/           SM3 → SPIR-V shader translator
│   │   ├── d3d9_sm3_translator.*      Main translator (83 opcodes)
│   │   ├── d3d9_fixed_function.*      Texture combiner generator
│   │   └── shader_translator.*        Base translator framework
│   └── util/             Logging, configuration
├── test/                 Test suites
├── android-stub/         Vulkan stubs for Android builds
├── winlator-package/     Winlator integration files
└── android-arm64.txt     Android cross-compilation file
```

## Running tests

```bash
meson setup build && meson compile -C build
cd build

# Shader translation tests (24 tests)
./test_spirv_validation

# Pipeline integration tests (24 tests)
./test_pipeline_integration
```

## Environment

**Target hardware:** MediaTek Helio G99 Ultra / ARM Mali-G57 MC2
- 128 GFLOPS, 32 execution units
- Vulkan 1.1 support
- 6 GB RAM

**Target games:**
- GTA San Andreas — 30 FPS
- NFS Underground 2 — 60 FPS
- Other D3D9 era titles (2002–2008)

## Related projects

- [VKWIND11](https://github.com/ggt64818-sys/vkwind11) — D3D10/11/12 to Vulkan translation layer

## Contributing

Contributions are welcome. Please open an issue before submitting large changes.

## License

MIT
