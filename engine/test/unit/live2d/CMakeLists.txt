if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

add_executable(Live2DRenderContractTest
    live2d_render_contract_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_render_contract.cpp)

target_include_directories(Live2DRenderContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render)

target_link_libraries(Live2DRenderContractTest PRIVATE KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Live2DRenderContractTest)

add_executable(Live2DModelDataContractTest
    live2d_model_data_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime/live2d_model_data.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render/live2d_render_contract.cpp)

target_include_directories(Live2DModelDataContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/runtime
    ${CMAKE_SOURCE_DIR}/engine/module/live2d/render)

target_link_libraries(Live2DModelDataContractTest PRIVATE KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Live2DModelDataContractTest)

if(KPENGINE_ENABLE_LIVE2D)
    add_executable(Live2DCoreTest
        live2d_core_test.cpp
        live2d_asset_test.cpp)

    target_include_directories(Live2DCoreTest PRIVATE
        "${KPENGINE_CUBISM_FRAMEWORK_SOURCE_DIR}"
        "${KPENGINE_CUBISM_CORE_INCLUDE_DIR}")

    target_link_libraries(Live2DCoreTest PRIVATE
        Live2D
        Live2DImport
        Live2DRuntime
        Live2DCubismFrameworkRuntime
        Live2D::CubismCore
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
    gtest_discover_tests(Live2DCoreTest)
endif()
