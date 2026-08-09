# test/unit/math/CMakeLists.txt

# Set common properties for all math tests
set(MATH_TEST_LIBRARIES
    Math
    googletest
)

set(MATH_TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/engine
)

# Function to create test with common settings
function(add_math_test TARGET_NAME)
    # Parse arguments (source files)
    set(TEST_SOURCES ${ARGN})
    
    if(NOT TEST_SOURCES)
        message(WARNING "No source files provided for ${TARGET_NAME}")
        return()
    endif()
    
    # Create executable
    add_executable(${TARGET_NAME} ${TEST_SOURCES})
    
    # Apply common settings
    target_link_libraries(${TARGET_NAME} PRIVATE ${MATH_TEST_LIBRARIES})
    target_include_directories(${TARGET_NAME} PRIVATE ${MATH_TEST_INCLUDES})
    
    target_compile_definitions(${TARGET_NAME} PRIVATE
        GTEST_HAS_TR1_TUPLE=0
        _CRT_SECURE_NO_WARNINGS
    )
    
    # Discover tests
    include(GoogleTest)
    gtest_discover_tests(${TARGET_NAME})
endfunction()

# Create individual test targets
add_math_test(KPT_Matrix3fTest
    matrix3f_test/matrix3f_add_test.cpp
    matrix3f_test/matrix3f_subtract_test.cpp
    matrix3f_test/matrix3f_function_test.cpp
    matrix3f_test/matrix3f_multiply_test.cpp
)

add_math_test(KPT_Matrix4fTest
    matrix4f_test/matrix4f_add_test.cpp
    matrix4f_test/matrix4f_subtract_test.cpp
    matrix4f_test/matrix4f_multiply_test.cpp
)

add_math_test(KPT_Vector2fTest
    vector2f_test/vector2f_add_test.cpp
    vector2f_test/vector2f_subtract_test.cpp
    vector2f_test/vector2f_multiply_test.cpp
    vector2f_test/vector2f_division_test.cpp
)

add_math_test(KPT_Vector3fTest
    vector3f_test/vector3f_add_test.cpp
    vector3f_test/vector3f_subtract_test.cpp
    vector3f_test/vector3f_multiply_test.cpp
    vector3f_test/vector3f_division_test.cpp
)

add_math_test(KPT_Vector4fTest
    vector4f_test/vector4f_add_test.cpp
    vector4f_test/vector4f_subtract_test.cpp
    vector4f_test/vector4f_multiply_test.cpp
    vector4f_test/vector4f_division_test.cpp
)