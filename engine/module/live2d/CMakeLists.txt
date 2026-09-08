if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

find_package(Live2DCubism REQUIRED)

set(_kpengine_live2d_framework_sources
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
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}/Type/csmString.cpp")

foreach(_framework_source IN LISTS _kpengine_live2d_framework_sources)
    if(NOT EXISTS "${_framework_source}")
        message(FATAL_ERROR
            "Cubism Framework R5 source layout changed; missing "
            "'${_framework_source}'. Update the audited Live2D source list "
            "only after revalidating the pinned SDK release.")
    endif()
endforeach()

add_library(Live2DCubismFrameworkRuntime STATIC
    ${_kpengine_live2d_framework_sources}
    runtime/cubism_renderer_static_release.cpp)

target_include_directories(Live2DCubismFrameworkRuntime
    SYSTEM PRIVATE
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}"
    "${KPENGINE_CUBISM_CORE_INCLUDE_DIR}")

target_link_libraries(Live2DCubismFrameworkRuntime
    PRIVATE Live2D::CubismCore)

set(_kpengine_live2d_msvc_runtime "MultiThreaded")
if(KPENGINE_LIVE2D_CRT STREQUAL "MD")
    set(_kpengine_live2d_msvc_runtime "MultiThreadedDLL")
endif()
set_property(TARGET Live2DCubismFrameworkRuntime PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")
set(KPENGINE_LIVE2D_MSVC_RUNTIME
    "${_kpengine_live2d_msvc_runtime}"
    CACHE INTERNAL "Live2D target MSVC runtime base")

add_library(Live2DRuntime STATIC
    runtime/live2d_cubism_lifecycle.cpp
    runtime/live2d_system.cpp)
target_include_directories(Live2DRuntime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_include_directories(Live2DRuntime SYSTEM PRIVATE
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}"
    "${KPENGINE_CUBISM_CORE_INCLUDE_DIR}")
target_link_libraries(Live2DRuntime
    PUBLIC Core Log
    PRIVATE Live2DCubismFrameworkRuntime)
set_property(TARGET Live2DRuntime PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2D INTERFACE)
target_link_libraries(Live2D INTERFACE Live2DRuntime)

add_library(Live2DModule STATIC
    live2d_module.cpp)
target_include_directories(Live2DModule PUBLIC
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(Live2DModule PUBLIC Live2D)
set_property(TARGET Live2DModule PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2DEditor STATIC
    editor/live2d_editor_viewer_component.cpp
    editor/live2d_editor_registration.cpp)
target_include_directories(Live2DEditor PUBLIC
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(Live2DEditor PRIVATE EditorUILib Live2D KP::ImGui)
set_property(TARGET Live2DEditor PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

set(KPENGINE_LIVE2D_MOC_PATH
    "${KPENGINE_CUBISM_SDK_ROOT}/Samples/Resources/Wanko/Wanko.moc3"
    CACHE FILEPATH
    "Optional external .moc3 fixture used by Live2DCoreTest")

if(NOT TARGET Live2DCubismFrameworkRuntime)
    message(FATAL_ERROR "Live2D Framework target was not created")
endif()
