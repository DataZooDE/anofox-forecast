---
phase: 15-detection-decomposition-on-fdars-0-47
verified: 2026-10-14T13:05:00Z
status: passed
score: 9/9 must-haves verified
covered_files:
  - ".planning/REQUIREMENTS.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-01-PLAN.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-01-SUMMARY.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-02-PLAN.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-02-SUMMARY.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-03-PLAN.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-03-SUMMARY.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-04-PLAN.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-04-SUMMARY.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-05-PLAN.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-05-SUMMARY.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-CONTEXT.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW-FIX.iter1.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW-FIX.iter2.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW-FIX.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.iter1.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.iter2.md"
  - ".planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.md"
  - "Cargo.lock"
  - "Cargo.toml"
  - "crates/anofox-fcst-core/Cargo.toml"
  - "crates/anofox-fcst-core/src/changepoint.rs"
  - "crates/anofox-fcst-core/src/decomposition.rs"
  - "crates/anofox-fcst-core/src/detrending.rs"
  - "crates/anofox-fcst-core/src/filter.rs"
  - "crates/anofox-fcst-core/src/peaks.rs"
  - "crates/anofox-fcst-core/src/periods.rs"
  - "crates/anofox-fcst-core/src/seasonality.rs"
  - "crates/anofox-fcst-core/src/stats.rs"
  - "crates/anofox-fcst-ffi/src/lib.rs"
  - "src/aggregate_functions/ts_classify_seasonality_agg.cpp"
  - "src/macros/ts_macros.cpp"
  - "src/table_functions/ts_decomposition.cpp"
  - "src/table_functions/ts_detrend.cpp"
  - "src/table_functions/ts_mstl_decomposition_native.cpp"
  - "src/table_functions/ts_peaks.cpp"
  - "src/table_functions/ts_periods.cpp"
  - "src/table_functions/ts_seasonality.cpp"
  - "test/sql/ts_detection_reference.test"
covered_digest: "v1:sha256:661df321218df3e94f2b172097d1485c71ef29feded5b1f2e6e0112bd2b8cac0"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 15: Detection & Decomposition on fdars 0.47 Verification Report

**Phase Goal:** Detrending, seasonality classification, period detection, peaks, changepoints and MSTL decomposition
return correct, non-trivial results matching numpy / scipy / R — running on fdars-core 0.47, whose FdMatrix API
structurally prevents the swapped size-argument bug that made detrend/decompose/seasonality no-ops.

