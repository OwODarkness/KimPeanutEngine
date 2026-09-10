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

add_library(Live2DProduct STATIC
    runtime/live2d_product.cpp)
target_include_directories(Live2DProduct PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
set_property(TARGET Live2DProduct PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2DRender STATIC
    render/live2d_render_contract.cpp
    render/live2d_render_planner.cpp)
target_include_directories(Live2DRender PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/runtime"
    "${CMAKE_SOURCE_DIR}/engine/runtime/core"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Live2DRender PUBLIC Render)
set_property(TARGET Live2DRender PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2DAssetRuntime STATIC
    runtime/live2d_asset_runtime.cpp)
target_include_directories(Live2DAssetRuntime PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/runtime"
    "${CMAKE_SOURCE_DIR}/engine/runtime/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_link_libraries(Live2DAssetRuntime PUBLIC Live2DProduct AssetRuntime)
set_property(TARGET Live2DAssetRuntime PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2DImport STATIC
    runtime/live2d_import.cpp)
target_include_directories(Live2DImport PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/runtime"
    "${CMAKE_SOURCE_DIR}/engine/runtime/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_link_libraries(Live2DImport PUBLIC Live2DProduct AssetImport
    PRIVATE KP::NlohmannJson)
set_property(TARGET Live2DImport PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2DRuntime STATIC
    runtime/live2d_cubism_lifecycle.cpp
    runtime/live2d_model_data.cpp
    runtime/live2d_model_instance.cpp
    runtime/live2d_system.cpp)
target_include_directories(Live2DRuntime PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/runtime"
    "${CMAKE_SOURCE_DIR}/engine/runtime/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_include_directories(Live2DRuntime SYSTEM PRIVATE
    "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}"
    "${KPENGINE_CUBISM_CORE_INCLUDE_DIR}")
target_link_libraries(Live2DRuntime
    PUBLIC Core Log AssetRuntime Live2DProduct
    PRIVATE Live2DCubismFrameworkRuntime)
set_property(TARGET Live2DRuntime PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2DRegistration STATIC
    runtime/live2d_registration.cpp)
target_include_directories(Live2DRegistration PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/runtime"
    "${CMAKE_SOURCE_DIR}/engine/runtime/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_link_libraries(Live2DRegistration PUBLIC Live2DProduct Live2DAssetRuntime
    AssetRuntime)
set_property(TARGET Live2DRegistration PROPERTY
    MSVC_RUNTIME_LIBRARY "${_kpengine_live2d_msvc_runtime}$<$<CONFIG:Debug>:Debug>")

add_library(Live2D INTERFACE)
target_link_libraries(Live2D INTERFACE Live2DRuntime Live2DProduct
    Live2DAssetRuntime Live2DRegistration Live2DRender)

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
