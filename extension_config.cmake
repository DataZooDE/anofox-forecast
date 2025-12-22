# This file is included by DuckDB's CMake build system to load this extension

duckdb_extension_load(anofox_forecast
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)
