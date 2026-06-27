add_library(ScriptLua STATIC)

target_sources(ScriptLua
PRIVATE
    Lua_vm.cpp

)

target_include_directories(ScriptLua
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ScriptLua
PUBLIC
    lua
    sol2
PRIVATE
    Log
)