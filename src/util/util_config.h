#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>

namespace vkwind {

struct Config {
  // Vulkan settings
  bool enable_validation = false;
  bool enable_async = true;
  uint32_t max_frame_latency = 2;

  // D3D9 settings
  bool force_d3d9Ex = false;
  bool enable_d3d9on12 = false;

  // Rendering
  bool vsync = true;
  bool allow_tearing = false;
  uint32_t framerate_limit = 0;

  // Memory
  uint64_t max_device_memory = 0;
  uint64_t max_shared_memory = 256 * 1024 * 1024;

  // Logging
  std::string log_file;
  int log_level = 2;

  // GPU selection
  int gpu_index = -1;

  // Shader cache
  bool enable_shader_cache = true;
  std::string shader_cache_dir;

  static Config& instance();
  void load_from_env();
  void load_from_file(const char* path);
};

} // namespace vkwind
