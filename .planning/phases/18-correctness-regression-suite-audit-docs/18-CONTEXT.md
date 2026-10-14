# Phase 18: Correctness Regression Suite, Audit & Docs - Context

**Gathered:** 2026-10-14
**Status:** Ready for planning

<domain>
## Phase Boundary

The corrected behaviour of Phases 9–17 is pinned against regression, guarded against statistics creeping back into C++/SQL, and accurately described in docs, skills and release notes. Requirements: TEST-02, TEST-03, RUST-10, DOCS-01, DOCS-02.

Execution note: planned in parallel with Phases 16/17. It EXECUTES last, after Phase 17 completes, because it pins the final numbers.

</domain>

<decisions>
## Implementation Decisions

### Tests (TEST-02, TEST-03)
- **D-01:** Make the json extension available to the SQL test runner (statically loaded into the test/release build, e.g. via `extension_config.cmake` / `duckdb_extension_load(json)`), so the 54 of 83 `test/sql/*.test` files gated by `require json` actually run locally and in CI. Then repair the stale ones: renamed or removed functions, the DATE+BIGINT issue, and the numeric shifts from Phases 13–17, each re-pinned with a stated reason. A prior memory note says about 23 such files failed on stale API references when run under WASM. Must not change OpenSSL linkage or the WASM `LINKED_LIBS` setup.
- **D-02:** Correctness suite: `test/sql/correctness/<area>.test`, one file per area (stats, metrics, data quality, features, detection & decomposition, conformal & bootstrap, diagnostics, forecast parity). Each pins every user-facing function fixed this milestone with values taken from the checked-in reference fixtures. Not json-gated. The existing `*_reference.test` files stay.
- **D-03:** Length-only sweep: a grep-based script finds Rust tests in modules touched this milestone that only assert length/shape, and each is upgraded to fixture or closed-form values. The sweep script is kept as a check, and the success criterion is that it finds none in those modules.

### The "no statistics in C++/SQL" audit (RUST-10)
- **D-04:** `scripts/audit_no_stats_in_cpp.py` scans `src/**/*.cpp` for statistical arithmetic (`sqrt`/`pow`/`log`/`exp` on data, `std::accumulate` / mean or variance loops, sorting for quantiles) and the macro SQL for statistical aggregates (`avg`, `stddev`, `quantile*`, `regr_*`, `corr`, …). It fails on any hit not in `scripts/audit_allowlist.txt`; each allow-list entry gives a file, a pattern and a justification (e.g. date arithmetic, list marshalling). It passes on HEAD.
- **D-05:** It runs in a new lightweight `.github/workflows/audit.yml` (on PRs and pushes) and as a `make audit` target. `MainDistributionPipeline.yml` is left untouched (it is the shared extension-ci-tools pipeline).

### Docs, release notes, leftovers (DOCS-01, DOCS-02)
- **D-06:** Promote the per-phase snippet harnesses (`.planning/phases/1[3-7]-*/snippets/`) into one permanent `scripts/check_doc_snippets.py` (plus a `make` target). It runs every SQL snippet in `docs/` and in `plugins/anofox-forecast/skills/` (bodies and frontmatter descriptions) against the built extension. The milestone docs sweep is verified with it.
- **D-07:** The CHANGELOG gets a consolidated "Breaking changes in v0.10.0" section: an old → new table by area (stats, metrics, data quality, features, detection, conformal, diagnostics, forecasts) plus SQL signature changes (conformal `date_col` and row-per-(group, date) output, the calibration-table input, MASE/RMAE, removed jackknife+/wavelet, the 'Holt' remap, stationarity labels, `n_gaps` semantics). Plus release-notes text for the GitHub release.
- **D-08:** Fix the deferred bugs in this phase, each with a correctness-suite test:
  - WR-05: `ts_data_quality_summary` miscounts groups whose dates are all NULL (Phase 13 deferred-items);
  - `ts_coverage_by` / `ts_quantile_loss_by` pass `source` into a TABLE parameter without `query_table()` (Phase 16 deferred-items).
- **D-09:** 4 plans:
  1. json for the test runner + repairing the stale gated tests [TEST-03 prerequisite];
  2. correctness suite + length-only sweep + the D-08 fixes [TEST-02, TEST-03];
  3. audit script + allow-list + CI [RUST-10];
  4. snippet checker + docs/skills sweep + CHANGELOG breaking-changes section [DOCS-01, DOCS-02].

### Claude's Discretion
- Exact audit patterns and allow-list format, the correctness file split if an area is too large, and how the length-only sweep detects "length-only".

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- Per-phase snippet harnesses: `.planning/phases/13-*/snippets/run_snippets.py` and the forks in phases 14–17.
- Reference fixtures: `validation/reference/fixtures/*` (moments, r_entropy, gap_counts, r_stl_strength, quality_scores, r_mase, tsfresh_features, numpy_detrend, peaks, changepoints, r_decomposition, period_detection, conformal, bootstrap, r_forecast, r_diagnostics, …) and the non-gated `test/sql/*_reference.test` files.
- Deferred-items files in phases 13–16.

### Established Patterns
- `extension_config.cmake` / `CMakeLists.txt` drive the build. Memory notes: OpenSSL must stay statically linked; WASM needs `LINKED_LIBS`; GHA Windows can hide dynamic deps.
- CI: `.github/workflows/MainDistributionPipeline.yml` (extension-ci-tools), `WasmTest.yml`, `_extension_*`.

### Integration Points
- 54/83 SQL test files contain `require json`. Under the WASM full suite, about 23 failed on stale API references, DATE+BIGINT and cascades (memory note).

</code_context>

<specifics>
## Specific Ideas

- The ROADMAP success criteria are the acceptance bar. The final DoD includes the full suite with json available (assertion count should jump substantially), `make audit` passing, the snippet checker reporting 0 FAIL, and a clean `cargo fmt` / `clippy` / `test`.

</specifics>

<deferred>
## Deferred Ideas

- None — the deferred bugs from Phases 13 and 16 are folded in (D-08).

</deferred>
