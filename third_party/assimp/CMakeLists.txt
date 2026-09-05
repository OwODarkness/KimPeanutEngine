add_library(KPThirdPartyAssimp SHARED IMPORTED GLOBAL)

set_target_properties(KPThirdPartyAssimp PROPERTIES
    IMPORTED_IMPLIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/assimp-vc143-mt.lib
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/bin/assimp-vc143-mt.dll
    INTERFACE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::Assimp ALIAS KPThirdPartyAssimp)
