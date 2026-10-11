---
phase: 14-tsfresh-feature-parity
verified: 2026-10-12T08:00:00Z
status: passed
score: 5/5 must-haves verified
covered_files:
  - .planning/phases/14-tsfresh-feature-parity/14-01-PLAN.md
  - .planning/phases/14-tsfresh-feature-parity/14-01-SUMMARY.md
  - .planning/phases/14-tsfresh-feature-parity/14-02-PLAN.md
  - .planning/phases/14-tsfresh-feature-parity/14-02-SUMMARY.md
  - .planning/phases/14-tsfresh-feature-parity/14-03-PLAN.md
  - .planning/phases/14-tsfresh-feature-parity/14-03-SUMMARY.md
  - .planning/phases/14-tsfresh-feature-parity/14-04-PLAN.md
  - .planning/phases/14-tsfresh-feature-parity/14-04-SUMMARY.md
  - CHANGELOG.md
  - crates/anofox-fcst-core/Cargo.toml
  - crates/anofox-fcst-core/src/features.rs
  - crates/anofox-fcst-core/src/stats.rs
  - crates/anofox-fcst-core/tests/tsfresh_features_reference.rs
  - docs/api/20-feature-extraction.md
  - docs/api/21-feature-reference.md
  - examples/feature_extraction/README.md
  - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
  - src/macros/ts_macros.cpp
  - test/sql/ts_agg_duplicate_timestamp_tiebreak.test
  - test/sql/ts_features_reference.test
  - validation/reference/README.md
  - validation/reference/fixtures/tsfresh_features/mapping.json
  - validation/reference/python/gen_tsfresh_features.py
covered_digest: "v1:sha256:ddcee5231e97e420d58063532befe5b5df9719eb0fa4281ac6b65340319795a5"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 14: tsfresh Feature Parity Verification Report

