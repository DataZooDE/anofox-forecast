---
phase: 13-core-statistics-metrics-data-quality
fixed_at: 2026-10-11T00:55:00Z
review_path: .planning/phases/13-core-statistics-metrics-data-quality/13-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 13: Code Review Fix Report

**Fixed at:** 2026-10-11T00:55:00Z
**Source review:** .planning/phases/13-core-statistics-metrics-data-quality/13-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope (fix_scope = critical_warning, i.e. CR-* and WR-*): 5
- Fixed: 5
- Skipped: 0
- Out of scope (not attempted): IN-01 (Info-tier; `fix_scope` excludes Info findings by default)

**Verification environment:** main checkout (no worktree — `workflow.use_worktrees`
is `false` in `.planning/config.json`; all edits, builds, and test runs happened
directly on branch `fix/statistical-correctness`). All four commits below are
reproducible from this same tree.

## Fixed Issues

### CR-01: Calendar frequency multiplier silently dropped in gap/temporal scoring

**Files modified:** `crates/anofox-fcst-core/src/gaps.rs`, `crates/anofox-fcst-core/src/stats.rs`,
`crates/anofox-fcst-core/src/quality.rs`, `crates/anofox-fcst-core/tests/stats_reference.rs`,
`crates/anofox-fcst-core/tests/quality_gaps_reference.rs`, `crates/anofox-fcst-ffi/src/lib.rs`,
`src/include/anofox_fcst_ffi.h` (cbindgen-regenerated), `src/include/ts_fill_gaps_native.hpp`,
`src/table_functions/ts_fill_gaps_native.cpp`, `src/table_functions/ts_fill_forward_native.cpp`,
`src/table_functions/ts_stats.cpp`, `src/table_functions/ts_data_quality.cpp`,
`src/aggregate_functions/ts_stats_agg.cpp`, `src/aggregate_functions/ts_data_quality_agg.cpp`,
`test/sql/ts_stats.test`, `test/sql/ts_data_quality_reference.test`,
`.planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md`

**Commit:** `f7e19c1` (combined with WR-02 — see below)

**Status:** fixed: requires human verification (algorithm-correctness finding; see note)

**Applied fix:** The review offered two equally-valid fix shapes: (a) thread an
explicit `period_count` through every FFI signature, or (b) decode the multiplier
back out of the existing approximate `frequency_micros` encoding. Went with (a),
matching the explicit instruction to be "consistent with whatever multiplier
handling the forecast-date path already uses" (`ts_future_dates.hpp`'s
`step_months = freq_seconds * {1,3,12}` pattern passes the real count directly
rather than re-deriving it from a lossy micros encoding).

Threaded a `period_count: u32`/`size_t` parameter end-to-end:
- Rust core: `gaps::fill_gaps`, `gaps::fill_forward`, `gaps::resolve_gap_step` all
  gained the parameter; `CalendarStep::Months(m)` now stores the TOTAL months per
  step (`period_count * {1,3,12}`), not just the base unit.
