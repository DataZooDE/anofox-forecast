# Codebase Concerns

**Analysis Date:** 2026-08-20

## Tech Debt

**Stale API References:**
- Issue: `ts_backtest_auto_by()` function was removed in favor of two-step workflow (`ts_cv_folds_by` + `ts_cv_forecast_by`), but stale references remain.
- Files: `duckdb/test/sql/ts_varchar_edge_cases.test` (lines ~87, 96), `test/sql/backtest_memory_investigation.sql`, `src/table_functions/ts_backtest_native.cpp`, `src/scalar_functions/metrics.cpp`
- Impact: Documentation and test files reference removed API, confusing new users and complicating cleanup.
- Fix approach: Remove stale references to `ts_backtest_auto_by` from tests and comments; consolidate documentation to reference the two-step pattern exclusively. Also audit `ts_detect_periods_by` and similar functions to ensure dependency on optional `json` extension is documented (recommend enabling `autoinstall_known_extensions` in setup docs).

**Unimplemented Aggregate Version:**
- Issue: `ts_data_quality()` has TODO comment indicating aggregate version not yet implemented.
- Files: `src/table_functions/ts_data_quality.cpp:123`
- Impact: Users can only compute data quality on scalar inputs, not across grouped aggregations.
- Fix approach: Implement aggregate function matching the pattern used by `ts_stats_agg`, `ts_features_agg`, etc.

