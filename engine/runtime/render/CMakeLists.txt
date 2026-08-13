add_library(Render 
render_system.cpp
render_camera.cpp
)

target_include_directories(Render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(Render PUBLIC Core Asset )