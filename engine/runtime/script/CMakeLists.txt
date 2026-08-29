# Engine-facing scripting layer. `script_core.cpp` is the seam the engine
# bindings (kpengine classes -> Lua) land in; LuaVM stays engine-agnostic.
add_library(Script STATIC
    script_core.cpp
    command/lua_command_bridge.cpp
)

add_subdirectory(lua)

target_include_directories(Script PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Script
PUBLIC
    ScriptLua
PRIVATE
    RuntimeCommand
)
