#include "d3d9_resource.h"
#include "../util/util_log.h"
#include <algorithm>

namespace vkwind {

static const char* kTag = "D3D9Resource";

D3D9ResourceManager& D3D9ResourceManager::instance() {
  static D3D9ResourceManager s_instance;
  return s_instance;
}

void D3D9ResourceManager::track_resource(void* resource, D3DRESOURCETYPE type) {
  std::lock_guard lock(m_mutex);

  // Check if already tracked
  for (auto& entry : m_resources) {
    if (entry.pointer == resource && entry.active) {
      return;
    }
  }

  // Find empty slot
  for (auto& entry : m_resources) {
    if (!entry.active) {
      entry.pointer = resource;
      entry.type = type;
      entry.active = true;
      return;
    }
  }

  // Add new entry
  ResourceEntry entry;
  entry.pointer = resource;
  entry.type = type;
  entry.active = true;
  m_resources.push_back(entry);
}

void D3D9ResourceManager::untrack_resource(void* resource) {
  std::lock_guard lock(m_mutex);
  for (auto& entry : m_resources) {
    if (entry.pointer == resource) {
      entry.active = false;
      return;
    }
  }
}

bool D3D9ResourceManager::is_valid(void* resource) const {
  std::lock_guard lock(m_mutex);
  for (const auto& entry : m_resources) {
    if (entry.pointer == resource && entry.active) {
      return true;
    }
  }
  return false;
}

uint32_t D3D9ResourceManager::get_resource_count() const {
  std::lock_guard lock(m_mutex);
  uint32_t count = 0;
  for (const auto& entry : m_resources) {
    if (entry.active) count++;
  }
  return count;
}

uint32_t D3D9ResourceManager::get_resource_count_by_type(D3DRESOURCETYPE type) const {
  std::lock_guard lock(m_mutex);
  uint32_t count = 0;
  for (const auto& entry : m_resources) {
    if (entry.active && entry.type == type) count++;
  }
  return count;
}

} // namespace vkwind
