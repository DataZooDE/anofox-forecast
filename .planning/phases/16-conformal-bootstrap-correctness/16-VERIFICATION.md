---
phase: 16-conformal-bootstrap-correctness
verified: 2026-10-14T16:00:00Z
status: passed
score: 11/11 must-haves verified
covered_files:
  - ".planning/REQUIREMENTS.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-01-PLAN.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-01-SUMMARY.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-02-PLAN.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-02-SUMMARY.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-03-PLAN.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-03-SUMMARY.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-CONTEXT.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-REVIEW-FIX.iter1.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-REVIEW-FIX.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-REVIEW.iter1.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/16-REVIEW.md"
  - ".planning/phases/16-conformal-bootstrap-correctness/deferred-items.md"
  - "CHANGELOG.md"
  - "crates/anofox-fcst-core/src/bootstrap.rs"
  - "crates/anofox-fcst-core/src/conformal.rs"
  - "crates/anofox-fcst-core/src/lib.rs"
  - "crates/anofox-fcst-core/tests/conformal_reference.rs"
  - "crates/anofox-fcst-ffi/src/lib.rs"
  - "crates/anofox-fcst-ffi/src/types.rs"
  - "docs/api/11-conformal-prediction.md"
  - "plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md"
  - "src/include/anofox_fcst_ffi.h"
  - "src/macros/ts_macros.cpp"
  - "src/scalar_functions/bootstrap.cpp"
  - "src/scalar_functions/conformal.cpp"
  - "test/sql/ts_conformal.test"
  - "test/sql/ts_conformal_null_policy.test"
  - "test/sql/ts_conformal_reference.test"
covered_digest: "v1:sha256:bbec130200c6b04990ea3676962947ee67b0c99845434c6be2aeb6e4182479b9"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 16: Conformal & Bootstrap Correctness Verification Report

**Phase Goal:** Conformal and bootstrap intervals have their nominal coverage and correct shape end-to-end — horizon order and pairing preserved in SQL, scores and empirical coverage computed in Rust, per-group calibration available, and each conformal variant implementing the method it is named after.

