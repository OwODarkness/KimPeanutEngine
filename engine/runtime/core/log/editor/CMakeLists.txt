add_library(LogEditor STATIC
    log_editor_view.cpp
)

target_include_directories(LogEditor PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(LogEditor
    PUBLIC Log
    PRIVATE KP::ImGui
)