**Partial WASM Support (Fixed but with Caveats):**
- Issue: WASM builds required special handling via `LINKED_LIBS` parameter to pass Rust static archives to the emcc post-build step (memory location: `~/.claude/projects/.../memory/project_extension_wasm_linked_libs.md`).
- Files: `CMakeLists.txt` (duckdb extension configuration)
- Impact: WASM builds previously failed silently with "not a function" errors if `LINKED_LIBS` was not passed. Now fixed (PR #240, closes #239).
- Fix approach: Already mitigated; ensure no regression when updating duckdb submodule or extension-ci-tools. See [[extension-wasm-linked-libs-trap]] in project memory.

## Known Bugs

**Error Handling Gaps in FFI Allocation:**
- Symptom: FFI functions allocate C arrays but do not consistently check for allocation failure. When memory is tight, allocations can return null, but the function may still return `true` (success), leading to C++ trying to dereference null pointers.
- Files: `crates/anofox-fcst-ffi/src/lib.rs` (multiple locations, e.g., lines 3198-3216 in earlier versions)
- Trigger: Run `ts_forecast_by()` or similar on large datasets with memory constraints (256-512 MB limit).
- Workaround: Increase system or process memory limit; monitor for segfaults on large backtest/forecast operations.

**Test Assertions Using `unwrap()` in Production Code:**
- Symptom: Core modules use `.unwrap()` extensively in test code and error handling (`crates/anofox-fcst-core/src/error.rs:137, 155`).
- Files: `crates/anofox-fcst-core/src/error.rs` (error handling test module), `crates/anofox-fcst-core/src/stats.rs`, `crates/anofox-fcst-core/src/decomposition.rs`, `crates/anofox-fcst-core/src/changepoint.rs`, `crates/anofox-fcst-core/src/gaps.rs`
- Impact: Tests panic if assertions fail during refactoring. ~194 unwrap() calls in core, concentrated in test sections, but some in hot paths like `detect_changepoints`, `detect_seasonality`, and `fill_gaps`.
- Priority: Low to medium — existing tests pass, but refactoring changepoint/seasonality algorithms requires care.

**BUG Markers in Changepoint Detection:**
- Symptom: Two historical bug fixes left as comments in changepoint.rs.
- Files: `crates/anofox-fcst-core/src/changepoint.rs:213, 304, 455` (BUG FIX and BUG comments)
- Impact: Comments document prior issues with prior distributions and run-length statistics shifting; no current functional issue but indicates complex, stateful logic.
- Safe modification: When modifying changepoint detection, review the BUG comments and verify they are still necessary or if the underlying assumptions have changed.

## Security Considerations

**OpenSSL Static Linkage (Mitigation in Place):**
- Risk: Dynamic linking to OpenSSL on Windows/Linux exposes users to "missing DLL/SO" errors when system libraries are upgraded or absent (issues #211, #215).
- Files: `CMakeLists.txt` (sets `OPENSSL_USE_STATIC_LIBS ON`), `vcpkg.json` (declares OpenSSL dependency with static triplet)
- Current mitigation: Static linking enforced on all platforms; CI verified to produce binaries with zero `libssl/libcrypto` imports (use `objdump -p <binary> | grep "DLL Name"` to verify).
- Recommendations: Maintain static linkage; do not regress to dynamic OpenSSL. Before major updates, run objdump check on Windows artifacts (GitHub Actions `windows-latest` masks dynamic deps with preinstalled Perl/Chocolatey libraries).

**CI Green Does Not Guarantee Binary Portability (Windows):**
- Risk: GitHub Actions `windows-latest` runners ship Strawberry Perl, which includes OpenSSL DLLs. A dynamically-linked extension passes CI but fails on clean user machines (issue #215).
- Files: `.github/workflows/` (CI configuration), implied in build artifacts
- Current mitigation: Static linkage (see above).
- Recommendations: When changing Windows build config, verify artifact imports on a Linux machine using `objdump`, not just CI green lights.

**Telemetry Opt-Out:**
- Risk: PostHog telemetry is enabled by default (opt-out via `DATAZOO_DISABLE_TELEMETRY=1`). Sensitive users may not be aware.
- Files: `src/anofox_forecast_extension.cpp`, `posthog-telemetry/` directory
- Current mitigation: Environment variable opt-out documented in README and TELEMETRY.md.
- Recommendations: Consider making telemetry opt-in for enterprise deployments, or add startup warnings.

## Performance Bottlenecks

**Large-Scale Backtest and Forecast Memory Overhead:**
- Problem: `ts_backtest_auto_by`, `ts_cv_forecast_by`, `ts_forecast_by` use DuckDB `LIST()` aggregation which cannot spill to disk, causing OOM on large multi-series datasets (M5: 30k series × 2k points).
- Files: `docs/dev/memory-patterns.md` (detailed audit), `src/table_functions/ts_backtest_native.cpp`, `src/table_functions/ts_cv_forecast_native.cpp` (table_in_out implementations)
- Cause: Earlier versions materialized entire groups as lists; now fixed via native table_in_out streaming (PR #114, #113).
- Improvement path: Existing fixes (native streaming) resolve this for v0.4+. Monitor for regressions if new `_by` functions are added without streaming pattern.

**ARIMA Optimization Pending:**
- Problem: AutoARIMA solver uses generic MLE, but crate 0.5.4+ offers steady-state MLE optimization (12.9x speedup per upstream).
- Files: `Cargo.toml` (anofox-forecast = 0.15.3)
- Cause: Feature not yet integrated into DuckDB extension API.
- Improvement path: Upgrade anofox-forecast crate to 0.5.4+ once available; expose `arima_fast_mle` parameter in `ts_forecast_by` MAP parameters. Benchmark on M4 Daily (4k+ series).

**Unwrap() in Core Loops:**
- Problem: Seasonality detection, gap filling, and changepoint analysis use `.unwrap()` on fallible operations, causing panics in edge cases (e.g., all-null input, insufficient data).
- Files: `crates/anofox-fcst-core/src/gaps.rs:296, 419, 433, 451, 483, 506, 523`, `crates/anofox-fcst-core/src/stats.rs` (803+), `crates/anofox-fcst-core/src/seasonality.rs`, `crates/anofox-fcst-core/src/periods.rs`
- Cause: ~194 unwrap() calls; most in tests, but some in public paths.
- Improvement path: Audit public APIs (gaps.rs, stats.rs, periods.rs) and convert `.unwrap()` to proper error propagation or sensible defaults (e.g., return zero gaps, return NaN statistics for empty series).

## Fragile Areas

**Changepoint Detection Logic:**
- Files: `crates/anofox-fcst-core/src/changepoint.rs` (551 lines)
- Why fragile: PELT algorithm with BIC penalty, uninformative priors, and run-length tracking is mathematically complex. Historical BUG fixes indicate prior oversights (lines 213, 304, 455). Two `panic!()` calls on error conditions (lines 455 on constant probabilities).
- Safe modification: Write comprehensive unit tests before refactoring; verify against known changepoint datasets (e.g., BOCD reference implementations). Do not assume priors or penalty terms are optimal for all domains.
- Test coverage: ~50 test cases in module; missing edge cases (all-null input, single-value input, extreme imbalance).

**Conformal Prediction Implementation:**
- Files: `crates/anofox-fcst-core/src/conformal.rs` (1938 lines)
- Why fragile: Dual API (legacy single-step + new learn/apply two-step) with overlapping implementations. Quantile computation and coverage guarantees depend on residual exchangeability assumption.
- Safe modification: When modifying quantile logic or adding new ConformalMethod variants, cross-reference with Vovk et al. papers and verify coverage on known benchmarks. Test on both regression (continuous errors) and intermittent (sparse errors) series.
- Test coverage: Coverage tests exist (`ts_conformal_coverage.test`, 212 assertions) but not all method/strategy combinations are tested end-to-end.

**FFI Boundary and Memory Management:**
- Files: `crates/anofox-fcst-ffi/src/lib.rs` (6467 lines, largest file), `crates/anofox-fcst-ffi/src/allocation.rs`, `crates/anofox-fcst-ffi/src/types.rs` (1672 lines)
- Why fragile: Manual pointer handling, unsafe functions, NULL checks scattered throughout. Dual memory backends (libc on native, std::alloc on WASM) with subtle differences (WASM free uses minimal Layout, can cause double-free if misused).
- Safe modification: Any FFI refactoring requires careful review of pointer lifecycle. Run full test suite on both native and WASM targets. Do not refactor allocation without running on actual WASM environment (CI catches some issues, but not all).
- Test coverage: 973-line FFI parity test suite (`crates/anofox-fcst-ffi/tests/core_ffi_parity.rs`); comprehensive for normal paths, but error path coverage limited.

**Periods and Seasonality Detection:**
- Files: `crates/anofox-fcst-core/src/periods.rs` (2103 lines), `crates/anofox-fcst-core/src/seasonality.rs` (943 lines)
- Why fragile: Spectral analysis and ACF-based heuristics are heuristic-heavy; performance on edge cases (bimodal seasonality, irregular sampling, short series <50 points) is not well-characterized.
- Safe modification: Verify changes against tsfresh/statsforecast benchmarks. Test on synthetic data with known periods (7, 12, 52-week patterns) and real retail/economic datasets (M4, M5, retail-sales).
- Test coverage: ~30 test cases in periods.rs; weak coverage for multi-period detection and short series (<50 points).

## Scaling Limits

**DuckDB LIST() Aggregation (Memory):**
- Current capacity: Safe up to ~5k series × 500 points per fold (2.5M rows per LIST aggregate).
- Limit: Hits OOM at ~30k series × 2k points with <1GB memory (issue #115).
- Scaling path: Use native table_in_out streaming (already implemented in v0.4+); new `_by` functions must follow this pattern. Batch processing parameters can further reduce per-group memory.

**Rust/FFI Call Overhead:**
- Current capacity: ~2.5 ms FFI roundtrip per group (measure: M4 Daily AutoARIMA, 4k+ series, 40 min wall).
- Limit: Not a bottleneck for typical workloads (CPU-dominated, FFI overhead ~2-3% of total time).
- Scaling path: No immediate optimization needed; if single-series throughput becomes a bottleneck, profile actual FFI hot paths and consider batching small groups.

**WASM Shared Memory (Threading):**
- Current capacity: WASM `wasm_eh` variant supported (modern Chrome/Edge/Safari 16.4+, no threading).
- Limit: WASM `wasm_threads` variant with shared memory cannot be produced via standard CI pipeline due to two nested blockers: missing `-DUSE_WASM_THREADS=1` flag (PR filed, duckdb/extension-ci-tools#391) and Rust libstd without atomics (requires nightly + `-Zbuild-std`, not accessible from downstream CI).
- Scaling path: Pragmatic ship-today move is to add `wasm_threads` to extension's `exclude_archs` so broken artifact isn't published. Long-term: wait for upstream CI-tools PR #391 + workflow input support for nightly Rust.

## Dependencies at Risk

**anofox-forecast Crate (0.15.3):**
- Risk: Crate is external; if upstream introduces breaking changes or stops maintenance, extension is blocked.
- Impact: Core forecasting algorithms depend on this crate. No vendored fallback.
- Migration plan: Maintain fork at DataZooDE/anofox-forecast if upstream becomes unmaintained. Current status (v0.15.3) is stable and feature-complete.

**argmin Patch (Stable Rust Compatibility):**
- Risk: Workspace patches `argmin` to use modulo operations instead of unstable `is_multiple_of` (Rust 1.87+). If upstream adopts stable, patch becomes unnecessary but not harmful.
- Impact: Patch enables Rust 1.86 compatibility; without it, build fails on older toolchains.
- Migration plan: Monitor upstream argmin releases; remove patch once `is_multiple_of` stabilizes or is no longer used.

**extension-ci-tools Submodule (v1.4-andium and v1.5-variegata):**
- Risk: Custom branches maintained externally; if upstream `duckdb/extension-ci-tools` diverges, rebasing becomes complex.
- Impact: Affects build configuration for both LTS (v1.4.5) and latest (v1.5.4) DuckDB versions.
- Migration plan: Monitor upstream for critical fixes; rebase periodically. Known issues tracked in project memory (WASM threads #391).

## Missing Critical Features

**Aggregate Data Quality Function:**
- Problem: `ts_data_quality()` exists only as scalar/table function; no aggregate version for grouped analysis.
- Blocks: Users cannot analyze quality metrics across multiple series hierarchically.
- Implementation guide: Follow `ts_stats_agg` pattern in `src/aggregate_functions/ts_stats_agg.cpp`; add state aggregation for quality indices.

**Cross-Validation Explainability:**
- Problem: `ts_cv_forecast_by` output is minimal (forecasts only); lacks residuals, model diagnostics, or hold-out metrics per fold.
- Blocks: Users cannot diagnose why a model fails on specific folds or compare holdout performance across models.
- Implementation guide: Extend `ts_cv_forecast_by` to include optional `include_residuals` / `include_metrics` parameters; return expanded schema with residuals, MAE, RMSE per fold.

## Test Coverage Gaps

**Changepoint Detection Edge Cases:**
- What's not tested: All-null series, single-value constant series, series with NaN interleaved (not just at ends).
- Files: `crates/anofox-fcst-core/src/changepoint.rs`
- Risk: Gaps-of-all-nulls or pathological inputs may cause panic or incorrect segmentation.
- Priority: High — changepoint is user-facing public API.

**Periods Detection on Short Series:**
- What's not tested: Series <50 points; bimodal or multi-period seasonality; highly irregular sampling.
- Files: `crates/anofox-fcst-core/src/periods.rs`, `crates/anofox-fcst-core/src/seasonality.rs`
- Risk: Heuristic-based detection may misidentify or crash on edge cases.
- Priority: Medium — most real datasets are longer, but forecasting short series is a known use case.

**Conformal Prediction Coverage on Real Data:**
- What's not tested: Coverage rates on regression datasets (e.g., M5, electricity load); known failure modes (e.g., very asymmetric residuals, bimodal residual distributions).
- Files: `test/sql/ts_conformal_coverage.test`, `crates/anofox-fcst-core/src/conformal.rs`
- Risk: Intervals may fail coverage target on real data, contradicting the coverage guarantee.
- Priority: Medium — currently relies on synthetic data.

**FFI Error Paths:**
- What's not tested: Allocation failure handling; null pointer arguments to all FFI functions; out-of-bounds array access.
- Files: `crates/anofox-fcst-ffi/src/lib.rs`, `crates/anofox-fcst-ffi/tests/core_ffi_parity.rs`
- Risk: Undetected segfaults on malformed input or low-memory conditions.
- Priority: High — FFI is the DuckDB boundary; failures here crash the entire engine.

**WASM Build Correctness:**
- What's not tested: Actual loading and execution of WASM artifacts in browser/Node.js; only CI artifact generation is tested.
- Files: CI configuration, WASM exclusion/inclusion settings in `MainDistributionPipeline.yml`
- Risk: WASM binary ships but silently fails to load (e.g., missing LINKED_LIBS, shared memory disabled).
- Priority: High if WASM is a supported platform; medium if web support is future work.

---

*Concerns audit: 2026-08-20*
