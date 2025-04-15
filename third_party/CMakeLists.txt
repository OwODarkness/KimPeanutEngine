file(GLOB IMGUI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.cpp")

add_library(ThirdPartyLib STATIC 
${IMGUI_SRC}
${CMAKE_CURRENT_SOURCE_DIR}/glad/glad.c
)


#add_subdirectory(googletest)
#include_directories(googletest/googletest/include)


target_link_libraries(ThirdPartyLib 
${CMAKE_CURRENT_SOURCE_DIR}/GLFW/lib/glfw3.lib
${CMAKE_CURRENT_SOURCE_DIR}/assimp/lib/assimp-vc143-mt.lib

)

