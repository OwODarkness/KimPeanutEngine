file(GLOB Shader_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(ShaderLib STATIC ${Shader_SRC}  )