# Find the locally installed Live2D Cubism SDK for Native R5.
#
# Cubism Core is proprietary and is intentionally not downloaded or copied into
# this repository. This module is included only when the optional Live2D module
# is enabled.

if(NOT MSVC)
    message(FATAL_ERROR
        "Live2D requires the repository's MSVC build; configure with "
        "-G \"Visual Studio 17 2022\".")
endif()

if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR
        "Live2D Cubism 5 R5 integration requires a 64-bit MSVC target.")
endif()

if(MSVC_VERSION LESS 1930)
    message(FATAL_ERROR
        "Live2D Cubism 5 R5 integration requires the Visual Studio 2022 "
        "toolset (MSVC 143 or newer).")
endif()

if(NOT KPENGINE_CUBISM_SDK_ROOT)
    # Keep the SDK outside the repository while allowing CMake Tools and
    # command-line configure sessions to discover a developer-local install.
    # An explicit -DKPENGINE_CUBISM_SDK_ROOT=... value still has priority.
    if(DEFINED ENV{KPENGINE_CUBISM_SDK_ROOT}
       AND NOT "$ENV{KPENGINE_CUBISM_SDK_ROOT}" STREQUAL "")
        set(KPENGINE_CUBISM_SDK_ROOT "$ENV{KPENGINE_CUBISM_SDK_ROOT}"
            CACHE PATH "Cubism SDK for Native R5 root" FORCE)
    elseif(DEFINED ENV{CUBISM_SDK_ROOT}
           AND NOT "$ENV{CUBISM_SDK_ROOT}" STREQUAL "")
        set(KPENGINE_CUBISM_SDK_ROOT "$ENV{CUBISM_SDK_ROOT}"
            CACHE PATH "Cubism SDK for Native R5 root" FORCE)
    endif()
endif()

if(NOT KPENGINE_CUBISM_SDK_ROOT)
    message(FATAL_ERROR
        "KPENGINE_ENABLE_LIVE2D=ON requires "
        "KPENGINE_CUBISM_SDK_ROOT pointing to the Cubism SDK for Native R5. "
        "Pass -DKPENGINE_CUBISM_SDK_ROOT=... or set the "
        "KPENGINE_CUBISM_SDK_ROOT environment variable.")
endif()

get_filename_component(KPENGINE_CUBISM_SDK_ROOT
    "${KPENGINE_CUBISM_SDK_ROOT}" ABSOLUTE)

set(_kpengine_cubism_core_include
    "${KPENGINE_CUBISM_SDK_ROOT}/Core/include")
set(_kpengine_cubism_core_header
    "${_kpengine_cubism_core_include}/Live2DCubismCore.h")
set(_kpengine_cubism_framework_root
    "${KPENGINE_CUBISM_SDK_ROOT}/Framework")
set(_kpengine_cubism_framework_source
    "${_kpengine_cubism_framework_root}/src")
set(_kpengine_cubism_info
    "${KPENGINE_CUBISM_SDK_ROOT}/cubism-info.yml")

foreach(_required_file
        "${_kpengine_cubism_core_header}"
        "${_kpengine_cubism_framework_root}/src/CubismFramework.cpp"
        "${_kpengine_cubism_framework_root}/src/CubismFramework.hpp"
        "${KPENGINE_CUBISM_SDK_ROOT}/LICENSE.md"
        "${KPENGINE_CUBISM_SDK_ROOT}/Core/LICENSE.md"
        "${KPENGINE_CUBISM_SDK_ROOT}/Core/RedistributableFiles.txt"
        "${KPENGINE_CUBISM_SDK_ROOT}/Framework/LICENSE.md")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR
            "Incomplete Cubism R5 SDK at '${KPENGINE_CUBISM_SDK_ROOT}': "
            "missing '${_required_file}'.")
    endif()
endforeach()

if(NOT EXISTS "${_kpengine_cubism_info}")
    message(FATAL_ERROR
        "Cubism SDK metadata is missing: '${_kpengine_cubism_info}'. "
        "The Live2D integration requires an identifiable R5 package.")
endif()

file(READ "${_kpengine_cubism_info}" _kpengine_cubism_info_text)
string(REGEX MATCH "version:[ \t]*([^\r\n]+)"
    _kpengine_cubism_version_match "${_kpengine_cubism_info_text}")
