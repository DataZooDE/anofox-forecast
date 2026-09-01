# Phase 7: WASM Node Harness + Local Green - Context

**Gathered:** 2026-09-01
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase — smart discuss skipped)

<domain>
## Phase Boundary

Stand up a Node-based DuckDB-Wasm test harness under `test/wasm/` that boots DuckDB-Wasm, `FORCE INSTALL` + `LOAD`s the freshly-built `anofox_forecast.duckdb_extension.wasm`, and runs the full 66-file `test/sql/**/*.test` suite green locally. Includes the ABI prerequisites: pin `@duckdb/duckdb-wasm` + `web-worker@1.2.0` to versions whose engine ABI matches the built DuckDB, and declare `openssl` as a `!wasm32` dependency in `vcpkg.json` so Emscripten stops compiling OpenSSL for the WASM target.

This is a CI/infra hardening phase — no new SQL functions, no Rust crate changes. Work lives in `test/wasm/`, `vcpkg.json`, and docs. It ports the reference implementation from anofox-statistics PR #131 (`test/wasm/run.mjs`, `test/wasm/sqllogic.mjs`).

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — pure infrastructure phase. Use the ROADMAP phase goal, the five success criteria, the known gotchas below, and the anofox-statistics PR #131 reference implementation to guide decisions.

</decisions>

<code_context>
## Existing Code Insights

### Reference Implementation to Port
- anofox-statistics PR #131: `test/wasm/run.mjs`, `test/wasm/sqllogic.mjs`, `WasmTest.yml`, the `wasm-runtime-test` job. Phase 7 is the harness + local-green half; Phase 8 wires it into CI.

### Established Patterns
- Extension build target: `wasm32-unknown-emscripten`; extension loads via `LINKED_LIBS` in `extension_config.cmake` (Rust static archives are silently dropped on WASM otherwise — issue #240, upstream duckdb/duckdb#23740).
- OpenSSL is statically linked on native targets (issues #211, #215) and must stay that way; the `!wasm32` guard only removes it from the WASM build.

### Integration Points
- `test/sql/**/*.test` — the existing 66-file native sqllogictest suite the harness must replay.
- `vcpkg.json` — dependency manifest; add the `!wasm32` platform guard on `openssl`.

</code_context>

<specifics>
## Specific Ideas

Known gotchas (issue #255 — must shape the plan):
- `@duckdb/duckdb-wasm` npm version ≠ engine version — must ABI-match the built DuckDB version or `LOAD` fails.
- DECIMAL renders unscaled in duckdb-wasm Arrow-JS → format results through `::VARCHAR` to match native sqllogictest output.
- Per-file catalog isolation — re-open the DB + re-`LOAD` the extension per `.test` file.
- Node pins `web-worker@1.2.0`, `pthreadWorker=null` for the `eh` bundle, and uses `FORCE INSTALL`.
- Serve the built `.wasm` over localhost via a version-agnostic `<version>/<platform>/` path.
- 66 `.test` files; WASM-03 permits an explicit, documented skip-list for tests genuinely infeasible on WASM (one reason per entry).

</specifics>

<deferred>
## Deferred Ideas

- Browser-based (not just Node) WASM E2E harness (WASM-F1) — deferred at milestone open.
- Shared-memory `wasm_threads` build — blocked upstream (WASM-F2).

</deferred>
