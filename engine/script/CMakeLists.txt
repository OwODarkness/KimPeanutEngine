add_library(Script STATIC
    script_core.cpp
)

add_subdirectory(lua)

target_link_libraries(Script
PUBLIC
    ScriptLua
)