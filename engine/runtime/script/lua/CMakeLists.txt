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
    KP::Lua
    KP::Sol2
PRIVATE
    Log
)

# lua53.lib is a prebuilt /MT artifact, while engine consumers use /MD or
# /MDd. Suppress only its embedded LIBCMT directive at this integration edge;
# do not change the runtime-library choice for the rest of the engine.
if(MSVC)
    target_link_options(ScriptLua INTERFACE "/NODEFAULTLIB:LIBCMT")
endif()

# sol2 builds without runtime type checks unless told otherwise.
target_compile_definitions(ScriptLua PUBLIC
    SOL_ALL_SAFETIES_ON
)
