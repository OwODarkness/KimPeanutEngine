# Vector2 Arithmetic Operation
set(vector2_test_dir ${CMAKE_SOURCE_DIR}/engine/runtime/test/vector2f_test)
add_executable(KPT_Vector2AddTest
   ${vector2_test_dir}/vector2f_add_test.cpp  # Path to your test file
)

add_executable(KPT_Vector2SubtractTest
    ${vector2_test_dir}/vector2f_subtract_test.cpp  # Path to your test file
)

add_executable(KPT_Vector2MultiplyTest
    ${vector2_test_dir}/vector2f_multiply_test.cpp  # Path to your test file
)

add_executable(KPT_Vector2DivisionTest
    ${vector2_test_dir}/vector2f_division_test.cpp  # Path to your test file
)

# Vector3 Arithmetic Operation
set(vector3_test_dir ${CMAKE_SOURCE_DIR}/engine/runtime/test/vector3f_test)

add_executable(KPT_Vector3AddTest
    ${vector3_test_dir}/vector3f_add_test.cpp  # Path to your test file
)

add_executable(KPT_Vector3MultiplyTest
${vector3_test_dir}/vector3f_multiply_test.cpp  # Path to your test file
)

add_executable(KPT_Vector3SubtractTest
${vector3_test_dir}/vector3f_subtract_test.cpp  # Path to your test file
)

add_executable(KPT_Vector3DivisionTest
    ${vector3_test_dir}/vector3f_division_test.cpp  # Path to your test file
)

# Vector4 Arithmetic Operation
set(vector4_test_dir ${CMAKE_SOURCE_DIR}/engine/runtime/test/vector4f_test)

add_executable(KPT_Vector4AddTest
    ${vector4_test_dir}/vector4f_add_test.cpp  # Path to your test file
)

add_executable(KPT_Vector4MultiplyTest
    ${vector4_test_dir}/vector4f_multiply_test.cpp 
)

add_executable(KPT_Vector4SubtractTest
    ${vector4_test_dir}/vector4f_subtract_test.cpp 
)

add_executable(KPT_Vector4DivisionTest
    ${vector4_test_dir}/vector4f_division_test.cpp 
)

# Matrix Operation
set(mat3_test_dir ${CMAKE_SOURCE_DIR}/engine/runtime/test/matrix3f_test)

add_executable(KPT_Matrix3AddTest
    ${mat3_test_dir}/matrix3f_add_test.cpp
)
add_executable(KPT_Matrix3MultiplyTest
    ${mat3_test_dir}/matrix3f_multiply_test.cpp
)
add_executable(KPT_Matrix3SubtractTest
    ${mat3_test_dir}/matrix3f_subtract_test.cpp
)

set(gtestlib ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/gtest.lib)
set(gtestmainlib ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/gtest_main.lib)

foreach(test_target IN ITEMS 
    KPT_Vector2AddTest
    KPT_Vector2SubtractTest
    KPT_Vector2MultiplyTest
    KPT_Vector2DivisionTest

    KPT_Vector3AddTest
    KPT_Vector3MultiplyTest
    KPT_Vector3SubtractTest
    KPT_Vector3DivisionTest

    KPT_Vector4AddTest
    KPT_Vector4MultiplyTest
    KPT_Vector4SubtractTest
    KPT_Vector4DivisionTest

    KPT_Matrix3AddTest
    KPT_Matrix3MultiplyTest
    KPT_Matrix3SubtractTest
    )

    target_link_libraries(${test_target}
    EngineLib
    ThirdPartyLib
    ${gtestlib}
    ${gtestmainlib}
    )
endforeach(test_target)


# Link the test executable with Google Test and your libraries

# Add the test to CTest
add_test(NAME KPT_Vector2AddTest COMMAND KPT_Vector2AddTest)
add_test(NAME KPT_Vector2SubtractTest COMMAND KPT_Vector2SubtractTest)
add_test(NAME KPT_Vector2MultiplyTest COMMAND KPT_Vector2MultiplyTest)
add_test(NAME KPT_Vector2DivisionTest COMMAND KPT_Vector2DivisionTest)

add_test(NAME KPT_Vector3AddTest COMMAND KPT_Vector3AddTest)
add_test(NAME KPT_Vector3MultiplyTest COMMAND KPT_Vector3MultiplyTest)
add_test(NAME KPT_Vector3SubtractTest COMMAND KPT_Vector3SubtractTest)
add_test(NAME KPT_Vector3DivisionTest COMMAND KPT_Vector3DivisionTest)

add_test(NAME KPT_Vector4AddTest COMMAND KPT_Vector4AddTest)
add_test(NAME KPT_Vector4MultiplyTest COMMAND KPT_Vector4MultiplyTest)
add_test(NAME KPT_Vector4SubtractTest COMMAND KPT_Vector4SubtractTest)
add_test(NAME KPT_Vector4DivisionTest COMMAND KPT_Vector4DivisionTest)

add_test(NAME KPT_Matrix3AddTest COMMAND KPT_Matrix3AddTest)
add_test(NAME KPT_Matrix3MultiplyTest COMMAND KPT_Matrix3MultiplyTest)
add_test(NAME KPT_Matrix3SubtractTest COMMAND KPT_Matrix3SubtractTest)
