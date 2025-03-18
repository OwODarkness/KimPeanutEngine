add_executable(KPT_VectorAddTest
    ${CMAKE_SOURCE_DIR}/engine/runtime/test/vector3f_add_test.cpp  # Path to your test file
)

add_executable(KPT_VectorMultiplyTest
    ${CMAKE_SOURCE_DIR}/engine/runtime/test/vector3f_multiply_test.cpp  # Path to your test file
)

add_executable(KPT_VectorSubtractTest
    ${CMAKE_SOURCE_DIR}/engine/runtime/test/vector3f_subtract_test.cpp  # Path to your test file
)

add_executable(KPT_VectorDivisionTest
    ${CMAKE_SOURCE_DIR}/engine/runtime/test/vector3f_division_test.cpp  # Path to your test file
)


# set(gtestlib ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/gtest.lib)
# set(gtestmainlib ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/gtest_main.lib)

foreach(test_target IN ITEMS 
    KPT_VectorAddTest
    KPT_VectorMultiplyTest
    KPT_VectorSubtractTest
    KPT_VectorDivisionTest
    )

    target_link_libraries(${test_target}
    EngineLib
    ThirdPartyLib
    ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/gtest.lib
    ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/gtest_main.lib
    )
endforeach(test_target)


# Link the test executable with Google Test and your libraries

# Add the test to CTest
add_test(NAME KPT_VectorAddTest COMMAND KPT_VectorAddTest)
add_test(NAME KPT_VectorMultiplyTest COMMAND KPT_VectorMultiplyTest)
add_test(NAME KPT_VectorSubtractTest COMMAND KPT_VectorSubtractTest)
add_test(NAME KPT_VectorDivisionTest COMMAND KPT_VectorDivisionTest)