**Verified:** 2026-10-14
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP success criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1a | `ts_conformal_by`/`ts_conformal_apply_by`/`ts_interval_width_by` order and pair by a date/step column | ✓ VERIFIED | Live query against the built extension (`verify16c3.sql`): `ts_conformal_by(backtest_results, group_col, date_col, actual_col, forecast_col, point_forecast_col, ...)` returns one row per `(group_col, date_col)`, correctly paired. `test/sql/ts_conformal_reference.test` lines 79-95: group A's point forecasts `30, 10, 20` (input order, not value-sorted `[10,20,30]`) come back at their own dates with the exact predicted bounds (28.095/31.905 etc., within 1e-9) — all 132 assertions in this file pass live (`build/release/test/unittest`). |
| 1b | Residuals and empirical (not nominal) coverage computed in Rust | ✓ VERIFIED | `conformal_from_pairs_matches_numpy_per_group` and `conformal_calibrate_pooled_matches_numpy` (Rust `conformal_reference.rs`, 22/22 tests pass, run live) cross-check against an independent numpy reference. SQL test lines 122-131: two_group alpha=0.1 coverage computed as `0.95` empirical (19/20 within bound) vs. `nominal_level = 0.9` (previously the literal `1-alpha`). `grep` of `ts_macros.cpp` conformal macros shows no `actual - forecast` arithmetic in SQL; residual computation happens only through the Rust-backed `_ts_conformal_*` scalars. |
| 1c | `ts_conformal_calibrate` calibrates per group (two groups → different quantiles) | ✓ VERIFIED | Live query (`verify16c3.sql` on two_group-shaped fixture) and SQL test lines 185-202: group A `conformity_score = 1.9050000000000056`, group B `= 19.05` (different, per plan); `params := {'pooled': true}` collapses to one row, `group_col IS NULL`, `conformity_score = 17.075000000000003`. Both STRUCT and MAP pooled forms agree. |
| 2 | Bootstrap intervals on a right-skewed fixture place the long tail on the correct side | ✓ VERIFIED | Live query: `ts_bootstrap_intervals([0,0,0,0,0,0,0,0,0,10]::DOUBLE[], [100.0], 2000, 0.9, 42)` → `lower=100.0, upper=110.0` (long tail correctly on the upper side; matches CHANGELOG before/after and D-10 exactly). Rust tests `bootstrap_mirror_anchor_puts_long_tail_on_upper_side` and `bootstrap_lognormal_right_and_left_match_numpy_tail_side_and_width` (60-residual log-normal fixture, 20000 paths, numpy cross-check) pass live. |
| 3 | Split conformal returns NULL/infinite width or errors when ⌈(n+1)(1−α)⌉ > n; rejects α∉(0,1); filters NaN without panicking | ✓ VERIFIED | Live query on a 3-row group at default alpha=0.1: all bounds/`conformity_score`/`coverage` NULL, `calibration_insufficient = true`, `n_calibration = 3` — never clamped. SQL test lines 330-356 (5-pair group, alpha 0.1 → NULL + insufficient; alpha 0.2 → `conformity_score = 5.0`, not insufficient) and lines 395-439 (NaN/Infinity-laden 20-pair fixture → `n_calibration = 18`, filtered, no panic; all-NaN → `n_calibration = 0`, insufficient, not an error) pass live. Rust `invalid_alphas_are_rejected_naming_the_value`, `split_insufficiency_cases_match_numpy`, `threshold_table_matches_numpy_for_every_n_and_alpha`, `n0_is_always_insufficient_not_an_error` pass live (22/22 in `conformal_reference.rs`). |
| 4a | Asymmetric conformal uses α/2 and 1−α/2 quantiles of signed residuals; simulated coverage within ±2pp | ✓ VERIFIED | Rust `monte_carlo_symmetric_and_asymmetric_coverage_matches_numpy` and `asymmetric_seeded_cases_match_numpy_margins` / `asymmetric_hand_checked_cases_match_numpy` pass live. CHANGELOG before/after row documents the construction change (full signed-residual array + negation vs. positive/negative subset) with measured before/after margins. |
| 4b | Adaptive uses \|r\|/d at calibration and d-scaling at prediction; same interval regardless of batch | ✓ VERIFIED | Rust `adaptive_batch_independence_exact_equality`, `adaptive_n20_matches_numpy`, `adaptive_seeded_cases_match_numpy`, `adaptive_heteroscedastic_monte_carlo_coverage_matches_numpy` pass live. SQL test lines 586-624: alpha 0.1 → q=1.905; prediction with difficulty 1/2/0.5 → half-widths `1.9050/3.8100/0.9525` exactly as `d`-scaled. |
| 5 | 'JackknifePlus' implements jackknife+ or is renamed/removed with a clear error | ✓ VERIFIED | Live query: `ts_conformal_learn([1,2,3]::DOUBLE[], [0.1]::DOUBLE[], 'symmetric', 'jackknife+')` raises `Invalid Input Error: ... conformal strategy 'jackknife+' was removed: it computed the same quantile as 'split' (no leave-one-out refits). Use 'split' or 'crossval'.` — matches D-09 exactly. `grep` confirms every alias (`jackknifeplus`, `jackknife_plus`, `jk+`, case-insensitive) is handled identically in `conformal.rs`; the strategy variant itself is removed from the Rust core/FFI enum/C++ layer (no remaining `JackknifePlus` branch outside comments and the explanatory-error match arm and its own removal-regression test). |

### User Decisions (16-CONTEXT.md)

| Decision | Status | Evidence |
|---|---|---|
| D-13: one row per (group, date) output | ✓ VERIFIED | Live query output (`verify16c3.sql`) shows scalar `point`/`lower`/`upper` columns, one row per `(group_col, date_col)`, not lists. |
| D-14: calibration-table input, pooled one-row NULL-group table | ✓ VERIFIED | SQL test lines 229-249: `ts_conformal_apply_by('cfut', g, ds, fc, 'cal_pooled')` joins a calibration TABLE; the pooled table is a one-row, `group_col = NULL` table. Code-review fix commits (WR-03/WR-04, `e61a0e5`/`e77b753`/`9c92b4d`) hardened the pooled-fallback and duplicate-row validation around this exact contract and were independently reproduced live by the reviewer per `16-REVIEW.md`. |

