add_library(KPThirdPartyStbImage STATIC
    stb_image_impl.cpp
)

target_include_directories(KPThirdPartyStbImage SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::StbImage ALIAS KPThirdPartyStbImage)