**Verified:** 2026-10-14T13:05:00Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1a | fdars-core 0.47, features `["parallel"]` (no `linalg`), no duplicate `anofox-regression 0.4.0` | ✓ VERIFIED | `Cargo.lock` has exactly one `fdars-core` entry at `0.47.0`; `crates/anofox-fcst-core/Cargo.toml` requests `["parallel"]` native / `["js"]` WASM, no `linalg`; `cargo tree -i anofox-regression` shows a single `anofox-regression v0.5.15` node with no `0.4.0` anywhere in the tree |
| 1b | WASM native + FFI compile | ✓ VERIFIED | `cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core -p anofox-fcst-ffi` → `Finished` cleanly, no errors |
| 1c | `ts_detrend`/`ts_detrend_by` cubic matches numpy `polyfit` to 1e-8 (was all-0 trend) | ✓ VERIFIED | Live query on cubic40: `trend[1]=3.1545111619804027` (expected `3.154511161980401`), `trend[40]=19.378996329308848` (expected `19.378996329308837`), `method='polynomial(3)'`, `n_params=4`, coefficients match within 1e-8; also pinned in `test/sql/ts_detection_reference.test` (147/147 assertions pass) |
| 1d | `'quadratic'` → degree 2 or errors; unknown method errors naming valid methods | ✓ VERIFIED | Live: `'quadratic'` → `method='polynomial(2)'`, `n_params=3`; `'quartic'` → `Invalid Input Error: ... expected one of: linear, quadratic (alias polynomial, poly), cubic, diff ..., loess ..., auto` |
| 1e | `ts_decompose_seasonal` non-trivial | ✓ VERIFIED | Live: constant series (n=48, period 12) → trend exactly `1.0`, seasonal `~1.26e-17` (≈0); trend+seasonal series → seasonal component correlates `0.9996` with the injected `3*sin(2πt/12)`, and `trend+seasonal+remainder` reproduces input within `3.55e-15` |
| 1f | `classify_seasonality` labels a strong sine as seasonal | ✓ VERIFIED | Live: 10+5·sin sine(period 12) → `is_seasonal=true`, `timing_classification='stable_seasonal'`, `seasonal_strength=0.9999999999999993`; white-noise control → `is_seasonal=false`, `seasonal_strength≈0.013` |
| 2 | Peak detection follows scipy `find_peaks` semantics (indices, plateaus, prominence); matches scipy on fixtures | ✓ VERIFIED | `crates/anofox-fcst-core/tests/peaks_reference.rs` (scipy `rw150`/`sine240` fixtures) passes in the full `cargo test --workspace --release` run (0 failed); live plateau check `[0,1,1,1,0,2,2,0,3,3,3,3,1,0]` → peaks at indices `2,5,9` (midpoints, correct prominences `1.0,2.0,3.0`), no minima reported; `test/sql/ts_detection_reference.test` pins `rw150` indices/prominences against scipy exactly |
| 3 | BOCPD detects a 3σ step and stays quiet on noise; PELT scale-invariant, median-L1, working pruning | ✓ VERIFIED | Live/test-file: `_ts_detect_changepoints_bocpd(step3,250.0,true)` → `changepoint_indices=[100]`; noise series → `[]`; `step3*100+7` → still `[100]` (scale/shift invariant); issue-71 step → flags `[12]`, `changepoint_probability[13]>0.5`; probability vector matches an independent numpy reference to 1e-10 (all confirmed passing in `test/sql/ts_detection_reference.test`, 147/147). `crates/anofox-fcst-core/tests/changepoint_reference.rs::{pelt_matches_numpy_exact_dp, pelt_is_scale_and_shift_invariant}` pass (4/4 in that binary, 0 failed) |
| 4 | MSTL processes periods shortest-first with Loess/STL matching R `forecast::mstl`; even-window moving averages correct (constant 1s → trend exactly 1.0) | ✓ VERIFIED | `crates/anofox-fcst-core/tests/mstl_decompose_reference.rs` (R `forecast::mstl`/`stats::decompose` fixtures, AirPassengers/det84/msts_7_28) passes in the full workspace run; `test/sql/ts_detection_reference.test` pins constant-1.0 MSTL trend exactly 1.0 (n=48 period 12, and n=100 non-seasonal supsmu path) and classical `stats::decompose` trend[7]/seasonal[1] within 1e-10 of R on AirPassengers |
| 5a | FFT period returns seasonal period (not n), handles odd n | ✓ VERIFIED | Live: `0.5t+2sin(2πt/12)`, n=120 → `period=12.0`; n=121 (odd) → `period=12.1` |
| 5b | ACF accepts lag 2, requires significance | ✓ VERIFIED | Live: alternating `[1,-1]*20` (n=40) → `period=2.0` (previously unreachable lag) |
| 5c | Regression-period defaults find 12 | ✓ VERIFIED | `crates/anofox-fcst-core/tests/period_detection_reference.rs::regression_rss_matches_numpy_lstsq` calls `estimate_period_regression_ts(..., None, None, ...)` (default integer grid 2..n/2) against fixtures `shape12_n144`/`pure_sine12_n120`, both expecting period `12.0`; test passes. (Function remains Rust/FFI-internal — not SQL-exposed — consistent with REVIEW.md IN-01, carried as pre-existing, no-action-required info) |
| 5d | Lomb-Scargle rejects invalid inputs | ✓ VERIFIED | Live: `n_frequencies=1` → `Invalid Input Error: ... n_frequencies' = '1': must be between 2 and 1,000,000`; `min_period(20) >= max_period(5)` → `Invalid Input Error: ... min_period' = '20': must be less than max_period` |
| 5e | Seasonal-strength variants detrend first; wavelet strength removed | ✓ VERIFIED | Live: `ts_seasonal_strength(..., 'wavelet')` → `Invalid Input Error: ... removed: seasonal_strength_wavelet returned 1.0 even for white noise in fdars-core 0.47; use 'variance' (default) or 'spectral'` |
| 5f | `is_constant` scale-relative | ✓ VERIFIED | Live: `_ts_is_constant([1e12, 1e12+0.001])` → `true`; `_ts_is_constant([1.0, 1.0+1e-9])` → `false` (tolerance `1e-10 * max(1,max|x|)`, confirmed in `filter.rs`) |
| 5g | Edge-NULL interpolation documented (D-14 revised: constant-extension fill kept) | ✓ VERIFIED | `docs/api/04-data-preparation.md` pins forward/backward-fill edge behavior (`test/sql/ts_detection_reference.test` line ~876 section); behavior unchanged per D-14, documented not changed |

