# Settings parser test. Compiles editor_settings.cpp directly instead of linking
# EditorLib so the test doesn't drag in RuntimeLib/imgui/glad; the module only
# needs nlohmann (parser) + Log (KP_LOG), and the engine include root for the
# "editor/..." header path.
add_executable(
    SettingsTest
    settings_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/settings/editor_settings.cpp
)

target_include_directories(SettingsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(SettingsTest PRIVATE nlohmann Log googletest)

include(GoogleTest)
gtest_discover_tests(SettingsTest)
