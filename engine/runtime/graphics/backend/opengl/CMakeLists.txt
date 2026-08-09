add_library(OpenglBackend STATIC
    opengl_backend.cpp
   opengl_texture.cpp
    opengl_shader.cpp
    opengl_sampler.cpp
   opengl_mesh.cpp
    opengl_pipeline.cpp
    opengl_descriptorset.cpp
)

target_include_directories(OpenglBackend
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(OpenglBackend PUBLIC USE_OPENGL)
target_link_libraries(OpenglBackend PUBLIC glad Core PRIVATE Data Asset glfw)
