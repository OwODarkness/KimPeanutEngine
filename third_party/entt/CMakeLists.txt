# EnTT is header-only. Keep this wrapper separate from the upstream build tree
# so the project does not import EnTT's tests, tools, or build configuration.

add_library(KPThirdPartyEnTT INTERFACE)
add_library(EnTT::EnTT ALIAS KPThirdPartyEnTT)

target_include_directories(KPThirdPartyEnTT SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# This vendored copy is EnTT 3.16.0 and follows the engine's C++17 baseline.
target_compile_features(KPThirdPartyEnTT INTERFACE cxx_std_17)
