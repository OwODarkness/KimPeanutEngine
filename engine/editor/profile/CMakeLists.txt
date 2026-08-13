# Profile module — bottom status bar + its metric layer. Metrics are engine/OS-agnostic
# (sampler injection), so they compile standalone and are unit-testable without
# EditorLib/imgui (see engine/test/unit/profile). Explicit sources, no GLOB.
target_sources(EditorLib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_metric.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_builtin_metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_profile_bar.cpp
)