**Score:** 9/9 roadmap success criteria verified (17 sub-truths, all VERIFIED, 0 behavior-unverified)

### Code Review Loop (CR/WR Findings)

The phase went through 4 review/fix iterations (`15-REVIEW.iter1.md` → `15-REVIEW-FIX.iter1.md` → ... → `15-REVIEW.md`/`15-REVIEW-FIX.md`, the latter a user-authorised iteration 4 beyond the standard 3-iteration cap). All 9 critical findings (CR-01..CR-07) and 4 warnings (WR-01..WR-04) across the loop were fixed. The final re-review (iteration 3) found 2 new critical issues (CR-06, CR-07) and 1 warning (WR-04) while re-verifying the prior iteration's fixes; iteration 4 fixed all three. This verifier independently reproduced the key fixes live against the rebuilt extension:

- `ts_classify_seasonality(series, 0.0)` → raises `Invalid Input Error: ... Period must be a positive finite number` (CR-06, confirmed fixed — previously returned NULL silently)
- `ts_detect_peaks` plateau handling, BOCPD 3σ/noise/scale-invariance, Lomb-Scargle validation, MSTL constant-trend-exactly-1.0 — all reproduced live above

Remaining open items (IN-01, IN-02 in `15-REVIEW.md`) are **info-only**, explicitly marked "no action required": `estimate_period_regression_ts`/`regression_period_rss` are dead code from an SQL perspective (pre-existing, not introduced by this phase, not required by any must-have), and IN-02 is resolved as a side effect of CR-04's fix.

### Executed Evidence Summary

| Check | Command | Result |
|-------|---------|--------|
| `make test_release` (detection file only) | `build/release/test/unittest test/sql/ts_detection_reference.test` | **All tests passed (147 assertions in 1 test case)** |
| Full Rust workspace tests | `RUSTUP_TOOLCHAIN=1.89.0 cargo test --workspace --release` | **0 failed** across all test binaries incl. `changepoint_reference` (4/4), `detrend_reference`, `mstl_decompose_reference`, `peaks_reference`, `period_detection_reference`, `reference_fixtures`, `stl_reference`, `stats_reference` |
| WASM check (DEP-03, D-03) | `cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core -p anofox-fcst-ffi` | **Finished cleanly**, no errors |
| `cargo tree -i anofox-regression` | — | Single node `anofox-regression v0.5.15`, no `0.4.0` anywhere |
| Build freshness | `stat` build artifact vs. `git log -1` | Extension binary (11:30:50) built after HEAD's only non-source commit (11:32:38 docs-only); all source files are older than the binary — build is current |
| Snippet harness | `pass-list-15-0{1..5}.tsv` | Every row is `PASS` or `SYNOPSIS` (expected non-executable signature lines) — **0 FAIL** |
| Debt markers | `grep TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER` on all 9 modified core/FFI/C++ files | **None found** |
| Stale "fix planned" sweep | `grep -rn "fix planned"` docs/plugins/README/CHANGELOG | All matches reference Phase 11/17 (diagnostics, unrelated), **none reference Phase 15** |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `crates/anofox-fcst-core/src/detrending.rs` | `single_series_matrix` helper, strict method parse | ✓ VERIFIED | Function present; live-verified polyfit parity and strict error on unknown method |
| `crates/anofox-fcst-core/src/peaks.rs` | scipy `find_peaks` port | ✓ VERIFIED | `find_peaks`, `local_maxima`, `peak_prominences`, `detect_peaks` present; 200000-point/<5s perf test present (`peaks.rs:851-859`) and passing |
| `crates/anofox-fcst-core/src/changepoint.rs` | BOCPD (Adams & MacKay) + PELT | ✓ VERIFIED | `robust_noise_scale`, `bocpd_trace`, scale-invariant PELT present; reference tests pass |
| `crates/anofox-fcst-core/src/decomposition.rs` | shared `mstl_fit`, `classical_decompose` | ✓ VERIFIED | Shared implementation confirmed by SUMMARY and live R-fixture-matching queries |
| `crates/anofox-fcst-core/src/periods.rs` | detrended periodogram, ACF, regression, Lomb-Scargle | ✓ VERIFIED | All four live-verified against the roadmap's exact numeric anchors |
| `crates/anofox-fcst-core/src/filter.rs` | scale-relative `is_constant` | ✓ VERIFIED | Live-verified `1e12` vs `1.0` scale cases |
| `test/sql/ts_detection_reference.test` | non-json-gated, 100+ assertions | ✓ VERIFIED | 147 assertions, all passing, no `require json` line |

