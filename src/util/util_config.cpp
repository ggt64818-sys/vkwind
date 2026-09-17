#include "util_config.h"
#include "util_log.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vkwind {

static const char* kTag = "Config";

Config& Config::instance() {
  static Config s_instance;
  return s_instance;
}

static bool parse_bool(const char* str) {
  if (!str) return false;
  return strcmp(str, "1") == 0 || strcmp(str, "true") == 0 || strcmp(str, "yes") == 0;
}

void Config::load_from_env() {
  const char* val;

  val = getenv("VKWIND_VALIDATION");
  if (val) enable_validation = parse_bool(val);

  val = getenv("VKWIND_ASYNC");
  if (val) enable_async = parse_bool(val);

  val = getenv("VKWIND_FRAME_LATENCY");
  if (val) max_frame_latency = std::atoi(val);

  val = getenv("VKWIND_VSYNC");
  if (val) vsync = parse_bool(val);

  val = getenv("VKWIND_FRAMERATE_LIMIT");
  if (val) framerate_limit = std::atoi(val);

  val = getenv("VKWIND_GPU");
  if (val) gpu_index = std::atoi(val);

  val = getenv("VKWIND_LOG_FILE");
  if (val) log_file = val;

  val = getenv("VKWIND_LOG_LEVEL");
  if (val) log_level = std::atoi(val);

  val = getenv("VKWIND_MAX_DEVICE_MEMORY");
  if (val) max_device_memory = std::strtoull(val, nullptr, 10);

  val = getenv("VKWIND_SHADER_CACHE");
  if (val) enable_shader_cache = parse_bool(val);

  val = getenv("VKWIND_SHADER_CACHE_DIR");
  if (val) shader_cache_dir = val;

  VKWIND_INFO(kTag, "Config loaded from environment variables");
  VKWIND_DBG(kTag, "  validation=%d, async=%d, vsync=%d, gpu=%d",
    enable_validation, enable_async, vsync, gpu_index);
}

void Config::load_from_file(const char* path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    VKWIND_DBG(kTag, "No config file at %s, using defaults", path);
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '[') continue;

    auto pos = line.find('=');
    if (pos == std::string::npos) continue;

    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);

    // Trim whitespace
    auto trim = [](std::string& s) {
      s.erase(0, s.find_first_not_of(" \t\r\n"));
      s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };
    trim(key);
    trim(value);

    if (key == "validation") enable_validation = parse_bool(value.c_str());
    else if (key == "async") enable_async = parse_bool(value.c_str());
    else if (key == "frame_latency") max_frame_latency = std::atoi(value.c_str());
    else if (key == "vsync") vsync = parse_bool(value.c_str());
    else if (key == "framerate_limit") framerate_limit = std::atoi(value.c_str());
    else if (key == "gpu") gpu_index = std::atoi(value.c_str());
    else if (key == "log_file") log_file = value;
    else if (key == "log_level") log_level = std::atoi(value.c_str());
    else if (key == "max_device_memory") max_device_memory = std::strtoull(value.c_str(), nullptr, 10);
    else if (key == "shader_cache") enable_shader_cache = parse_bool(value.c_str());
    else if (key == "shader_cache_dir") shader_cache_dir = value;
  }

  VKWIND_INFO(kTag, "Config loaded from %s", path);
}

} // namespace vkwind
