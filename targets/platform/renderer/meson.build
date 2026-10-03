renderer_opt = get_option('renderer')

if renderer_opt == 'vulkan'
    vulkan_dep = dependency('vulkan')
    platform_renderer_vk_dependencies = [
        dependency('sdl2'),
        dependency('glm'),
        dependency('stb'),
        vulkan_dep,
        java_dep,
    ]

    lib_platform_renderer = static_library(
        'platform_renderer_vk',
        files(
            'vk/VKRenderer.cpp',
            'vk/render_stubs.cpp', # <--- APUNTAR AL DE VULKAN
        ),
        include_directories: include_directories('../../'),
        dependencies: platform_renderer_vk_dependencies,
        cpp_args: ['-DUSE_VULKAN'] + global_cpp_args + global_cpp_defs,
    )  
else
    platform_renderer_gl_dependencies = [
        dependency('sdl2'),
        dependency('glm'),
        dependency('stb'),
        java_dep,
    ]
    platform_renderer_gl_defs = []

    if renderer_opt == 'gles'
        platform_renderer_gl_dependencies += dependency('glesv2')
        platform_renderer_gl_defs = ['-DGLES']
    else
        # Modo 'gl3' estándar
        platform_renderer_gl_dependencies += [dependency('gl'), dependency('glew')]
        platform_renderer_gl_defs = []
    endif

    lib_platform_renderer = static_library(
        'platform_renderer_gl',
        files('gl/GLRenderer.cpp', 'gl/render_stubs.cpp'),
        include_directories: include_directories('../../'),
        dependencies: platform_renderer_gl_dependencies,
        cpp_args: platform_renderer_gl_defs + global_cpp_args + global_cpp_defs,
    )
endif

platform_renderer_gl_dep = declare_dependency(
    link_with: lib_platform_renderer,
)