### Requirements Coverage

| Requirement | Source Plan | Status | Evidence |
|---|---|---|---|
| DEP-03 | 15-01 | ✓ SATISFIED | fdars-core 0.47 single version, no linalg, no duplicate anofox-regression 0.4.0, WASM check clean |
| WRAP-01 | 15-01 | ✓ SATISFIED | ts_detrend/_by, classify_seasonality, decompose_seasonal all non-trivial and numpy/live-verified |
| DET-01 | 15-02 | ✓ SATISFIED | scipy find_peaks port, plateau/prominence live-verified, reference tests pass |
| DET-02 | 15-03 | ✓ SATISFIED | BOCPD 3σ/noise/scale-invariance live-verified |
| DET-03 | 15-03 | ✓ SATISFIED | PELT scale-invariance + exact-DP parity test passing |
| DET-04 | 15-04 | ✓ SATISFIED | MSTL/classical decomposition R-fixture parity, constant-1.0 trend live-verified |
| DET-05 | 15-05 | ✓ SATISFIED | FFT/ACF/regression/Lomb-Scargle live-verified against exact roadmap anchors |
| DET-06 | 15-05 | ✓ SATISFIED | Wavelet removal live-verified; variance/spectral detrend-first per SUMMARY + passing tests |
| DET-07 | 15-05 | ✓ SATISFIED | is_constant scale-relative live-verified; edge-NULL documented per D-14 |

**Note on REQUIREMENTS.md bookkeeping:** at the time of this verification, `.planning/REQUIREMENTS.md` shows DET-02/DET-03 as `[x] Complete` and the other 7 phase-15 IDs (DEP-03, WRAP-01, DET-01, DET-04..DET-07) as `[ ] Pending`. This is a checklist-maintenance lag, not a gap in implementation — every one of the 9 IDs is independently verified above with executed evidence (live queries against the rebuilt extension and/or passing Rust reference tests). No requirement is orphaned (`.planning/REQUIREMENTS.md`'s own "Phase 15" mapping table lists exactly these 9 IDs, matching the PLAN frontmatter `requirements:` fields across 15-01..15-05). Updating the checkboxes is a bookkeeping action for the ship/complete step, not a verification blocker.

### Anti-Patterns Found

None. No TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER markers in any of the 9 modified Rust/FFI/C++ files. No stale Phase-15 "fix planned" notes in docs/skills/CHANGELOG/README (all such notes found reference Phase 11/17, unrelated diagnostics work).

### Human Verification Required

None. All roadmap success criteria had executable, numeric evidence (live SQL queries against the rebuilt extension, or passing Rust/SQL reference tests with checked-in numpy/scipy/R fixtures). No behavior-dependent truth was left unexercised.

### Gaps Summary

No gaps. All 5 phase plans (15-01..15-05) delivered their claimed artifacts; the 4-iteration code-review loop converged with all critical and warning findings fixed and independently reproduced live by this verifier; the full Rust workspace test suite and the non-gated SQL reference test both pass with zero failures; the WASM check is clean; the dependency bump (fdars-core 0.47, no duplicate anofox-regression 0.4.0) is confirmed in `Cargo.lock`/`cargo tree`.

---

_Verified: 2026-10-14T13:05:00Z_
_Verifier: Claude (gsd-verifier)_
