add_library(RuntimeLevel STATIC
    level_instance.cpp
)

target_include_directories(RuntimeLevel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Runtime level policy consumes immutable Asset data, Gameplay Actor
# composition, and the value-only Render environment source contract. It has
# no Graphics, backend, or Editor dependency.
target_link_libraries(RuntimeLevel PUBLIC Asset Gameplay Render)
