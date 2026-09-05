# GoogleTest is a source submodule. Build it through its upstream CMake files
# so each configuration produces its own library and matching debug symbols.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../googletest
    ${CMAKE_CURRENT_BINARY_DIR}/googletest-source
    EXCLUDE_FROM_ALL
)

add_library(KPThirdPartyGoogleTestBundle INTERFACE)
target_link_libraries(KPThirdPartyGoogleTestBundle INTERFACE gtest_main)

add_library(KP::GoogleTest ALIAS KPThirdPartyGoogleTestBundle)
