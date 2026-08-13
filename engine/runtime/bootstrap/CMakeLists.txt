# Engine-scoped bootstrap preload (docs/status.md item 6). Only RuntimeLib links
# this target — the startup need-list and its request building are the engine's
# business, deliberately not part of core/resource's public API. Asset is linked
# for the request type + extension sniffing; Log for KP_LOG; nlohmann for the
# bootstrap.json parser.
add_library(Bootstrap STATIC
    bootstrap.cpp
)

target_include_directories(Bootstrap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Bootstrap PRIVATE Config Asset Log nlohmann)
