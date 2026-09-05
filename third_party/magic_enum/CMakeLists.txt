add_library(KPThirdPartyMagicEnum INTERFACE)

target_include_directories(KPThirdPartyMagicEnum SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::MagicEnum ALIAS KPThirdPartyMagicEnum)
