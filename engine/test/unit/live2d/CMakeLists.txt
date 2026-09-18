if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

add_executable(Live2DRenderContractTest
    live2d_render_contract_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_render_contract.cpp)

target_include_directories(Live2DRenderContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render
    ${CMAKE_SOURCE_DIR}/engine/runtime)

target_link_libraries(Live2DRenderContractTest PRIVATE KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Live2DRenderContractTest PROPERTIES LABELS "live2d")

add_executable(Live2DModelDataContractTest
    live2d_model_data_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime/live2d_model_data.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_render_contract.cpp)

target_include_directories(Live2DModelDataContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render
    ${CMAKE_SOURCE_DIR}/engine/runtime)

target_link_libraries(Live2DModelDataContractTest PRIVATE KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Live2DModelDataContractTest PROPERTIES LABELS "live2d")

add_executable(Live2DEmotionPolicyTest
    live2d_emotion_policy_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime/live2d_emotion_policy.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime/live2d_emotion_resolver.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime/live2d_emotion_replay.cpp)

target_include_directories(Live2DEmotionPolicyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime)

target_link_libraries(Live2DEmotionPolicyTest PRIVATE KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Live2DEmotionPolicyTest PROPERTIES LABELS "live2d")

add_executable(Live2DRenderPlannerTest
    live2d_render_planner_test.cpp
    live2d_submission_hardening_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_mask_planner.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_render_planner.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_render_contract.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_submission.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime/live2d_model_data.cpp)
target_include_directories(Live2DRenderPlannerTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend)
target_link_libraries(Live2DRenderPlannerTest PRIVATE KP::GoogleTest)
include(GoogleTest)
gtest_discover_tests(Live2DRenderPlannerTest PROPERTIES LABELS "live2d")

add_executable(Live2DMaskPlannerTest
    live2d_mask_planner_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_mask_planner.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime/live2d_model_data.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_render_contract.cpp)
target_include_directories(Live2DMaskPlannerTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime)
target_link_libraries(Live2DMaskPlannerTest PRIVATE KP::GoogleTest)
include(GoogleTest)
gtest_discover_tests(Live2DMaskPlannerTest PROPERTIES LABELS "live2d")

if(KPENGINE_ENABLE_LIVE2D)
    add_executable(Live2DCoreTest
        live2d_core_test.cpp
        live2d_asset_test.cpp
        live2d_settings_test.cpp
        live2d_renderer_test.cpp
        ${CMAKE_SOURCE_DIR}/engine/module/live2d/live2d_settings.cpp)

    target_include_directories(Live2DCoreTest PRIVATE
        "${CMAKE_SOURCE_DIR}/engine/module/live2d"
        "${CMAKE_SOURCE_DIR}/engine/module/live2d/render"
        "${CMAKE_SOURCE_DIR}/engine/runtime"
        "${CMAKE_SOURCE_DIR}/engine/runtime/core"
        "${CMAKE_SOURCE_DIR}/engine/test"
        "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}"
        "${KPENGINE_CUBISM_CORE_INCLUDE_DIR}")

    # Live2DRender brings Render, so the renderer test can drive
    # Live2DRenderer::Initialize against the shared backend test double.
    target_link_libraries(Live2DCoreTest PRIVATE
        Live2D
        Live2DImport
        Live2DRuntime
        Live2DCubismFrameworkRuntime
        Live2D::CubismCore
        KP::NlohmannJson
        KP::GoogleTest)

    string(REPLACE "\\" "/" _kpengine_live2d_moc_path
        "${KPENGINE_LIVE2D_MOC_PATH}")
    target_compile_definitions(Live2DCoreTest PRIVATE
        KPENGINE_LIVE2D_MOC_PATH="${_kpengine_live2d_moc_path}")

    set_property(TARGET Live2DCoreTest PROPERTY
        MSVC_RUNTIME_LIBRARY
        "${KPENGINE_LIVE2D_MSVC_RUNTIME}$<$<CONFIG:Debug>:Debug>")

    add_custom_command(TARGET Live2DCoreTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
            $<TARGET_FILE_DIR:Live2DCoreTest>)

    include(GoogleTest)
    gtest_discover_tests(Live2DCoreTest PROPERTIES LABELS "live2d")
endif()

# The bubble's geometry, which is pure CPU: the shape a shader evaluates, with
# no device, no shader, and no Cubism framework.
add_executable(Live2DBubbleLayoutTest
    bubble_layout_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/bubble/bubble_layout.cpp)

target_include_directories(Live2DBubbleLayoutTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/bubble)

target_link_libraries(Live2DBubbleLayoutTest PRIVATE KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Live2DBubbleLayoutTest PROPERTIES LABELS "live2d")

# The bubble's placement and constant packing. Also pure CPU: the pop-in and the
# aspect correction are arithmetic, and the only thing a device would add is the
# shader that consumes the result.
add_executable(Live2DBubbleRenderContractTest
    bubble_render_contract_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/bubble/bubble_layout.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/bubble/bubble_render_contract.cpp)

target_include_directories(Live2DBubbleRenderContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/bubble
    ${CMAKE_SOURCE_DIR}/engine/runtime/core)

target_link_libraries(Live2DBubbleRenderContractTest PRIVATE KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Live2DBubbleRenderContractTest PROPERTIES LABELS "live2d")

# The bubble renderer against the shared backend test double: no device, no
# Cubism, so the text swap and the handle lifetime are assertable in CI.
add_executable(Live2DBubbleRendererTest
    bubble_renderer_test.cpp)

target_include_directories(Live2DBubbleRendererTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/bubble
    ${CMAKE_SOURCE_DIR}/engine/module/panel/runtime
    ${CMAKE_SOURCE_DIR}/engine/test
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend)

target_link_libraries(Live2DBubbleRendererTest PRIVATE Live2DBubble Panel
    KP::GoogleTest)

copy_runtime_dependencies(Live2DBubbleRendererTest)

include(GoogleTest)
gtest_discover_tests(Live2DBubbleRendererTest PROPERTIES LABELS "live2d")
