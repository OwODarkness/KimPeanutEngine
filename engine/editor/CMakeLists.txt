#Editor CMakeListsts.txt
file(GLOB EDITOR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB EDITOR_UICOMPONENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/editor_ui_component/*.cpp")

# Create a library or executable from the editor source files
add_library(EditorLib STATIC ${EDITOR_SRC}  ${EDITOR_UICOMPONENT_SRC})

target_include_directories(EditorLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}/include"
"${CMAKE_CURRENT_SOURCE_DIR}/include/editor_ui_component"
"${CMAKE_SOURCE_DIR}/engine/runtime"
)

target_link_libraries(EditorLib PRIVATE Input Component Object)