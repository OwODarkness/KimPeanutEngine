# Header-only: the generic transport queue. The asset-flavored request lives in
# the Asset module (asset_load_request.h), NOT here -- core must not reach up to
# a top-level module. Flip to STATIC when the loading thread / drain land.
add_library(Async INTERFACE)

target_include_directories(Async INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
