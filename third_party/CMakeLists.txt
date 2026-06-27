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
# target_include_directories(ThirdPartyLib PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/vulkan
# )
# target_link_libraries(ThirdPartyLib 
# ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/lib/shaderc_shared.lib
# ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/lib/vulkan-1.lib
# )


#miniaudio
add_library(miniaudio INTERFACE)

target_include_directories(miniaudio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/include
)

target_link_libraries(miniaudio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/lib/miniaudio.lib
)


#assimp
add_library(assimp INTERFACE)

target_include_directories(assimp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/assimp/include
)

target_link_libraries(assimp INTERFACE
${CMAKE_CURRENT_SOURCE_DIR}/assimp/lib/assimp-vc143-mt.lib
)


#nlohmann

add_library(nlohmann INTERFACE)

target_include_directories(nlohmann INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann/include
)

#stb_image
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/include
)
#httplib
add_library(httplib INTERFACE)

target_include_directories(httplib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/httplib/include
)

set(THIRD_PARTY_DLLS
    ${CMAKE_CURRENT_SOURCE_DIR}/assimp/bin/assimp-vc143-mt.dll
    #${CMAKE_CURRENT_SOURCE_DIR}/curl/bin/libcurl-x64.dll
     CACHE INTERNAL "Third party dlls"
)

target_link_libraries(
    ThirdPartyLib INTERFACE
${CMAKE_CURRENT_SOURCE_DIR}/meshoptimizer/lib/meshoptimizer.lib


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