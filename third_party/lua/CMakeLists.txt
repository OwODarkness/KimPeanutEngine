add_library(KPThirdPartyLua STATIC IMPORTED GLOBAL)

set_target_properties(KPThirdPartyLua PROPERTIES
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/lua53.lib
    INTERFACE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::Lua ALIAS KPThirdPartyLua)