**Score:** 11/11 truths/decisions verified (0 present-behavior-unverified, 0 overrides)

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `crates/anofox-fcst-core/src/conformal.rs` | Per-group calibration, signed-residual asymmetric, adaptive d-scaling, jackknife+ removal | ✓ VERIFIED | 2280+ lines; strict strategy/method parsing, removal match arm with explanatory error, no debt markers (`grep` clean for TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER). |
| `crates/anofox-fcst-core/src/bootstrap.rs` | Correct-sign residual resampling + validation | ✓ VERIFIED | Live bootstrap query confirms correct sign; `fit(residuals, ...)` present; no debt markers. |
| `crates/anofox-fcst-core/tests/conformal_reference.rs` | Numpy cross-checked reference tests | ✓ VERIFIED | 22/22 tests pass live (`cargo test --release -p anofox-fcst-core --test conformal_reference`). |
| `src/macros/ts_macros.cpp` (conformal macros) | date_col pairing, calibration-table join, pooled-fallback and dupe-row validation | ✓ VERIFIED | Live queries exercise `ts_conformal_by`, `ts_conformal_calibrate`, `ts_conformal_apply_by` against the rebuilt extension; behavior matches plan/CHANGELOG exactly. |
| `src/scalar_functions/conformal.cpp`, `bootstrap.cpp` | Strict alpha/strategy/method validation, FFI wiring | ✓ VERIFIED | Live error messages (`alpha must be in (0,1)`, jackknife+ removal) originate from this layer per `grep` of error strings. |
| `test/sql/ts_conformal_reference.test`, `ts_conformal_null_policy.test` | Non-gated SQL assertions | ✓ VERIFIED | 132/132 and 17/17 assertions pass, run live against `build/release/test/unittest` just now (not taken from SUMMARY). |
| `CHANGELOG.md` | Breaking-change bullet + ≥14-row before/after table | ✓ VERIFIED | 16-row before/after table present under `[Unreleased] — v0.10.0 Statistical Correctness`, covers every D-01..D-14 item with measured before/after values. |
| `docs/api/11-conformal-prediction.md`, backtest SKILL.md | Describe new contract (date_col, difficulty_col, calibration table, pooled, calibration_insufficient, jackknife+ removal) | ✓ VERIFIED | `grep` confirms all listed terms present in both files; no "fix planned for v0.10.0, Phase 16" stale note found anywhere in `docs/`, `README.md`, `examples/`. |
| Snippet harness (`.planning/phases/16-conformal-bootstrap-correctness/snippets/pass-list-16-03.tsv`) | 0 FAIL | ✓ VERIFIED | 34 PASS + 3 EXPECTED-ERROR + 15 SYNOPSIS, 0 FAIL. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `ts_conformal_by` macro | `_ts_conformal_from_pairs` / `_ts_conformal_calibrate` Rust-backed scalars | per-row (actual, forecast) pairs, date-ordered LIST | ✓ WIRED | Live query returns correct, date-paired, non-value-sorted results; residual arithmetic absent from SQL (grep). |
| `ts_conformal_apply_by` macro | calibration TABLE (`ts_conformal_calibrate` output) | `query_table` join on `group_col`, gated by `cal_pooled_eligible` / `cal_dupe_check` | ✓ WIRED | SQL reference test + code-review fix verification (WR-03/WR-04) both reproduced live. |
| `ts_conformal_learn` / `ts_conformal_apply` | Rust strategy/method parser | strict string parsing, explanatory errors | ✓ WIRED | Live query reproduces the exact jackknife+ error string documented in CHANGELOG and D-09. |
| `ts_bootstrap_intervals` | `BootstrapPredictor::fit` (Rust) | FFI scalar call | ✓ WIRED | Live query confirms corrected sign (100.0/110.0, not 90.0/100.0). |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|---|---|---|---|---|
| RUST-04 | 16-01 | date/step pairing, Rust-computed residuals/coverage, per-group calibration | ✓ SATISFIED | Live queries + 132 SQL assertions + CHANGELOG. |
| CONF-01 | 16-03 | Bootstrap correct sign | ✓ SATISFIED | Live query + Rust tests. |
| CONF-02 | 16-02 | Split conformal insufficiency/alpha/NaN handling | ✓ SATISFIED | Live query + SQL/Rust tests. |
| CONF-03 | 16-02 | Asymmetric signed-residual quantiles, ±2pp coverage | ✓ SATISFIED | Rust Monte Carlo test passes live. |
| CONF-04 | 16-02 | Adaptive \|r\|/d, batch independence | ✓ SATISFIED | Rust batch-independence test + SQL d-scaled half-widths. |
| CONF-05 | 16-03 | JackknifePlus removed with clear error | ✓ SATISFIED | Live query reproduces exact error. |

