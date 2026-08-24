add_executable(
    GraphicsContractTest
    graphics_contract_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend/common/pipeline_validation.cpp
)

target_include_directories(GraphicsContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend
)

target_link_libraries(GraphicsContractTest PRIVATE
    googletest
    VulkanSDK
)

include(GoogleTest)
gtest_discover_tests(GraphicsContractTest)