**Phase Goal:** All 117 features returned by `ts_features*` match tsfresh on reference series, or carry a documented, intentional deviation.
**Verified:** 2026-10-12T08:00:00Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 (SC1/FEAT-01) | `partial_autocorrelation` lags 1–5 via Durbin-Levinson match statsmodels `pacf(method='ld')`/tsfresh; lags 3–5 no longer reuse the lag-2 formula | ✓ VERIFIED | `pacf_durbin_levinson()` (features.rs:549–592) implements the real Durbin-Levinson recursion with adjusted `(n-k)` autocovariance. `short_n20` fixture row values (`lag3 -0.025999690982590442`, `lag4 -0.39110953857042163`, `lag5 0.1107535522386214`) match PLAN anchors exactly and distinct from each other (no longer the old reused `-0.06739432948956775`). `cargo test -p anofox-fcst-core --test tsfresh_features_reference` → 12/12 passed including `tsfresh_parity_short_n20`. CHANGELOG before/after table documents the exact regression. |
| 2 (SC2/FEAT-02) | `mean_second_derivative_central` (½ factor), `last_location_of_max/min` ((idx+1)/n), `time_reversal_asymmetry_statistic`, signed `agg_linear_trend` rvalue, bias-corrected skewness/kurtosis equal tsfresh on fixtures | ✓ VERIFIED | `short_n20` fixture: `mean_second_derivative_central 0.030861111111111117`, `last_location_of_maximum 0.9`, `last_location_of_minimum 0.5`, `time_reversal_asymmetry_stat_1 -1.0168216098333345`, `variation_coefficient 0.5026400051317574` — all match PLAN 14-02 anchors. Regression test `test/sql/ts_agg_duplicate_timestamp_tiebreak.test` passes (8 assertions) asserting `last_location_of_maximum = 1.0` on duplicate-timestamp data. `sample_skewness_g1`/`sample_excess_kurtosis_g2` are shared `stats.rs` helpers (grep confirms single implementation used by both `ts_stats` and `ts_features`). `cargo test -p anofox-fcst-core --lib` → 356/356 passed. |
| 3 (SC3/FEAT-03) | `fft_coefficient` unnormalised via FFT O(n log n); spectral centroid/variance use one-sided tsfresh `fft_aggregated` definition | ✓ VERIFIED | `rustfft` is a direct dependency (`Cargo.toml:23: rustfft = "6.4"`), exactly one locked version (`Cargo.lock` single `rustfft` package). `one_sided_spectrum()`/`fft_aggregated_moments()` (features.rs:1121–1160) implement a single FFT feeding both families. `short_n20` fixture: `fft_coefficient_0_real 55.537`, `fft_coefficient_3_abs 9.654943456108715`, `spectral_centroid 2.740689544749791`, `spectral_variance 11.264706553070855` — match PLAN anchors exactly (previously 2.777/0.483/2.017/22.834). Executed `cargo test -p anofox-fcst-core --lib features::tests::test_one_sided_spectrum_prime_n_cosine_peak` (n=100003, prime) → passed in 0.28s, proving O(n log n) not O(n²) (a naive DFT at this n would be computationally infeasible in this time). `test_one_sided_spectrum_parseval_unnormalised` → passed. `cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core` → succeeded (WASM-safe). |
| 4 (SC4/FEAT-04) | `sample_entropy` ≈1.099 (was 0.981), `binned_entropy`, `benford_correlation`, `permutation_entropy`, `lempel_ziv_complexity`, `number_peaks` match tsfresh or are documented deviations | ✓ VERIFIED | Executed `cargo test -p anofox-fcst-core --test tsfresh_features_reference sample_entropy_roadmap_anchor` → passed, asserting `sample_entropy == 1.0986122886681098` (ln 3) on `short_n20`, matching the ROADMAP anchor exactly (was 0.9808292530117262). CHANGELOG documents permutation_entropy (0.9338→1.6731, un-normalised), lempel_ziv_complexity (1.5127→0.4, tsfresh bins=2 algorithm), approximate_entropy degenerate-case fix. `number_peaks` confirmed unchanged/already-correct per CHANGELOG. All changes are in `mapping.json` with `status: match`. |
| 5 (SC5/FEAT-05) | tsfresh-generated fixture over ≥4 series covers all 117 features; every feature asserted within tolerance or on documented deviation list | ✓ VERIFIED | `mapping.json` has exactly 117 feature rows, **all** `status: "match"` (verified via direct JSON parse — zero non-match rows). 9 fixture series present (white_noise_n200, random_walk_n200, seasonal_ar12_n144, trend_season_n120, intermittent_n100, short_n20, constant_n30, all_zero_n50, tiny_n8) — exceeds the ≥4 requirement. `cargo test -p anofox-fcst-core --test tsfresh_features_reference` → 12/12 passed (9 per-series parity tests + coverage-guard + roadmap-anchor + fixture-presence test), each asserting exactly 117 features per series (enforced by the harness itself, not a silent skip). Regenerated the fixture generator live (`uv run --locked --project validation/reference/python python gen_tsfresh_features.py`) — RuntimeWarnings from live `statsmodels`/`tsfresh` internals confirm real calculator calls (not fabricated), and `git diff` against the regenerated fixtures shows **zero diff** (byte-identical, deterministic). The two documented deviations (`skewness`/`kurtosis` on zero-variance series) are recorded in `mapping.json`'s `deviation` field and mirrored in `validation/reference/README.md`. |

