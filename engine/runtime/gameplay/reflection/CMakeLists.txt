add_library(GameplayReflection STATIC
    actor_reflection.cpp
    camera_reflection.cpp
    gameplay_reflection.cpp
    light_reflection.cpp
)

target_include_directories(GameplayReflection PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(GameplayReflection
    PUBLIC Gameplay Reflection
    PRIVATE EnTT::EnTT
)
