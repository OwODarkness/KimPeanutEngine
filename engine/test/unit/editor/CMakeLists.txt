add_executable(EditorUILifecycleTest
    editor_ui_lifecycle_test.cpp
)

target_include_directories(EditorUILifecycleTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
)

target_link_libraries(EditorUILifecycleTest PRIVATE
    EditorUILib
    KP::ImGui
    KP::GoogleTest
)

add_custom_command(TARGET EditorUILifecycleTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:EditorUILifecycleTest>
)

include(GoogleTest)
gtest_discover_tests(EditorUILifecycleTest)

add_executable(ActorEditorModelUnitTest
    actor_editor_model_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/actor/actor_editor_model.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/actor/actor_property_widget.cpp
)

target_include_directories(ActorEditorModelUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
)

target_link_libraries(ActorEditorModelUnitTest PRIVATE
    KP::GoogleTest
)

include(GoogleTest)
gtest_discover_tests(ActorEditorModelUnitTest)

# The tool-row model is ImGui-free by design, so this target links no ImGui and
# compiles the model source directly. If it ever starts pulling imgui.h, the
# layering has been broken and this build will say so.
#
# The layout model is compiled in as well because the placement drop rule asks it which
# region is under the cursor. Also ImGui-free, so the layering check still holds.
add_executable(EditorToolRowModelUnitTest
    editor_tool_row_model_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/ui/component/editor_tool_row_model.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/ui/component/editor_layout_model.cpp
)

target_include_directories(EditorToolRowModelUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(EditorToolRowModelUnitTest PRIVATE
    KP::GoogleTest
)

include(GoogleTest)
gtest_discover_tests(EditorToolRowModelUnitTest)

# The layout model is ImGui-free by design, so this target links no ImGui and compiles
# the model source directly. If it ever starts pulling imgui.h, the layering has been
# broken and this build will say so.
#
# The tool-row model is compiled in as well because it is what the persisted placements
# are applied to. Also ImGui-free, so the layering check still holds.
add_executable(EditorLayoutModelUnitTest
    editor_layout_model_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/ui/component/editor_layout_model.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/ui/component/editor_tool_row_model.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/settings/editor_layout_settings.cpp
)

target_include_directories(EditorLayoutModelUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

# NlohmannJson is header-only and is needed only by the persistence round-trip; it pulls
# in no ImGui. The settings parser deliberately does not log, so no Log lib is needed and
# the caller owns the diagnostic it returns.
target_link_libraries(EditorLayoutModelUnitTest PRIVATE
    KP::GoogleTest
    KP::NlohmannJson
)

include(GoogleTest)
gtest_discover_tests(EditorLayoutModelUnitTest)

# The asset browser model is ImGui-free AND Asset-library-free by design: it compiles its
# own source and links only KP::GoogleTest, so a dependency on either — or a call into
# Asset's validator — is a build error rather than a review finding. It includes the frozen
# catalog contract for its value types, which is why engine/runtime is on the include path.
add_executable(AssetBrowserModelUnitTest
    asset_browser_model_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/asset/asset_browser_model.cpp
)

target_include_directories(AssetBrowserModelUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
)

target_link_libraries(AssetBrowserModelUnitTest PRIVATE
    KP::GoogleTest
)

include(GoogleTest)
gtest_discover_tests(AssetBrowserModelUnitTest)
