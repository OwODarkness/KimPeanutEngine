# rgbcx is a single-header CPU BC1-5 encoder. Keep its implementation in one
# translation unit and expose only the private include target to AssetImport.
add_library(KPThirdPartyRgbcx STATIC
    rgbcx_impl.cpp
)

target_include_directories(KPThirdPartyRgbcx SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(KPThirdPartyRgbcx PUBLIC cxx_std_17)

add_library(KP::Rgbcx ALIAS KPThirdPartyRgbcx)
