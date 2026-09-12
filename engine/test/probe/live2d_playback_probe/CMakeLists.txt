# Standalone Cubism R5 conformance probe for the L2D6.0 contract freeze.
#
# This is a probe, not a shipping target. It is deliberately NOT added to the
# parent build: the engine's Live2DCubismFrameworkRuntime intentionally links
# only the audited Model/Utils/Id/Math subset and no Motion translation unit.
# Configure and build it on its own:
#
#   cmake -S engine/test/probe/live2d_playback_probe -B build-l2d6-probe \
#         -G "Visual Studio 17 2022" -A x64
#   cmake --build build-l2d6-probe --config Debug
#
# The probe links the same pinned Core release through the repository's
# FindLive2DCubism.cmake, so a mixed or unsupported SDK still fails loudly.

cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(Live2DPlaybackProbe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake")
find_package(Live2DCubism REQUIRED)

set(_probe_framework_sources
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/CubismFramework.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Utils/CubismDebug.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Utils/CubismJson.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Utils/CubismString.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Id/CubismId.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Id/CubismIdManager.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Math/CubismMath.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Math/CubismMatrix44.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Math/CubismVector2.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Model/CubismMoc.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Model/CubismModel.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Model/CubismModelMultiplyAndScreenColor.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Rendering/csmBlendMode.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Type/csmRectF.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Type/csmString.cpp"
    # The Motion units the engine does not currently compile or link.
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/ACubismMotion.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/CubismMotion.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/CubismMotionJson.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/CubismMotionQueueEntry.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/CubismMotionQueueManager.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/CubismMotionManager.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/CubismExpressionMotion.cpp"
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Motion/CubismExpressionMotionManager.cpp")

foreach(_probe_source IN LISTS _probe_framework_sources)
    if(NOT EXISTS "${_probe_source}")
        message(FATAL_ERROR
            "Cubism Framework R5 source layout changed; missing "
            "'${_probe_source}'.")
    endif()
endforeach()

add_executable(Live2DPlaybackProbe
    main.cpp
    renderer_static_release.cpp
    ${_probe_framework_sources})

target_include_directories(Live2DPlaybackProbe SYSTEM PRIVATE
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}"
    "${KPENGINE_CUBISM_CORE_INCLUDE_DIR}")

target_link_libraries(Live2DPlaybackProbe PRIVATE Live2D::CubismCore)

if(KPENGINE_LIVE2D_CRT STREQUAL "MD")
    set(_probe_runtime_debug "MultiThreadedDebugDLL")
    set(_probe_runtime_release "MultiThreadedDLL")
else()
    set(_probe_runtime_debug "MultiThreadedDebug")
    set(_probe_runtime_release "MultiThreaded")
endif()
set_property(TARGET Live2DPlaybackProbe PROPERTY MSVC_RUNTIME_LIBRARY
    "$<$<CONFIG:Debug>:${_probe_runtime_debug}>$<$<NOT:$<CONFIG:Debug>>:${_probe_runtime_release}>")

set(KPENGINE_LIVE2D_MOC_PATH
    "${KPENGINE_CUBISM_SDK_ROOT}/Samples/Resources/Wanko/Wanko.moc3"
    CACHE FILEPATH "External .moc3 fixture used by the probe")
target_compile_definitions(Live2DPlaybackProbe PRIVATE
    "KPENGINE_LIVE2D_MOC_PATH=\"${KPENGINE_LIVE2D_MOC_PATH}\"")
