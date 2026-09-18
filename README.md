# VKWIND

A Vulkan-based translation layer for Direct3D 9 which allows running Windows 3D applications on Android using Wine/Winlator. Designed for ARM64 Mali GPUs (MediaTek Helio G99 Ultra / Mali-G57 MC2).

## Status

**v0.5.1** — Performance fixes and correctness improvements.

| Component | Status |
|---|---|
| D3D9 Device | 60+ methods implemented |
| Vulkan Backend | Instance, device, swapchain, pipeline, command buffers |
| SM3 Shader Translator | **83/83 opcodes (100%)** |
| Draw Calls | DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP |
| State Mapping | Depth, Stencil, Blend — full mapping |
| Pipeline Cache | Ring buffer staging, thread-safe |
| Tests | 48/48 passing |
| Winlator Integration | APK built and ready |

### v0.5.1 Changes

- **DrawIndexedPrimitiveUP**: Ring buffer instead of per-draw `vkCreateBuffer` (eliminates frame-time spikes)
- **MVP dirty tracking**: Fixed broken static-local variable tracking (was always recomputing, wasted GPU cycles)
- **DrawPrimitiveUP**: Added missing `ensure_render_pass_active()` call (prevents null pipeline on first draw)
- **ColorFill**: Fixed returning `D3D_OK` on `LockRect` failure (now returns actual error)
- **Shader translator**: Replaced heap-allocated `std::vector` with stack-allocated `std::array` in hot paths

### Not yet implemented

- SM3 addressing modes (aL, a0, indexed constants)
- SM4/DXBC (D3D10/11) — see [VKWIND11](https://github.com/ggt64818-sys/vkwind11)
- D3DX routines
- MSAA, cube maps, 3D textures
- Palette textures (256-color)

## How to use

### With Winlator (Android)

1. Install [Winlator](https://github.com/ggt64818-sys/winlator/releases/tag/winlator-vkwind) on your Android device
2. Copy the `d3d9.so` from the [release builds](https://github.com/ggt64818-sys/vkwind/releases) into the Winlator DXVK directory
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
│   │   ├── d3d9_sm3_translator.*   Main translator (83 opcodes)
│   │   └── shader_translator.*     Base translator framework
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

# Shader translation tests (19 tests)
./test_spirv_validation

# Pipeline integration tests (20 tests)
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

**D3D10/11/12 coming soon..**

## Contributing

Contributions are welcome. Please open an issue before submitting large changes.

## License

MIT