- **Follow-on fix discovered during verification**: `gaps::step_count` (used by
  `gap_counts`, which backs `resolve_gap_step`'s stats/quality consumers) and
  `gaps::fill_gaps_calendar` (used by `fill_gaps`) both had their own internal
  `match m { 1 => ..., 3 => ..., _ => ... }` dispatch that picked a
  month/quarter/year index function based on the *raw* `m` value — this broke the
  moment `m` became a multiplied total (e.g. `9` for `'3q'` at count 3, which fell
  through to the wrong branch). Fixed by redesigning both to always measure the
  index difference in exact MONTHS (via `month_index`, which works uniformly
  regardless of declared frequency) and then dividing by the total months-per-step
  with the same round-half-away-from-zero rule the `Fixed` branch already uses.
  This was caught by a self-authored SQL regression test failing on first run
  (`'3q'` case), not by the original review — flagging it here as the reason this
  finding needs human sign-off despite the test suite being green.
- FFI boundary: `anofox_ts_fill_gaps`, `anofox_ts_fill_forward_dates`,
  `anofox_ts_stats_with_dates_and_type`, `anofox_ts_data_quality` all gained a
  `period_count` parameter; header regenerated via `cargo build --release`
  (cbindgen), diff is exactly the 4 signature additions.
  `_agg` variants (always Fixed/auto-detect) pass a literal `1`.
  C++ callers (`_ts_fill_gaps_native`, `_ts_fill_forward_native`) pass their
  already-parsed `bind_data.frequency_seconds` (the raw declared multiplier for
  calendar types); `ts_stats.cpp`/`ts_data_quality.cpp` pass the `period_count`
  field of the newly-shared `ParseFrequencyToMicrosAndType` result (see WR-02).
- Marked the matching 13-03 `deferred-items.md` entry RESOLVED.

**Verification:** Rust unit tests in `gaps.rs` (7 new: bimonthly/tri-quarterly
fill_gaps, resolve_gap_step, gap_counts, fill_forward honoring the multiplier) and
`quality.rs` (1 new); integration tests in `stats_reference.rs` and
`quality_gaps_reference.rs` (1 new each); SQL assertions in `test/sql/ts_stats.test`
(`'2mo'`) and `test/sql/ts_data_quality_reference.test` (`'2mo'`/`'3q'`/`'5y'`).
`cargo test -p anofox-fcst-core -p anofox-fcst-ffi`: all passed (`314+1+1+11+10+17+
12+1+2+5+6+4+3+3+32+5+47+36` unit/integration tests across both crates, 0 failed).
`cargo fmt --all --check`: clean. `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`:
succeeded. `make test_release`: **all tests passed (50 skipped [require json], 1795
assertions in 30 test cases)**.

**Why "requires human verification" despite all-green:** this finding is an
algorithm-correctness fix (wrong step-counting logic), not a mechanical change. The
rounding semantics I chose for non-exact-multiple gaps (nearest whole multiple of
the declared step, matching the pre-existing `Fixed`-frequency behavior) is a
reasonable design choice but was not explicitly specified by the review or the
crate's prior behavior for calendar frequencies with a multiplier — a human should
confirm this rounding convention is the intended one before this ships.

### WR-02: Duplicated frequency-parsing helper risks future drift

**Files modified:** `src/include/ts_fill_gaps_native.hpp`,
`src/table_functions/ts_fill_gaps_native.cpp`, `src/table_functions/ts_stats.cpp`,
`src/table_functions/ts_data_quality.cpp` (all included in the CR-01 commit above —
fixing CR-01 required touching both duplicated functions anyway, so the dedup was
inseparable from that change)

**Commit:** `f7e19c1` (same commit as CR-01)

**Status:** fixed

**Applied fix:** Moved the shared logic into a single
`ParseFrequencyToMicrosAndType()` helper in `ts_fill_gaps_native.hpp`/`.cpp` (which
already owns `ParseFrequencyWithType`), returning `{micros, type, period_count}`.
Removed `ts_stats.cpp`'s `ParseFrequencyForStats` and `ts_data_quality.cpp`'s
`ParseFrequencyForDataQuality` (verbatim copies of each other) in favor of the one
shared call.

**Verification:** Same build/test run as CR-01 (`make test_release`: all passed).

### WR-01: NULL date-list elements silently become epoch 0 in gap/quality scoring

**Files modified:** `src/table_functions/ts_data_quality.cpp`,
`src/table_functions/ts_stats.cpp`, `test/sql/ts_data_quality.test`,
`test/sql/ts_stats.test`

**Commit:** `9c41958`

**Status:** fixed

**Applied fix:** `ExtractListTimestamps` (duplicated identically in both files) now
throws `InvalidInputException` naming the offending row and list position when a
NULL element is found *inside* an otherwise non-NULL dates list, instead of silently
writing epoch 0 (which previously looked like a multi-decade gap to
`gap_counts`/`resolve_gap_step`). A NULL dates list itself (the documented "no date
information" case) is unaffected.

**Verification:** Added `statement error` SQL regression tests on both affected
scalars (`_ts_data_quality` and `_ts_stats_with_dates`, the latter reachable via the
`ts_stats(...)` macro) plus a sanity check that a NULL dates list itself still works.
`RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` + `make test_release`: **all tests
passed (50 skipped, 1795 assertions in 30 test cases)**.

### WR-03: Inconsistent "no value" sentinel for `entropy` vs `trend_strength`/`seasonality_strength`

**Files modified:** `crates/anofox-fcst-ffi/src/types.rs`

**Commit:** `15f7d63`

**Status:** fixed

**Applied fix:** Changed `entropy: stats.entropy.unwrap_or(0.0)` to
`entropy: stats.entropy.unwrap_or(f64::NAN)`, matching the two structurally
identical neighboring fields. Not an active bug (all current call sites already
check `has_entropy` before reading the raw field), but removes the landmine for the
next caller.

**Verification:** Added two unit tests in `types.rs`: one asserting all three
`has_*` sentinels round-trip to NaN when `None`, one asserting a genuine
`Some(0.0)` entropy (a constant series) still round-trips as `0.0` and is not
confused with the sentinel. `cargo test -p anofox-fcst-ffi`: both new tests pass;
full crate suite green. No FFI signature change, so no header regen was needed
(confirmed via `git status` on `src/include/anofox_fcst_ffi.h` after a release
rebuild). `make test_release`: all tests passed (50 skipped, 1795 assertions, 30
test cases).

### WR-04: STL port reuses the caller's output buffer as scratch before it is written

**Files modified:** `crates/anofox-fcst-core/src/stl.rs`

**Commit:** `3f40207`

**Status:** fixed

**Applied fix:** Took the review's "more robust" option: gave `stlss` its own
dedicated scratch buffer instead of aliasing `stlstp`'s `season` output parameter.
Grew the shared `work` scratch allocation in `stl()` from `5 * n2p` to `6 * n2p`
and split out a sixth buffer (`buf5`) passed to `stlss` in place of `season`. This
removes the fragile aliasing invariant entirely (previously documented only in a
comment, not enforced by the type system) rather than just asserting it.

**Verification:** `cargo test -p anofox-fcst-core --test stl_reference`: all 3
fixture tests (R `stats::stl` component/strength parity at 1e-8 tolerance) still
pass unchanged, confirming the refactor is value-neutral. Full core-crate suite
green; `cargo fmt --all --check` clean. `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make
release` + `make test_release`: **all tests passed (50 skipped, 1795 assertions, 30
test cases)**.

## Skipped Issues

None — all 5 in-scope findings were fixed.

## Out of Scope

### IN-01: Dead/unreachable `_ts_stats` values-only scalar path

Info-tier finding; `fix_scope` defaults to `critical_warning` (Critical + Warning
only). Not attempted. The review itself recommends "no action required for this
phase" and defers removal to a future cleanup once confirmed unused by any external
binding — consistent with leaving it untouched here.

---

_Fixed: 2026-10-11T00:55:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
