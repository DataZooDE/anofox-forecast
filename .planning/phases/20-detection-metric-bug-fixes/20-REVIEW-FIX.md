---
phase: 20-detection-metric-bug-fixes
fixed_at: 2026-10-23T22:56:02Z
review_path: .planning/phases/20-detection-metric-bug-fixes/20-REVIEW.md
iteration: 1
findings_in_scope: 7
fixed: 6
skipped: 1
status: partial
---

# Phase 20: Code Review Fix Report

**Fixed at:** 2026-10-23T22:56:02Z
**Source review:** `.planning/phases/20-detection-metric-bug-fixes/20-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope (critical + warning): 7
- Fixed: 6
- Skipped: 1 (CR-01, binding user decision: won't fix)

All fixes applied the binding `## User Decisions` section of REVIEW.md, which overrides each
finding's own suggested fix where present (CR-01, CR-02, WR-03).

## Fixed Issues

### CR-02: `deseasonalize: true` without `threshold` makes changepoint detection materially worse

**Files modified:** `crates/anofox-fcst-core/src/changepoint.rs`,
`test/sql/correctness/detection_decomposition.test`, `docs/api/06-changepoint-detection.md`,
`plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md`, `CHANGELOG.md`,
`examples/changepoint_detection/synthetic_changepoint_examples.sql`
**Commit:** `59b2b86`
**Applied fix:** Implemented the user-decided "default threshold" option entirely in Rust
(`detect_changepoints_bocpd_with_options`): when `opts.deseasonalize == true` and
`opts.threshold.is_none()`, the *effective* threshold used for the `is_changepoint` decision
now defaults to `0.5` instead of falling back to the MAP run-length-reset heuristic. An
explicit `opts.threshold` always wins. `opts.deseasonalize == false` is untouched, so default
outputs stay byte-identical (verified — see Verification below). Added two Rust unit tests
(`test_deseasonalize_without_threshold_defaults_to_0_5_cr02` on a noisy weekly sine, amplitude
5, + level shift of +8 at i=90, n=180, confirming `deseasonalize` alone now matches
`deseasonalize + threshold=0.5` exactly and flags only the true shift;
`test_deseasonalize_explicit_threshold_wins_over_default_cr02` confirming an explicit
`threshold: Some(0.0)` overrides the 0.5 default). Updated the pinned SQL block in
`detection_decomposition.test` (lines ~420-438) that previously asserted the old "weekly
troughs flagged" behavior to now assert `deseasonalize: 'true'` alone equals
`deseasonalize: 'true', threshold: '0.5'` on the #287 panel. Updated
`docs/api/06-changepoint-detection.md`'s `deseasonalize` param description and the "Ignoring
Seasonal Cycles" worked example to state the 0.5 default and removed the stale "can still flag
weekly-spaced points" caveat. Updated the detection SKILL.md's params table and example
comment the same way. Updated the CHANGELOG `[Unreleased]` entry (additive — the option
itself is unreleased). Updated `examples/changepoint_detection/synthetic_changepoint_examples.sql`
Section 7 comments/print labels so Section 7.2 (deseasonalize alone) is described as matching
7.3/7.4 exactly rather than having "extra weekly-spaced flags."

### WR-01: New FFI export doesn't check `malloc()` for null before writing through it

**Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`
**Commit:** `88cc022`
**Applied fix:** Added null checks for all three `malloc()` calls in
`anofox_ts_detect_changepoints_bocpd_opts` (`is_cp_ptr`, `prob_ptr`, `idx_ptr`), each setting
`ErrorCode::AllocationError` and returning `false` on failure instead of dereferencing a null
pointer. Each failure path also frees and nulls out any buffers already allocated earlier in
the same call, matching the existing `checked_malloc`-style pattern already used elsewhere in
this file (e.g. the MSTL decomposition FFI export) rather than leaking on a later allocation
failure.

### WR-02: Asymmetric null-guard on sibling fields from the same `BocpdResult`

**Files modified:** `src/aggregate_functions/ts_changepoints_agg.cpp`
**Commit:** `03c81af`
**Applied fix:** `cp_child_data[current_size + j] = cp_result.is_changepoint[j];` is now guarded
the same way its sibling `prob_child_data` assignment already was:
`cp_result.is_changepoint ? cp_result.is_changepoint[j] : false`.

### WR-04: Key-name typos silently defeat the "typo'd value must not silently disable the feature" protection

**Files modified:** `src/include/ts_changepoints_params.hpp`
**Commit:** `f3859a6`
**Applied fix:** No runtime-behavior change (rejecting unrecognized keys would be a breaking
change for any caller currently passing an extra, intentionally-ignored key — in tension with
the project's additive-only SQL API compatibility rule). Added an explicit doc comment at the
top of the header documenting this as a known, deliberate tradeoff between unknown-key
leniency (kept) and the value-level strictness on `threshold`/`deseasonalize`/`seasonal_period`
(unchanged), and stating what a future key-typo-protection change would require (validate every
input key against the four recognized names, as its own explicit decision).

### WR-05: `ParseChangepointParamSet`'s STRUCT branch appears unreachable from either declared call site

**Files modified:** `src/include/ts_changepoints_params.hpp`
**Commit:** `64c6935`
**Applied fix:** Confirmed via static analysis that the STRUCT branch was genuinely
unreachable: both declared call sites (`_ts_detect_changepoints_by_native`'s `TableFunction`
signature and `ts_detect_changepoints_agg`'s `AggregateFunction` signature) type the `params`
argument as exactly `MAP(VARCHAR, VARCHAR)`, not `LogicalType::ANY` (unlike the sibling
`ts_forecast_params.hpp`/`ts_forecast_scalar.cpp`, which does declare `LogicalType::ANY` and
therefore genuinely needs its own STRUCT branch) — so DuckDB's binder never lets a bare STRUCT
value reach this parser. Removed the dead `else if (params_value.type().id() ==
LogicalTypeId::STRUCT)` branch from `read_key` and updated the function's doc comment to
explain why and point at what reviving STRUCT support would require (widening both declared
signatures to `LogicalType::ANY`).

### WR-03: `TsCollectFinalizeBarrier` cross-line verification deferral

**Files modified:** `.planning/phases/20-detection-metric-bug-fixes/deferred-items.md`
**Commit:** `a11325c`
**Applied fix:** Per the binding user decision, no code change. Recorded the finding in
deferred-items.md: cross-line verification of `TsCollectFinalizeBarrier` on the v1.4.5/v1.5.6
DuckDB lines happens through the PR CI run (which builds and tests all three lines) rather than
a dedicated stress test added in this fix pass; flagged for re-opening if that CI run surfaces a
silent empty-result regression on either LTS line.

## Skipped Issues

### CR-01: `ts_detect_amplitude_modulation` can leak raw NaN into a DOUBLE column instead of SQL NULL

**File:** `src/table_functions/ts_detrend.cpp:839`
**Reason:** Binding user decision in REVIEW.md ("## User Decisions"): **WON'T FIX**. This is
the deliberate 20-01 decision that `ts_detect_amplitude_modulation` keeps returning NaN for a
degenerate `seasonal_strength`. No change made to `src/table_functions/ts_detrend.cpp`.
**Original issue:** The function's own doc comment says NaN maps to SQL NULL at the FFI/C++
boundary, but `TsDetectAmplitudeModulationFunction` writes the raw FFI double through
unguarded, unlike its two sibling call sites (which reuse a different field and are therefore
unaffected). Per the binding decision this is intentional and out of scope for this fix pass.

## Out of Scope

**IN-01** (Info severity) was not addressed — `fix_scope` for this run is `critical_warning`,
which excludes Info-tier findings.

## Verification

All verification ran in the **main checkout** (`/home/simonm/projects/duckdb/anofox-forecast`,
branch `milestone/v0.11.0-issue-sweep`) — `workflow.use_worktrees` is `false` in this project's
`.planning/config.json`, so no isolated worktree was created for this run; every edit, build,
and test below is reproducible directly from this tree at the commits listed.

- `cargo fmt --all -- --check`: clean (ran after each Rust edit; `cargo fmt --all` applied
  once to fix formatting before committing CR-02 and WR-01).
- `cargo test --workspace`: full workspace suite passed, exit code 0 (includes the two new
  `changepoint::tests::test_deseasonalize_*_cr02` tests and all 32
  pre-existing+new `changepoint::tests::*` tests).
- `DUCKDB_VERSION=v2.0.0 make release`: built clean, exit code 0 (two full rebuilds were run —
  one before the WR-01/WR-02/WR-04/WR-05 C++/FFI edits landed, and one comprehensive rebuild
  afterward incorporating every commit through `a11325c`; the second is the one the sqllogictest
  and example runs below used).
- `test/sql/correctness/detection_decomposition.test`: **140/140 assertions passed** via
  `./build/release/test/unittest`, confirming the pre-existing md5 pins (default,
  `deseasonalize` unset/`false`) stayed byte-identical and the updated `deseasonalize`-alone
  block now matches `deseasonalize+threshold=0.5` exactly.
- `test/sql/correctness/union_source.test`: **322/322 assertions passed**.
- `test/sql/ts_changepoints.test`: **79/79 assertions passed**.
- `make check-docs` (`scripts/check_doc_snippets.py`): **347 PASS, 0 FAIL, 8 EXPECTED-ERROR,
  155 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE** against the freshly built extension.
- Ran `examples/changepoint_detection/synthetic_changepoint_examples.sql` end-to-end against
  `build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`: Section 7.2
  (`deseasonalize` alone) now returns identical rows to 7.3 (`deseasonalize`+`threshold=0.5`)
  and 7.4 (aggregate form) — `A: NULL, B: 2024-02-20, C: NULL` in all three — confirming the
  CR-02 fix end-to-end on the built extension, not just in the Rust unit tests.
- `git push origin milestone/v0.11.0-issue-sweep`: pushed, `b8d0fb0..a11325c`.

## Commits

| Finding | Commit | Status |
|---|---|---|
| CR-01 | — | skipped (won't fix, binding user decision) |
| CR-02 | `59b2b86` | fixed |
| WR-01 | `88cc022` | fixed |
| WR-02 | `03c81af` | fixed |
| WR-03 | `a11325c` | fixed (docs-only, no code change, per binding user decision) |
| WR-04 | `f3859a6` | fixed (docs-only, no behavior change) |
| WR-05 | `64c6935` | fixed |

---

_Fixed: 2026-10-23T22:56:02Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