**Score:** 5/5 truths verified (0 present, behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `crates/anofox-fcst-core/src/features.rs` | Durbin-Levinson PACF, FFT, entropy/complexity fixes, 117-key invariant | ✓ VERIFIED | `fn pacf_durbin_levinson`, `fn one_sided_spectrum`, `fn fft_aggregated_moments`, `fn sample_entropy`, `fn approximate_entropy`, `fn binned_entropy`, `fn benford_correlation`, `fn permutation_entropy`, `fn lempel_ziv_complexity` all present and exercised by passing tests |
| `crates/anofox-fcst-core/src/stats.rs` | Shared bias-corrected moment / template-match helpers | ✓ VERIFIED | `sample_skewness_g1`/`sample_excess_kurtosis_g2`/`template_match_counts` present, imported by `features.rs`, used by both `ts_stats` and `ts_features` |
| `validation/reference/fixtures/tsfresh_features/mapping.json` | 117-row parity contract, all `status: match` | ✓ VERIFIED | Parsed directly: 117 rows, 0 non-match, 2 documented deviations |
| `validation/reference/python/gen_tsfresh_features.py` | Calls tsfresh calculators directly, deterministic | ✓ VERIFIED | `from tsfresh.feature_extraction import feature_calculators as fc`; regenerated live, byte-identical diff against checked-in fixtures |
| `crates/anofox-fcst-core/tests/tsfresh_features_reference.rs` | 117/117 parity harness, ratchet removed (FEAT-05 gate) | ✓ VERIFIED | 12 tests, all passing, `mapping_covers_list_features_exactly` enforces exact coverage + all-match |
| `test/sql/ts_features_reference.test` | Non-json-gated SQL assertions | ✓ VERIFIED | `require anofox_forecast` only (no json gate); executed against built extension → 111 assertions, 1 test case, all passed |
| `docs/api/21-feature-reference.md` | 117-row Definition/tsfresh-equivalent/Deviation table | ✓ VERIFIED | All 117 mapping names present (113 matched by backtick-regex + 4 `quantile_0.X` rows containing a dot); 4-column table confirmed |
| `CHANGELOG.md` | Before/after table for every changed feature | ✓ VERIFIED | Comprehensive before/after rows for PACF, autocorrelation, locations, FFT, spectral, entropy, permutation entropy, Lempel-Ziv, approximate_entropy present under `## [Unreleased]` |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `features.rs` skewness/kurtosis | `stats.rs` `sample_skewness_g1`/`sample_excess_kurtosis_g2` | shared helper call | ✓ WIRED | grep confirms single implementation imported and called from `features.rs` |
| `features.rs` `fft_coefficient_*`/`spectral_*` | `rustfft::FftPlanner` via `one_sided_spectrum` | one spectrum computed once, feeds both families | ✓ WIRED | `plan_fft_forward` call confirmed in `one_sided_spectrum`; both feature families consume its output |
| `gen_tsfresh_features.py` | `tsfresh.feature_extraction.feature_calculators` | direct per-row calculator dispatch | ✓ WIRED | `FORMULAS`/calculator dispatch confirmed; live regeneration proves real calls (RuntimeWarnings from genuine tsfresh internals) |
| `mapping.json` rows | `tsfresh_features_reference.rs` test harness | row-driven assertion loop | ✓ WIRED | Harness reads `mapping.json` directly; 12/12 tests pass; coverage-guard test enforces 1:1 correspondence with `list_features()` |
| `ts_features_by`/`ts_features`/`ts_features_agg` SQL surfaces | `extract_features()` core | FFI passthrough, `src/macros/ts_macros.cpp` | ✓ WIRED | SQL test (`ts_features_reference.test`) executed against built extension confirms values reach SQL surfaces; `autocorrelation_lag10` schema fix also confirmed present |

### Behavioral Spot-Checks / Executed Evidence

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Rust parity harness (12 tests) | `cargo test -p anofox-fcst-core --test tsfresh_features_reference` | 12 passed, 0 failed | ✓ PASS |
| Rust lib unit tests (356 tests) | `cargo test -p anofox-fcst-core --lib` | 356 passed, 0 failed | ✓ PASS |
| `cargo fmt` check | `cargo fmt --all --check` | clean, exit 0 | ✓ PASS |
| SQL reference test against built extension | `./build/release/test/unittest test/sql/ts_features_reference.test` | 111 assertions, 1 test case, all passed | ✓ PASS |
| Duplicate-timestamp regression test | `./build/release/test/unittest test/sql/ts_agg_duplicate_timestamp_tiebreak.test` | 8 assertions, all passed | ✓ PASS |
| Full native SQL test suite | `./build/release/test/unittest` | 3870 assertions in 62 test cases, all passed (100 skipped, `require json`) | ✓ PASS |
| FFT prime-n O(n log n) proof | `cargo test -p anofox-fcst-core --lib features::tests::test_one_sided_spectrum_prime_n_cosine_peak` | passed in 0.28s on n=100003 (prime) | ✓ PASS |
| FFT Parseval correctness | `cargo test -p anofox-fcst-core --lib features::tests::test_one_sided_spectrum_parseval_unnormalised` | passed | ✓ PASS |
| WASM build check | `cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core` | succeeded | ✓ PASS |
| sample_entropy ROADMAP anchor | `cargo test -p anofox-fcst-core --test tsfresh_features_reference sample_entropy_roadmap_anchor` | passed (1.0986122886681098 = ln 3) | ✓ PASS |
| Fixture determinism | `uv run --locked --project validation/reference/python python gen_tsfresh_features.py` then `git diff --stat` | zero diff — byte-identical regeneration | ✓ PASS |
| 117-key invariant / NaN-no-panic / -0.0 canonicalisation | `cargo test -p anofox-fcst-core --lib features::tests::test_117_key_invariant_across_lengths features::tests::test_extract_features_with_nan_returns_117_keys_without_panicking features::tests::test_negative_zero_treated_as_zero_in_unique_duplicate_features` | all 3 passed | ✓ PASS |
| Runnable example against built extension | `duckdb -unsigned` + `LOAD` + `examples/feature_extraction/synthetic_feature_examples.sql` | completed end-to-end, produced classification tables | ✓ PASS |
| FFI header unchanged (D-14) | `md5sum src/include/anofox_fcst_ffi.h` | `6ac65ee57445a99a7c665f380c60cb55` — matches REVIEW-FIX.md's recorded value | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| FEAT-01 | 14-01 | PACF Durbin-Levinson parity | ✓ SATISFIED | Truth #1 evidence above |
| FEAT-02 | 14-02 | derivative/location/time-reversal/agg_linear_trend/moments parity | ✓ SATISFIED | Truth #2 evidence above |
| FEAT-03 | 14-03 | FFT unnormalised O(n log n) + one-sided spectral moments | ✓ SATISFIED | Truth #3 evidence above |
| FEAT-04 | 14-04 | entropy/Benford/permutation/Lempel-Ziv/peaks parity | ✓ SATISFIED | Truth #4 evidence above |
| FEAT-05 | 14-01/14-04 | 117/117 fixture gate across ≥4 series | ✓ SATISFIED | Truth #5 evidence above |

**Note:** `.planning/REQUIREMENTS.md` still shows FEAT-01..05 checkboxes unchecked and "Pending" in the tracking table — this is bookkeeping lag (updated at phase-completion, not blocking the goal-achievement verdict); all five IDs are accounted for by the plans and codebase evidence above with no orphans.

### Anti-Patterns Found

None. Scanned `features.rs`, `stats.rs`, `tsfresh_features_reference.rs`, `ts_features_reference.test`, `docs/api/21-feature-reference.md` for `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER`/stale "fix planned" notes — zero hits in Phase-14-owned files. Remaining "fix planned for v0.10.0" notes elsewhere in the docs tree reference other future phases (11, 15, 16, 17), correctly out of this phase's scope.

### D-16 Decision Compliance

- **D-04** (breaking changes documented, no compat flag): CHANGELOG before/after table present and comprehensive; no compatibility flag introduced. ✓
- **D-12** (non-json-gated SQL gate): `test/sql/ts_features_reference.test` confirmed `require anofox_forecast` only, executed and passing (111 assertions). ✓
- **D-14** (minimal C++/FFI change): FFI header md5 unchanged across the phase (`6ac65ee5...`); `ts_macros.cpp` changes are passthrough struct-field additions only. ✓
- **D-15** (docs updated, stale notes removed): `docs/api/21-feature-reference.md`, `docs/api/20-feature-extraction.md`, EDA skill, `examples/feature_extraction/README.md`, `validation/reference/README.md`, CHANGELOG all updated and internally consistent; "fix planned... Phase 14" notes fully removed. ✓

### Human Verification Required

None. All success criteria resolved to VERIFIED with direct, executed evidence (not SUMMARY claims).

### Gaps Summary

No gaps. All 5 ROADMAP success criteria and all 5 requirement IDs (FEAT-01..05) are backed by executed, reproducible evidence: a live-regenerated, byte-identical fixture set calling real tsfresh/numpy/scipy calculators; a 117/117 all-`match` mapping.json enforced by a passing Rust test harness (12/12); a non-json-gated SQL test passing against the actual built extension (111 assertions); an O(n log n) FFT proven via a prime-length-n timing test; and a complete, consistent documentation/CHANGELOG update with the stale Phase-14 "fix planned" notes removed. The only informational note is that `.planning/REQUIREMENTS.md`'s own checkbox/status table has not yet been flipped to done — a bookkeeping item, not a goal-achievement gap.

---

_Verified: 2026-10-12T08:00:00Z_
_Verifier: Claude (gsd-verifier)_
