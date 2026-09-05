add_library(KPThirdPartyNlohmannJson INTERFACE)

target_include_directories(KPThirdPartyNlohmannJson SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(KP::NlohmannJson ALIAS KPThirdPartyNlohmannJson)
