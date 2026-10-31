# Phase 19: WASM Traps & Distribution Pipeline - Context

**Gathered:** 2026-10-22
**Status:** Ready for planning

<domain>
## Phase Boundary

The extension runs in DuckDB-Wasm without the #289 traps, and the get.erpl.io release pipeline can no longer publish a WASM artifact that fails to LOAD (#272). Requirements: BUG-01, BUG-02, DIST-01, DIST-02, DIST-03, DIST-04. Issue bodies: GitHub #289, #272 (incl. the wasm_mvp `_setThrew` comment).

</domain>

<decisions>
## Implementation Decisions

### #289: fill-forward operator and WASM traps
- `ts_fill_forward_operator` keeps its public signature but becomes a SQL macro that delegates to `ts_fill_forward_by` (same args: source table name, group, date, value, end date, frequency). The C++ table function with the nested `QueryMaterialized` in InitGlobal is deleted. The macro binder takes care of identifier quoting, and `frequency` is honoured through `ts_fill_forward_by`. The macro is marked deprecated in the CHANGELOG and docs and removed in the following release (same one-release policy as #280). The `anofox_fcst_ts_fill_forward_operator` alias follows the same path.
- `ts_features_list()` / `ts_features_config_template()` traps: reproduce first in the Node DuckDB-Wasm harness (test/wasm/run.mjs), find the root cause (suspects: static table data or an FFI string buffer in no-argument table functions), then fix it.
- Done means 117 rows natively AND in DuckDB-Wasm (wasm_eh), the instance stays usable for the next statement, and both functions plus the fill-forward macro are added to the CI-gated curated WASM subset.

### #272: distribution pipeline (get.erpl.io)
- `scripts/extension-upload.sh`: for WASM, replace the last 256 bytes (the zeroed placeholder signature) of the existing trailing `duckdb_signature` custom section, exactly like the native path. Never append a new section header. Guard: abort unless the artifact ends with exactly one `duckdb_signature` section.
- The post-upload check runs in CI (`.github/workflows/_extension_deploy.yml`) right after the upload: download the uploaded artifact (decompressing brotli `content-encoding: br`), assert exactly one `duckdb_signature` section, and for `wasm_eh` LOAD it in the Node DuckDB-Wasm harness. Any failure fails the deploy job. Note: the harness's @duckdb/duckdb-wasm bundles engine v1.5.6, so the LOAD check applies to the v1.5.6 line; for v2.0.0, assert the section count only and record that.
- `wasm_mvp` `_setThrew is not defined`: time-boxed root-cause investigation. If it's ours (link flags, Rust panic=abort/unwind, emscripten exception settings), fix it. If it's in DuckDB-Wasm or emsdk, stop deploying wasm_mvp to get.erpl.io (exclude it in the deploy matrix), document why, and draft an upstream issue.
- The exact S3 CORS rule (GET/HEAD from `*`), the stale old-version paths (found by listing get.erpl.io over HTTP, read-only) and the redeploy steps go in `docs/distribution/get-erpl-io.md`, plus a comment on #272 linking to it. The owner applies the bucket changes; this phase makes NO bucket changes.

### Claude's Discretion
- Exact structure of the WASM section parser (bash + python helper vs a small Node script), as long as CI can run it.
- How the harness repro for the traps is structured (a separate `.test` file vs added to an existing one).

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `test/wasm/run.mjs` + `sqllogic.mjs`: Node DuckDB-Wasm harness with a curated, explicit test-file subset (8 files); `--file` and `ANOFOX_WASM_EXT` let it target a given artifact.
- `scripts/extension-upload.sh`: the native path already does `truncate -s -256` + appends the signature; the WASM path (lines ~33-45) appends a full new custom section.
- `.github/workflows/_extension_deploy.yml`: per-arch deploy matrix calling extension-upload.sh.
- `ts_fill_forward_by` → `_ts_fill_forward_native` (the operator is unused by it).

### Established Patterns
- Macros live in `src/macros/ts_macros.cpp`; table-in macros wrap `query_table(...)` in a subselect (memory: a bare TABLE arg silently fails to register).
- `make audit` forbids statistics in C++/SQL; fill-forward is data prep, not a statistic.

### Integration Points
- `src/table_functions/ts_fill_forward_operator.cpp` (to delete), its registration in `src/anofox_forecast_extension.cpp`, CMakeLists source list, `test/sql/ts_fill_forward_operator.test`, the data-prep skill doc.

</code_context>

<specifics>
## Specific Ideas

- #289 repro SQL (raw_sales, 6 rows, end 2024-01-10, '1d') becomes a sqllogictest, native + WASM.
- #272 evidence: affected files had 2 sections `[530, 274]`; a correct one has 1 `[530]`.

</specifics>

<deferred>
## Deferred Ideas

- DuckDB-Wasm runtime LOAD check for v2.0.0 artifacts (needs a duckdb-wasm release bundling engine 2.0).
- Applying the CORS rule and deleting stale paths (owner action).

</deferred>
