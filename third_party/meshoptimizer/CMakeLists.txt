add_library(KPThirdPartyMeshoptimizer STATIC IMPORTED GLOBAL)

set_target_properties(KPThirdPartyMeshoptimizer PROPERTIES
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/meshoptimizer.lib
)

add_library(KP::Meshoptimizer ALIAS KPThirdPartyMeshoptimizer)
