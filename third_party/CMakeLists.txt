file(GLOB IMGUI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.cpp")

add_library(ThirdPartyLib STATIC ${IMGUI_SRC})


target_link_libraries(ThirdPartyLib 
${CMAKE_CURRENT_SOURCE_DIR}/glad/lib/glad.lib
${CMAKE_CURRENT_SOURCE_DIR}/GLFW/lib/glfw3.lib)

