# SQLite is embedded from the upstream amalgamation. Keep the C implementation
# in its own target so engine modules depend on a stable KP:: target rather than
# on the vendored directory layout.
add_library(KPThirdPartySQLite STATIC
    3.53.4/src/sqlite3.c
)

set_source_files_properties(
    3.53.4/src/sqlite3.c
    PROPERTIES LANGUAGE C
)

target_include_directories(KPThirdPartySQLite SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/3.53.4/include
)

add_library(KP::SQLite ALIAS KPThirdPartySQLite)
