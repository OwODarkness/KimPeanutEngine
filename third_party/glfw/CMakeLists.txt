add_library(KPThirdPartyGlfw STATIC IMPORTED GLOBAL)

set_target_properties(KPThirdPartyGlfw PROPERTIES
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw3.lib
    INTERFACE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::Glfw ALIAS KPThirdPartyGlfw)
