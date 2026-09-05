find_package(Vulkan REQUIRED)

add_library(KPThirdPartyVulkan INTERFACE)
target_link_libraries(KPThirdPartyVulkan INTERFACE Vulkan::Vulkan)
add_library(KP::Vulkan ALIAS KPThirdPartyVulkan)

add_library(KPThirdPartyShaderc UNKNOWN IMPORTED GLOBAL)

set_target_properties(KPThirdPartyShaderc PROPERTIES
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/shaderc_shared.lib
    INTERFACE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(KP::Shaderc ALIAS KPThirdPartyShaderc)
