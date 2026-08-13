#glfw
add_library(glfw INTERFACE)
target_include_directories(glfw INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/glfw/include
)
target_link_libraries(glfw INTERFACE
${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/glfw3.lib
)

#imgui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imgui STATIC 
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends/
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vulkan
)
target_link_libraries(imgui PUBLIC glfw)

#glad
add_library(glad STATIC ${CMAKE_CURRENT_SOURCE_DIR}/glad/src/glad.c)
target_include_directories(glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)


#magic_enum
add_library(magic_enum INTERFACE)
target_include_directories(magic_enum INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/magic_enum/include
)

#lua
add_library(lua INTERFACE)
target_include_directories(lua INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/include

)
target_link_libraries(lua INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/lib/lua53.lib

)

#sol2
add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/sol2/include
    lua
)

#vulkan
find_package(Vulkan REQUIRED)
add_library(VulkanSDK INTERFACE)
target_link_libraries(VulkanSDK INTERFACE
    Vulkan::Vulkan
)

#shaderc
add_library(shaderc INTERFACE)
target_include_directories(shaderc INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/include
)
target_link_libraries(shaderc INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/lib/shaderc_shared.lib
)


add_library(ThirdPartyLib INTERFACE)
target_link_libraries(ThirdPartyLib INTERFACE
    Vulkan::Vulkan
    shaderc
    glad
    miniaudio
)

#miniaudio
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/include
)
target_link_libraries(miniaudio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/lib/miniaudio.lib
)

add_subdirectory(assimp)

#nlohmann
add_library(nlohmann INTERFACE)
target_include_directories(nlohmann INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann/include
)

#stb_image — compiled once (STB_IMAGE_IMPLEMENTATION lives in stb_image_impl.cpp);
#consumers include <stb_image/...> for declarations only.
add_library(stb_image STATIC stb_image/stb_image_impl.cpp)
target_include_directories(stb_image PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/include
)


#httplib
add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/httplib/include
)

#meshoptimize
target_link_libraries(
    ThirdPartyLib INTERFACE
${CMAKE_CURRENT_SOURCE_DIR}/meshoptimizer/lib/meshoptimizer.lib

)

#googletest
add_library(googletest INTERFACE)
target_include_directories(googletest INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/googletest/googletest/include
)
target_link_libraries(googletest INTERFACE
${CMAKE_CURRENT_SOURCE_DIR}/googletest/lib/gtest.lib
${CMAKE_CURRENT_SOURCE_DIR}/googletest/lib/gtest_main.lib
)

#copy dlls
set(THIRD_PARTY_DLLS
    #${CMAKE_CURRENT_SOURCE_DIR}/assimp/bin/assimp-vc143-mt.dll
    #${CMAKE_CURRENT_SOURCE_DIR}/curl/bin/libcurl-x64.dll
     CACHE INTERNAL "Third party dlls"
)


function(copy_thirdparty_dlls target)
    message(STATUS "[DLL COPY] target = ${target}")
    message(STATUS "[DLL LIST] = ${THIRD_PARTY_DLLS}")

    foreach(dll ${THIRD_PARTY_DLLS})
        message(STATUS "[DLL] ${dll}")

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "COPY: ${dll}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${dll}
                $<TARGET_FILE_DIR:${target}>
        )
    endforeach()
endfunction()