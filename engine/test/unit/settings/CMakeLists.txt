add_executable(SettingsUnitTest
    settings_registry_test.cpp
)

target_link_libraries(SettingsUnitTest PRIVATE
    RuntimeSettings
    KP::GoogleTest
    KP::NlohmannJson
)

include(GoogleTest)
gtest_discover_tests(SettingsUnitTest PROPERTIES LABELS "settings")
