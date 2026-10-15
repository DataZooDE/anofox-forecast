# This file is included by DuckDB's CMake build system to load this extension

# json is required at test-time only (50 test/sql/*.test files use `require json`
# to assert JSON-typed intermediate results). This is DuckDB's own in-tree
# extension (duckdb/extension/json, part of the pinned duckdb submodule) — no
# download, no GIT_URL. A plain in-tree default-path load, deliberately
# independent of the anofox_forecast block below: no LINKED_LIBS, no LOAD_TESTS
# on it, and it does not touch anofox_forecast's own OpenSSL static linkage or
# the WASM LINKED_LIBS workaround documented below.
duckdb_extension_load(json)

duckdb_extension_load(anofox_forecast
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
    # On WASM builds the extension target is a STATIC library and the final
    # link happens in a post-build emcc step (extension_build_tools.cmake:196)
    # that reads its extra archives from DUCKDB_EXTENSION_ANOFOX_FORECAST_LINKED_LIBS.
    # Without this, the Rust FFI static archive (target: anofox_fcst_ffi, defined
    # in CMakeLists.txt via corrosion_import_crate) is never linked into the .wasm,
    # so all 84 anofox_ts_* / anofox_free_* FFI symbols end up as unresolvable
    # imports and LOAD fails with "TypeError: r is not a function" (issue #239).
    # Corrosion creates two targets: `anofox_fcst_ffi` (INTERFACE — for
    # `target_link_libraries`) and `anofox_fcst_ffi-static` (the actual STATIC
    # IMPORTED archive). We need the -static suffix here so TARGET_FILE
    # resolves to the .a file path.
    LINKED_LIBS "$<TARGET_FILE:anofox_fcst_ffi-static>"
)
