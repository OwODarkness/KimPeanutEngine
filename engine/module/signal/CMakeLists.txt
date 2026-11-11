add_library(SignalLabMode STATIC
    tools/signal_scope_session.cpp
    tools/signal_lab_editor.cpp
    tools/signal_lab_host.cpp)
target_include_directories(SignalLabMode PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(SignalLabMode
    PUBLIC Signal RuntimeLib
    PRIVATE EditorUiHostLib UIWidgets LogEditor)

add_library(SignalLabRegistration OBJECT
    signal_registration.cpp)
target_include_directories(SignalLabRegistration PRIVATE
    "${CMAKE_SOURCE_DIR}/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SignalLabRegistration PRIVATE SignalLabMode ModuleBootstrap)
