add_library(Reflection STATIC
    reflection_system.cpp
    entt/entt_reflection_registry.cpp
)

target_include_directories(Reflection PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Reflection PRIVATE EnTT::EnTT)
