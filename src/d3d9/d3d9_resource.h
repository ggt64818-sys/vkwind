#pragma once

#include "d3d9_types.h"
#include <vector>
#include <mutex>

namespace vkwind {

// Resource tracking for proper lifecycle management
class D3D9ResourceManager {
public:
  static D3D9ResourceManager& instance();

  // Track resources
  void track_resource(void* resource, D3DRESOURCETYPE type);
  void untrack_resource(void* resource);

  // Check if resource is valid
  bool is_valid(void* resource) const;

  // Get resource stats
  uint32_t get_resource_count() const;
  uint32_t get_resource_count_by_type(D3DRESOURCETYPE type) const;

private:
  D3D9ResourceManager() = default;

  struct ResourceEntry {
    void* pointer = nullptr;
    D3DRESOURCETYPE type = D3DRTYPE_SURFACE;
    bool active = false;
  };

  std::vector<ResourceEntry> m_resources;
  mutable std::mutex m_mutex;
};

} // namespace vkwind
