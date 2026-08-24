add_library(Render
render_system.cpp
frame_context.cpp
render_pass.cpp
render_target.cpp
render_camera.cpp
render_scene.cpp
)

target_include_directories(Render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Graphics PRIVATE: RenderSystem owns the API-neutral RenderBackend facade;
# scenes record through the common command-recorder contract.
target_link_libraries(Render PUBLIC Core Asset PRIVATE Graphics )