if(NOT _kpengine_cubism_version_match)
    message(FATAL_ERROR
        "Cubism SDK metadata does not declare a release version in "
        "'${_kpengine_cubism_info}'.")
endif()
string(STRIP "${CMAKE_MATCH_1}" KPENGINE_CUBISM_SDK_VERSION)
if(NOT KPENGINE_CUBISM_SDK_VERSION STREQUAL "5-r.5")
    message(FATAL_ERROR
        "Unsupported Cubism SDK version '${KPENGINE_CUBISM_SDK_VERSION}'; "
        "this project is pinned to Cubism Native R5 (5-r.5).")
endif()

# The engine's CMake targets use MSVC's default DLL CRT unless a target opts
# into another runtime. Keep MD as the default for a clean link with the
# existing engine, while allowing MT as an explicit opt-in because the SDK
# ships both CRT variants. Never silently mix the Core library and module CRT.
set(KPENGINE_LIVE2D_CRT "MD" CACHE STRING
    "MSVC CRT variant used by Cubism Core (MT or MD)")
set_property(CACHE KPENGINE_LIVE2D_CRT PROPERTY STRINGS MT MD)
if(NOT KPENGINE_LIVE2D_CRT STREQUAL "MT" AND
   NOT KPENGINE_LIVE2D_CRT STREQUAL "MD")
    message(FATAL_ERROR
        "KPENGINE_LIVE2D_CRT must be either MT or MD, got "
        "'${KPENGINE_LIVE2D_CRT}'.")
endif()

set(_kpengine_cubism_core_library_root
    "${KPENGINE_CUBISM_SDK_ROOT}/Core/lib/windows/x86_64/143")
set(_kpengine_cubism_core_debug
    "${_kpengine_cubism_core_library_root}/Live2DCubismCore_${KPENGINE_LIVE2D_CRT}d.lib")
set(_kpengine_cubism_core_release
    "${_kpengine_cubism_core_library_root}/Live2DCubismCore_${KPENGINE_LIVE2D_CRT}.lib")

foreach(_core_library "${_kpengine_cubism_core_debug}" "${_kpengine_cubism_core_release}")
    if(NOT EXISTS "${_core_library}")
        message(FATAL_ERROR
            "Cubism R5 Core library is missing for MSVC x64 and CRT "
            "${KPENGINE_LIVE2D_CRT}: '${_core_library}'.")
    endif()
endforeach()

if(TARGET Live2DCubismCore)
    message(FATAL_ERROR
        "Live2DCubismCore target already exists; refusing to reuse a possibly "
        "mixed-version Cubism Core target.")
endif()

add_library(Live2DCubismCore STATIC IMPORTED GLOBAL)
set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
    IMPORTED_LOCATION_DEBUG "${_kpengine_cubism_core_debug}"
    IMPORTED_LOCATION_RELEASE "${_kpengine_cubism_core_release}"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
    INTERFACE_INCLUDE_DIRECTORIES "${_kpengine_cubism_core_include}"
)
add_library(Live2D::CubismCore ALIAS Live2DCubismCore)

set(KPENGINE_CUBISM_CORE_INCLUDE_DIR
    "${_kpengine_cubism_core_include}" CACHE INTERNAL
    "Live2D Core include directory")
set(KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR
    "${_kpengine_cubism_framework_source}" CACHE INTERNAL
    "Live2D Framework source directory")
set(KPENGINE_CUBISM_LICENSE_FILES
    "${KPENGINE_CUBISM_SDK_ROOT}/LICENSE.md"
    "${KPENGINE_CUBISM_SDK_ROOT}/Core/LICENSE.md"
    "${KPENGINE_CUBISM_SDK_ROOT}/Core/RedistributableFiles.txt"
    "${KPENGINE_CUBISM_SDK_ROOT}/Framework/LICENSE.md")

message(STATUS
    "Live2D Cubism ${KPENGINE_CUBISM_SDK_VERSION}: "
    "using local static Core (${KPENGINE_LIVE2D_CRT}) from "
    "${KPENGINE_CUBISM_SDK_ROOT}")
