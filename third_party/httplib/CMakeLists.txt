add_library(KPThirdPartyHttpLib INTERFACE)

target_include_directories(KPThirdPartyHttpLib SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::HttpLib ALIAS KPThirdPartyHttpLib)
