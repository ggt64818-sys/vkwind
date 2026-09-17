# VKWIND Winlator Integration

## Package Structure

```
installable_components/
  dxvk/
    index.txt                           ← List of available DXVK packages
    dxvk-vkwind-0.5.0.tzst             ← VKWIND D3D9→Vulkan translation
    dxvk-0.96.tzst                      ← Original DXVK (unchanged)
    dxvk-1.4.2.tzst                     ← Original DXVK (unchanged)
    dxvk-2.6.1.tzst                     ← Original DXVK (unchanged)
    x64/
      d3d9.dll                          ← VKWIND (manual install alternative)
      d3d11.dll                         ← DXVK (kept for D3D11 games)
      dxgi.dll                          ← DXVK (kept for D3D11 games)
```

## How It Works

1. **VKWIND d3d9.dll** replaces DXVK's d3d9.dll for D3D9 games
2. **DXVK d3d11.dll + dxgi.dll** kept for D3D11 games (unchanged)
3. Game.exe → Wine → VKWIND d3d9.dll → Vulkan → Turnip/Mali (D3D9)
4. Game.exe → Wine → DXVK d3d11.dll → Vulkan → Turnip/Mali (D3D11)

## Installation Method 1: Winlator DXVK Selector (Recommended)

1. Copy `dxvk-vkwind-0.5.0.tzst` to your device
2. In Winlator → Container Settings → DXWrapper → Click gear icon
3. Click "Download" → Import → Select `dxvk-vkwind-0.5.0.tzst`
4. VKWIND "0.5.0" will appear in the version dropdown
5. Select it and save

## Installation Method 2: Manual File Copy

1. Copy `d3d9.dll` (from x64/) to Winlator's container:
   ```
   {rootDir}/home/xuser/.wine/drive_c/windows/d3d9.dll
   ```
2. Launch Winlator with Vortek (Mesa) driver
3. Run a D3D9 game

## Build for Android ARM64

Cross-compile using Meson + Android NDK:

```bash
# Setup NDK toolchain
meson setup --cross-file android-arm64.txt build-android

# Build
ninja -C build-android

# Package as .tzst for Winlator
mkdir -p staging/dxvk-vkwind-0.5.0
cp build-android/d3d9.so staging/dxvk-vkwind-0.5.0/d3d9.dll
tar cf staging.tar -C staging .
zstd -19 staging.tar -o installable_components/dxvk/dxvk-vkwind-0.5.0.tzst
```

## Testing

1. Install VKWIND via DXVK selector or manual copy
2. Launch Winlator with Vortek (Mesa) driver
3. Run a D3D9 game (e.g., NFS Underground 2, Half-Life 2)
4. Check logs:
   ```
   adb logcat | grep VKWIND
   ```

## Troubleshooting

- **Game crashes**: Check `VKWIND_LOG_FILE` and `VKWIND_LOG_LEVEL=debug`
- **Black screen**: Ensure Vortek (Mesa) driver is selected (not Turnip — Mali only)
- **VKWIND not loading**: Verify d3d9.dll is in the correct Wine prefix path
