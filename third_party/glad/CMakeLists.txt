add_library(KPThirdPartyGlad STATIC
    src/glad.c
)

target_include_directories(KPThirdPartyGlad SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::Glad ALIAS KPThirdPartyGlad)
