# Requirements: anofox-forecast — v0.9.0 WASM Runtime Verification

**Defined:** 2026-09-01
**Core Value:** Prove the built `anofox_forecast` `.wasm` actually loads and runs in DuckDB-Wasm — not just that it compiles and links — and gate it in CI so WASM regressions fail the build.

Tracks GH issue #255. Reference implementation to port: anofox-statistics PR #131.

## v1 Requirements

Requirements for this milestone. Each maps to a roadmap phase.

### Harness

- [x] **WASM-01**: A Node harness in `test/wasm/` boots DuckDB-Wasm (`eh` bundle, `pthreadWorker=null`, `web-worker` pinned), serves the locally-built `anofox_forecast.duckdb_extension.wasm` over localhost (version-agnostic `<version>/<platform>/` path), and `FORCE INSTALL` + `LOAD`s it successfully.
- [x] **WASM-02**: A minimal sqllogictest-subset runner executes `test/sql` `.test` files against the loaded WASM extension, re-opening the DB and re-`LOAD`ing per file for catalog isolation, and formats results through `::VARCHAR` so DECIMAL scale matches native sqllogictest output.
- [x] **WASM-03**: The full `test/sql/**/*.test` suite (66 files) passes against the built `.wasm`; any test genuinely infeasible on WASM is explicitly skip-listed with a documented reason.

### CI Gating

- [x] **CI-01**: A gating CI job (`needs:` the wasm build) downloads the `wasm_eh` artifact and runs the harness, failing the build on any WASM load or runtime error.
- [x] **CI-02**: A dedicated WASM workflow (separate from the main distribution pipeline) runs the harness so WASM status is independently observable.
- [x] **CI-03**: A WASM status badge in the README reflects the dedicated WASM workflow's pass/fail state.

### Dependencies & Pinning

- [x] **DEP-01**: `@duckdb/duckdb-wasm` and `web-worker@1.2.0` are pinned to versions whose engine ABI matches the built DuckDB version, with the version-verification procedure documented.
- [x] **DEP-02**: `openssl` is declared a `!wasm32` dependency in `vcpkg.json` so Emscripten no longer compiles unused OpenSSL for the WASM target.

## Future Requirements

Deferred to a future release. Tracked but not in this roadmap.

### WASM

- **WASM-F1**: Browser-based (not just Node) WASM end-to-end test harness.
- **WASM-F2**: Shared-memory `wasm_threads` build — blocked upstream (needs Rust nightly + `-Zbuild-std`; duckdb/extension-ci-tools#391 fixes only half).

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| New SQL functions / models | This is a CI/infra hardening milestone — no new user-facing surface |
| `anofox-forecast` crate changes | WASM verification is an extension-repo/build concern; crate bump not required |
| Full browser E2E (Playwright/headless) | Node harness proves load+run; browser matrix is a separate future effort (WASM-F1) |
| `wasm_threads` shared-memory build | Blocked upstream on Rust std + `-Zbuild-std`; not resolvable this milestone (WASM-F2) |

## Traceability

Which phases cover which requirements. Filled during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| WASM-01 | Phase 7 | Complete |
| WASM-02 | Phase 7 | Complete |
| WASM-03 | Phase 7 | Complete |
| DEP-01 | Phase 7 | Complete |
| DEP-02 | Phase 7 | Complete |
| CI-01 | Phase 8 | Complete |
| CI-02 | Phase 8 | Complete |
| CI-03 | Phase 8 | Complete |

**Coverage:**

- v1 requirements: 8 total
- Mapped to phases: 8 ✓
- Unmapped: 0

---
*Requirements defined: 2026-09-01*
*Last updated: 2026-09-01 after roadmap creation (traceability filled, Phases 7-8)*
