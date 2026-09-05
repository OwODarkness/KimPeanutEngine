add_library(KPThirdPartySol2 INTERFACE)

target_include_directories(KPThirdPartySol2 SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(KPThirdPartySol2 INTERFACE
    KP::Lua
)

add_library(KP::Sol2 ALIAS KPThirdPartySol2)
