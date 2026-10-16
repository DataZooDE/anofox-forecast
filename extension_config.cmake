# This file is included by DuckDB's CMake build system to load this extension

# json is needed by 50 test/sql/*.test files that use `require json` to assert
# JSON-typed intermediate results. This is DuckDB's own in-tree extension
# (duckdb/extension/json, part of the pinned duckdb submodule) — no download,
# no GIT_URL. A plain in-tree default-path load (no DONT_LINK), deliberately
# independent of the anofox_forecast block below: no LINKED_LIBS, no LOAD_TESTS
# on it, and it does not touch anofox_forecast's own OpenSSL static linkage or
# the WASM LINKED_LIBS workaround documented below.
#
# WR-05 (Phase 18 review): what this actually statically links, and where.
# duckdb_extension_load() without DONT_LINK bundles json's registration code
# (JSONFunctions/JsonExtension) into *this build's own DuckDB core binaries* --
# the `duckdb` shell and `test/unittest` executables that `make release`/
# `make debug` produce -- the same way DONT_LINK-less core_functions/icu/
# parquet always are. DONT_LINK is not an option here: `require json` runs
# against `test/unittest`, and the sqllogictest harness needs json's functions
# available there without a separate `LOAD json;` step in every test file.
# Confirmed live (objdump/nm against a built tree, Phase 18 review): `nm -C
# build/release/duckdb` and `nm -C build/release/test/unittest` both contain
# ~94 JSONFunctions/JsonExtension symbols; `nm -C build/release/extension/
# anofox_forecast/anofox_forecast.duckdb_extension` contains zero -- the json
# extension's own code is NOT statically linked into the distributed
# anofox_forecast.duckdb_extension artifact itself, and `objdump -p` on that
# artifact shows no new NEEDED entries from this line. (The anofox_forecast
# artifact does contain ~71 `duckdb_yyjson::*` symbols, but that is DuckDB's
# own internally-vendored yyjson copy used by core -- core_functions.duckdb_
# extension and parquet.duckdb_extension, which never reference this line at
# all, carry the identical 71 symbols.) In short: this line affects the dev/
# test binaries built alongside the extension, not the extension artifact
# that ships to users.
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

# DuckDB 2.0 links an extension into its own shell and unittest only when an
# extension config asks for it; otherwise the sqllogictest runner skips every
# `require anofox_forecast` test (the duckdb-next CI leg ran 0 of 94). DuckDB
# 1.x links every loaded extension by default and has no such command, so this
# is a no-op there. The distributed loadable extension is unaffected.
if(COMMAND duckdb_extension_statically_link)
    duckdb_extension_statically_link(anofox_forecast)
endif()
