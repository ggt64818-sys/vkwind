project('vkwind', 'cpp',
  version : '0.5.0-alpha',
  license : 'MIT',
  default_options : [
    'cpp_std=c++20',
    'warning_level=3',
    'optimization=2',
  ],
)

# Platform-specific flags
if host_machine.system() == 'windows'
  add_project_arguments(
    '-DVKWIND_EXPORT=__declspec(dllexport)',
    '-DVKWIND_API=__declspec(dllimport)',
    '-DNOMINMAX',
    '-D_CRT_SECURE_NO_WARNINGS',
    language : 'cpp'
  )
elif host_machine.system() == 'android'
  add_project_arguments(
    '-DVKWIND_EXPORT=',
    '-DVKWIND_API=',
    '-DVK_USE_PLATFORM_ANDROID_KHR',
    language : 'cpp'
  )
endif

# Dependencies
if host_machine.system() == 'android'
  # Android: Vulkan is a system library (libvulkan.so) loaded at runtime
  # Use a stub library for linking, real functions resolved at runtime
  vulkan_stub = static_library('vulkan_stub',
    'android-stub/libvulkan_stub.cpp',
  )
  vulkan_dep = declare_dependency(
    link_with : vulkan_stub,
  )
else
  vulkan_dep = dependency('vulkan', required : true)
endif

# d3d9 headers (we provide our own)
inc_dirs = include_directories('src')

# Source files
vkwind_sources = files(
  # Vulkan backend
  'src/vulkan/vk_instance.cpp',
  'src/vulkan/vk_device.cpp',
  'src/vulkan/vk_swapchain.cpp',
  'src/vulkan/vk_pipeline.cpp',
  'src/vulkan/vk_memory.cpp',
  'src/vulkan/vk_cmd_buffer.cpp',
  'src/vulkan/vk_buffer.cpp',
  'src/vulkan/vk_image.cpp',
  # D3D9 implementation
  'src/d3d9/d3d9.cpp',
  'src/d3d9/d3d9_device.cpp',
  'src/d3d9/d3d9_texture.cpp',
  'src/d3d9/d3d9_surface.cpp',
  'src/d3d9/d3d9_vertex_buffer.cpp',
  'src/d3d9/d3d9_shader.cpp',
  'src/d3d9/d3d9_state.cpp',
  'src/d3d9/d3d9_volume_texture.cpp',
  'src/d3d9/d3d9_cube_texture.cpp',
  'src/d3d9/d3d9_resource.cpp',
  'src/d3d9/d3d9_swapchain.cpp',
  'src/d3d9/d3d9_state_mapper.cpp',
  'src/d3d9/d3d9_fvf.cpp',
  'src/d3d9/d3d9_texture_manager.cpp',
  # Shader translation
  'src/shader/shader_translator.cpp',
  'src/shader/d3d9_sm3_translator.cpp',
  # Utilities
  'src/util/util_log.cpp',
  'src/util/util_config.cpp',
  # DLL entry
  'src/dllmain.cpp',
)

# Build the shared library
vkwind_lib = shared_library('d3d9',
  vkwind_sources,
  dependencies : [vulkan_dep],
  include_directories : inc_dirs,
  name_prefix : '',
  install : true,
)

# Alias for use as subproject
vkwind_dep = declare_dependency(
  link_with : vkwind_lib,
  include_directories : inc_dirs,
)

# ---- Tests (Windows only) ----
if host_machine.system() == 'windows'
  # ---- Test app ----
  test_d3d9 = executable('test_d3d9',
    'test/test_d3d9.cpp',
    include_directories : inc_dirs,
    install : false,
  )

  # ---- SPIR-V Validation test ----
  test_spirv_sources = files(
    'test/test_spirv_validation.cpp',
    'src/shader/d3d9_sm3_translator.cpp',
    'src/util/util_log.cpp',
    'src/util/util_config.cpp',
  )

  test_spirv = executable('test_spirv_validation',
    test_spirv_sources,
    include_directories : inc_dirs,
    install : false,
  )

  # ---- Pipeline Integration test (Phases 9.4 + 10.5) ----
  test_pipeline_sources = files(
    'test/test_pipeline_integration.cpp',
    'src/shader/d3d9_sm3_translator.cpp',
    'src/d3d9/d3d9_fvf.cpp',
    'src/d3d9/d3d9_state_mapper.cpp',
    'src/util/util_log.cpp',
    'src/util/util_config.cpp',
  )

  test_pipeline = executable('test_pipeline_integration',
    test_pipeline_sources,
    include_directories : inc_dirs,
    install : false,
  )
endif
