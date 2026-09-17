# VKWIND v0.5.0-alpha

D3D9 → Vulkan translation layer для запуска PC игр на Android через Wine/Winlator.
Аналог DXVK, фокус на Mali GPU (Helio G99 Ultra / Mali-G57 MC2).

## Статус

**v0.5.0-alpha** — ~40% готовности. Базовые D3D9 игры работают.

- ✅ D3D9 Device: ~60+ методов с реальной логикой
- ✅ Vulkan backend: instance, device, swapchain, pipeline, command buffers
- ✅ SM3 shader translator: 65/85 опкодов (77%)
- ✅ DrawPrimitive/DrawIndexedPrimitive/UP — все draw path
- ✅ Depth/Stencil/Blend — полный mapping
- ✅ Pipeline cache + ring buffer staging
- ✅ Thread safety (mutex)
- ✅ 39 тестов PASS
- ✅ Winlator integration — APK работает

**Не реализовано:**
- SM3 addressing (aL, a0, indexed constants)
- SM4/DXBC (D3D10/11)
- D3DX routines
- MSAA, cube maps, 3D textures
- Palette textures (256-color)

## Запуск

### Windows (тестирование)
```bash
meson setup build
meson compile -C build
# d3d9.dll готов — положить рядом с .exe игры
```

### Android (ARM64)
```bash
meson setup build-android --cross-file android-arm64.txt
ninja -C build-android
# d3d9.so — упаковать в Winlator
```

## Конфигурация

| Переменная | По умолчанию | Описание |
|---|---|---|
| `VKWIND_LOG_LEVEL` | `2` | 0=Off, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Trace |
| `VKWIND_LOG_FILE` | — | Логировать в файл |
| `VKWIND_VALIDATION` | `false` | Vulkan validation layers |
| `VKWIND_VSYNC` | `true` | VSync |

## Структура

```
vkwind/
├── src/
│   ├── d3d9/           # D3D9 API (~60 файлов)
│   │   ├── d3d9_device.*       # IDirect3DDevice9 — основной файл
│   │   ├── d3d9_types.h        # Все D3D9 типы
│   │   ├── d3d9_texture.*      # Текстуры
│   │   ├── d3d9_shader.*       # Шейдеры
│   │   └── ...
│   ├── vulkan/         # Vulkan backend (~16 файлов)
│   │   ├── vk_pipeline.*       # Pipeline + cache
│   │   ├── vk_cmd_buffer.*     # Command buffers + ring staging
│   │   ├── vk_swapchain.*      # Swapchain
│   │   └── ...
│   ├── shader/         # SM3 → SPIR-V translator
│   │   └── d3d9_sm3_translator.*  # ~65 опкодов
│   └── util/           # Логирование, конфигурация
├── test/               # 39 тестов (SPIR-V + pipeline)
├── android-stub/       # Vulkan stubs для Android
└── winlator-package/   # Winlator integration
```

## Тесты

```bash
meson setup build && meson compile -C build
cd build && ./test_spirv_validation    # 19 тестов
./test_pipeline_integration            # 20 тестов
```

## Лицензия

MIT
