# The imported library and matching Debug PDB are packaged together under lib/.
add_library(KPThirdPartyMiniaudio STATIC IMPORTED GLOBAL)

set_target_properties(KPThirdPartyMiniaudio PROPERTIES
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/miniaudio.lib
    INTERFACE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::Miniaudio ALIAS KPThirdPartyMiniaudio)
