cmake_minimum_required(VERSION 3.5.0)
project(KimPeanutEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(
    third_party
    third_party/googletest/googletest/include
    third_party/vulkan/
    )

add_subdirectory(engine)
add_subdirectory(third_party)

set(THIRD_PARTY_DLLS
    ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
)

function(copy_thirdparty_dlls target)
    foreach(dll ${THIRD_PARTY_DLLS})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${dll}
            $<TARGET_FILE_DIR:${target}>
        )
    endforeach()
endfunction()


add_executable(KimPeanutEngine 
    engine/editor/main.cpp
)
target_link_libraries(KimPeanutEngine 
    EngineLib
    ThirdPartyLib
)

copy_thirdparty_dlls(KimPeanutEngine)

#TEST
# enable_testing()
# add_subdirectory(engine/test)
