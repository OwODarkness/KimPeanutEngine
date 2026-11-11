function(add_editor_ui_smoke target)
    add_executable(${target} editor_ui_smoke.cpp)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/engine)
    target_link_libraries(${target} PRIVATE
        EditorUiHostLib Window Graphics
        ${ARGN}
    )
    copy_runtime_dependencies(${target})
endfunction()

add_editor_ui_smoke(EditorUiSmoke)
add_editor_ui_smoke(EditorUiSmokeOpenGL)
target_compile_definitions(EditorUiSmokeOpenGL PRIVATE
    KPENGINE_EDITOR_UI_SMOKE_DEFAULT_OPENGL=1
)

# Proves the same real Settings feature subview used by the recording test on
# a native production backend, without adding RuntimeLib or EditorUILib.
add_editor_ui_smoke(EditorSettingsFieldsSmoke SettingsPropertyFields)
target_compile_definitions(EditorSettingsFieldsSmoke PRIVATE
    KPENGINE_EDITOR_UI_SMOKE_SETTINGS_FIELDS=1
)

add_editor_ui_smoke(EditorSettingsFieldsSmokeOpenGL SettingsPropertyFields)
target_compile_definitions(EditorSettingsFieldsSmokeOpenGL PRIVATE
    KPENGINE_EDITOR_UI_SMOKE_SETTINGS_FIELDS=1
    KPENGINE_EDITOR_UI_SMOKE_DEFAULT_OPENGL=1
)
