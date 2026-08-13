# Generic Lua hosting layer (engine-agnostic). sol2 is header-only; lua is a
# static lib — both PUBLIC because lua_vm.h exposes sol2 types in its API.
add_library(ScriptLua STATIC
    lua_vm.cpp
)

target_include_directories(ScriptLua PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(ScriptLua
PUBLIC
    lua
    sol2
PRIVATE
    Log
)

# sol2 builds without runtime type checks unless told otherwise.
target_compile_definitions(ScriptLua PUBLIC
    SOL_ALL_SAFETIES_ON
)
