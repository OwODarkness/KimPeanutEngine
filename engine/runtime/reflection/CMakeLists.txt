add_library(Reflection STATIC
    reflection_system.cpp
    entt/entt_reflection_registry.cpp
)

target_include_directories(Reflection PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# The registration-phase headers include EnTT meta/factory types, so the
# header-only dependency must propagate to module-owned registration targets.
target_link_libraries(Reflection PUBLIC EnTT::EnTT)