No orphaned requirements found for Phase 16 in `.planning/REQUIREMENTS.md` (RUST-04, CONF-01..05 all mapped to plans 16-01/02/03; the REQUIREMENTS.md status column still reads "Pending" — a tracking-table field outside this verifier's scope to edit, not evidence of incompleteness given the above).

### Anti-Patterns Found

None. `grep` for `TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER|not yet implemented|coming soon` across all phase-modified Rust/C++ files returned no matches. `cargo fmt --all --check` is clean. `git status` shows no uncommitted changes under the phase's touched paths.

### Behavioral Spot-Checks / Probe Execution

| Behavior | Command | Result | Status |
|---|---|---|---|
| Non-gated SQL reference suite | `build/release/test/unittest test/sql/ts_conformal_reference.test` | 132/132 assertions pass | ✓ PASS |
| Null-policy SQL suite | `build/release/test/unittest test/sql/ts_conformal_null_policy.test` | 17/17 assertions pass | ✓ PASS |
| Rust conformal reference suite | `cargo test --release -p anofox-fcst-core --test conformal_reference` | 22/22 tests pass | ✓ PASS |
| Bootstrap sign (live) | `ts_bootstrap_intervals([0]*9+[10],[100],2000,0.9,42)` | `lower=100.0, upper=110.0` | ✓ PASS |
| JackknifePlus removal (live) | `ts_conformal_learn(..., 'symmetric', 'jackknife+')` | exact documented error | ✓ PASS |
| Per-group calibration + date pairing (live) | `ts_conformal_by`/`ts_conformal_calibrate` on constructed fixture | one row per (group,date); insufficient-calibration NULLs as expected | ✓ PASS |
| Build freshness | `find crates/.../src src -newer build/release/duckdb` | no newer source files than binary | ✓ confirmed fresh |

No probes (`scripts/*/tests/probe-*.sh`) are declared for this phase; none found by convention search.

### Human Verification Required

None. All ROADMAP success criteria and CONTEXT decisions were verifiable through live execution against the rebuilt extension, live Rust test runs, and direct SQL-test execution.

### Gaps Summary

None. All 5 ROADMAP success criteria, both cited user decisions (D-13, D-14), and all 6 requirement IDs (RUST-04, CONF-01..05) are verified with executed evidence (live SQL queries against the rebuilt extension, live `unittest` runs of the non-gated reference/null-policy suites, and a live run of the Rust `conformal_reference` integration test — not SUMMARY claims). The code-review cycle (16-REVIEW.md / 16-REVIEW-FIX.md) independently found and fixed 4 warnings (WR-01..WR-04) in a prior iteration and converged clean; this verification additionally reproduced the jackknife+ error, bootstrap sign, per-group calibration, and insufficient-calibration behaviors directly rather than relying on that report.

---

_Verified: 2026-10-14_
_Verifier: Claude (gsd-verifier)_
