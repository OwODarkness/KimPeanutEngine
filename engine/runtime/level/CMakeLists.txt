add_library(RuntimeLevel STATIC
    level_instance.cpp
)

target_include_directories(RuntimeLevel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Runtime level policy consumes immutable Asset data and the Gameplay Actor
# lifecycle. It has no Render, Graphics, or Editor dependency.
target_link_libraries(RuntimeLevel PUBLIC Asset Gameplay)
