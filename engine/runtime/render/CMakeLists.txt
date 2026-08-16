add_library(Render
render_system.cpp
render_camera.cpp
render_scene.cpp
)

target_include_directories(Render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Graphics PRIVATE: the demo scene (render_scene) records through the RHI's
# frame API; RenderSystem itself stays API-agnostic.
target_link_libraries(Render PUBLIC Core Asset PRIVATE Graphics )