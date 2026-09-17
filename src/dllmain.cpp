// vkwind - Direct3D 9 to Vulkan translation layer
// DLL entry point

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "d3d9/d3d9.h"
#include "d3d9/d3d9_device.h"
#include "util/util_log.h"
#include "util/util_config.h"

#include <cstdlib>
#include <cstdint>

static const char* kTag = "DLL";

// --- Exported functions ---

extern "C" {

// Main D3D9 creation function
void* __stdcall Direct3DCreate9(uint32_t SDKVersion) {
  VKWIND_INFO(kTag, "Direct3DCreate9 called (SDK=%u)", SDKVersion);

  // Initialize config from environment
  auto& config = vkwind::Config::instance();
  config.load_from_env();

  // Set up logging
  if (!config.log_file.empty()) {
    vkwind::Logger::instance().set_log_file(config.log_file.c_str());
  }
  vkwind::Logger::instance().set_log_level(static_cast<vkwind::LogLevel>(config.log_level));

  if (SDKVersion != D3D_SDK_VERSION) {
    VKWIND_WARN(kTag, "SDK version mismatch: got %u, expected %u", SDKVersion, D3D_SDK_VERSION);
  }

  auto* d3d = new vkwind::D3D9();
  return static_cast<void*>(d3d);
}

// Extended version (D3D9Ex)
void* __stdcall Direct3DCreate9Ex(uint32_t SDKVersion, void** ppD3D9Ex) {
  VKWIND_INFO(kTag, "Direct3DCreate9Ex called (SDK=%u)", SDKVersion);

  auto& config = vkwind::Config::instance();
  config.load_from_env();

  if (!config.log_file.empty()) {
    vkwind::Logger::instance().set_log_file(config.log_file.c_str());
  }
  vkwind::Logger::instance().set_log_level(static_cast<vkwind::LogLevel>(config.log_level));

  auto* d3d = new vkwind::D3D9();
  *ppD3D9Ex = static_cast<void*>(d3d);
  return d3d;
}

} // extern "C"

#ifdef _WIN32

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
  (void)hModule;
  (void)lpReserved;

  switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
      DisableThreadLibraryCalls(hModule);

      VKWIND_INFO(kTag, "vkwind DLL attached (PID=%u)", GetCurrentProcessId());

      auto& config = vkwind::Config::instance();
      config.load_from_env();

      if (!config.log_file.empty()) {
        vkwind::Logger::instance().set_log_file(config.log_file.c_str());
      }
      vkwind::Logger::instance().set_log_level(static_cast<vkwind::LogLevel>(config.log_level));

      VKWIND_INFO(kTag, "vkwind initialized");
      break;
    }

    case DLL_PROCESS_DETACH:
      VKWIND_INFO(kTag, "vkwind DLL detached");
      break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
      break;
  }

  return TRUE;
}

#endif // _WIN32
