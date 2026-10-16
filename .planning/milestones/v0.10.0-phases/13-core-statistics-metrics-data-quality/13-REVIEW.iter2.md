---
phase: 13-core-statistics-metrics-data-quality
reviewed: 2026-10-10T23:06:02Z
depth: standard
files_reviewed: 15
files_reviewed_list:
  - crates/anofox-fcst-core/src/gaps.rs
  - crates/anofox-fcst-core/src/quality.rs
  - crates/anofox-fcst-core/src/stats.rs
  - crates/anofox-fcst-core/src/stl.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - src/include/anofox_fcst_ffi.h
  - src/include/ts_fill_gaps_native.hpp
  - src/aggregate_functions/ts_data_quality_agg.cpp
  - src/aggregate_functions/ts_stats_agg.cpp
  - src/table_functions/ts_data_quality.cpp
  - src/table_functions/ts_fill_forward_native.cpp
  - src/table_functions/ts_fill_gaps_native.cpp
  - src/table_functions/ts_stats.cpp
  - src/macros/ts_macros.cpp
findings:
  critical: 2
  warning: 0
  info: 0
  total: 2
status: issues_found
---

# Phase 13: Code Review Report (re-review, iteration 2)

**Reviewed:** 2026-10-10T23:06:02Z
**Depth:** standard
**Files Reviewed:** 15
**Status:** issues_found

## Summary

Re-reviewed iteration 1's 5 in-scope findings (CR-01, WR-01, WR-02, WR-03, WR-04) against
the 4 fix commits (`f7e19c1`, `9c41958`, `15f7d63`, `3f40207`), reading both the diffs and
the resulting source, and then built and loaded the extension to reproduce every scenario
directly against SQL rather than trusting the fixer's test-suite summary.

**Verified correctly fixed, mechanically sound, no regressions found:**

- **WR-02** (duplicated frequency parser): cleanly consolidated into
  `ParseFrequencyToMicrosAndType()` in `ts_fill_gaps_native.hpp/.cpp`; both `ts_stats.cpp`
  and `ts_data_quality.cpp` now call the single helper.
- **WR-03** (entropy `0.0`/NaN sentinel collision): `types.rs`'s `From<TsStats>` now uses
  `f64::NAN` for `entropy`'s `None` case, matching `trend_strength`/`seasonality_strength`;
  verified by reading the diff and the new round-trip tests.
- **WR-04** (STL scratch-buffer aliasing): `stlss` now gets its own dedicated 6th scratch
  buffer (`work` grown from `5*n2p` to `6*n2p`) instead of aliasing `stlstp`'s `season`
  output parameter; the aliasing invariant is fully removed, not just asserted. Diff is
  clean and value-neutral per the unchanged `stl_reference.rs` fixture tests.
- **CR-01's own stated scope** (the declared calendar multiplier for Monthly/Quarterly/
  Yearly was previously hard-coded to 1): the multiplier itself is now correctly threaded
  end-to-end — Rust core (`gaps::fill_gaps`/`fill_forward`/`resolve_gap_step`,
  `quality::compute_data_quality`, `stats::compute_ts_stats_with_dates_and_type`) → FFI
  (`anofox_ts_fill_gaps`, `anofox_ts_fill_forward_dates`,
  `anofox_ts_stats_with_dates_and_type`, `anofox_ts_data_quality`, all with a `size_t
  period_count` parameter, header regenerated and ABI-consistent) → C++ callers. Every
  caller was checked: `ts_fill_gaps_native.cpp`/`ts_fill_forward_native.cpp` pass
  `bind_data.frequency_seconds` (the raw declared multiplier from
  `ParseFrequencyWithType`); `ts_stats.cpp` (both the `_ts_stats_with_dates` scalar and
  `_ts_stats_by_native` table function) and `ts_data_quality.cpp` pass
  `ParseFrequencyToMicrosAndType(...).period_count`; `ts_stats_agg.cpp`/
  `ts_data_quality_agg.cpp` correctly pass a literal `1` (always `FrequencyType::FIXED`,
  for which `period_count` is ignored on the Rust side). Confirmed `'2mo'`/`'3q'`/`'5y'`
  produce the documented, correct multiplier-aware `n_gaps` via
  `test/sql/ts_data_quality_reference.test` (re-ran directly: 47/47 assertions pass against
  the current build).
- **WR-01's literal scope** (a NULL element inside an otherwise non-NULL dates list no
  longer silently becomes epoch 0): confirmed via direct SQL — `_ts_stats_with_dates` and
  `_ts_data_quality` now throw `InvalidInputException` naming the row/position for a
  hand-built array containing a NULL timestamp, instead of corrupting the gap count.

**Two new/newly-surfaced correctness defects found** (both reproduced live against the
built extension, not just read from source) — see Critical Issues below. Both sit squarely
in the area this re-review was asked to focus on: the realistic, GROUP BY-driven call paths
for the dates-aware functions, and the rounding/stepping behavior for calendar frequencies.

## Critical Issues

### CR-02: WR-01's NULL-dates-list fix turns one bad row into a hard failure for the entire `ts_stats()`/`ts_data_quality()`/`ts_data_quality_by()` call, inconsistent with every other NULL-date code path in the extension

**File:** `src/table_functions/ts_stats.cpp:129-164` (`ExtractListTimestamps`), `src/table_functions/ts_data_quality.cpp:50-85` (same function, duplicated), both reached via the `LIST(date_col ORDER BY date_col)` aggregation in `src/macros/ts_macros.cpp` (`ts_stats`, `ts_data_quality`, `ts_data_quality_summary`, `ts_data_quality_by`)

**Issue:** WR-01's fix (commit `9c41958`) makes `ExtractListTimestamps` throw
`InvalidInputException` when it finds a NULL element inside an otherwise non-NULL `LIST(
TIMESTAMP)`. That list is built by the `ts_stats`/`ts_data_quality`/`ts_data_quality_by`
macros via `LIST(date_col::TIMESTAMP ORDER BY date_col) ... GROUP BY unique_id_col`
(`ts_macros.cpp:36-38`, `135-136`, `170-171`). DuckDB's `LIST()` aggregate **includes** NULL
elements by default (verified directly: `SELECT list(x) FROM (VALUES (1),(NULL),(3)) t(x)`
→ `[1, 3, NULL]`). So a single row with a NULL `date_col` — a completely realistic and
otherwise-handled input shape (a late-arriving/malformed timestamp mixed into an otherwise
clean series) — now makes the **entire scalar call for that group** throw, which **aborts
the whole query** (DuckDB cannot catch a scalar-function exception per-row). Reproduced
live:

```sql
CREATE TABLE t AS
SELECT 'A' AS id,
       CASE WHEN i = 5 THEN NULL ELSE ('2023-01-01'::DATE + (i||' day')::INTERVAL)::TIMESTAMP END AS date,
       (i+1)::DOUBLE AS val
FROM generate_series(0, 9) AS g(i);

SELECT * FROM ts_data_quality('t', id, date, val, 3, '1d');
-- Invalid Input Error: ts_data_quality: dates list for row 0 contains a NULL element at
-- position 9; every element of a declared dates list must be non-NULL ...
SELECT * FROM ts_stats('t', id, date, val, '1d');
-- same error, different function name
```

This is a regression relative to every *other* NULL-date code path in the extension, which
all treat a NULL-dated row as "drop this one row, keep going":

- `_ts_stats_by_native`'s `TsStatsByInOut` (`ts_stats.cpp:625`): `if (date_val.IsNull())
  continue;`
- `_ts_fill_gaps_native`/`_ts_fill_forward_native`'s `InOut` functions: same
  `if (date_val.IsNull()) continue;` pattern
- `ts_stats_agg`/`ts_data_quality_agg`'s `Update` functions: `if (!ts_data.validity.
  RowIsValid(ts_idx)) continue;`

Reproduced the inconsistency directly — same table, same one bad row:

```sql
SELECT id, length, n_gaps FROM ts_stats_by('t', id, date, val, '1d');
-- A | 9 | 1   -- succeeds, silently drops the bad row (length 9, not 10)

SELECT * FROM ts_stats('t', id, date, val, '1d');
-- Invalid Input Error: ... contains a NULL element at position 9 ...   -- aborts entirely

SELECT * FROM ts_data_quality_by('t', id, date, val, 3, '1d');
-- Invalid Input Error: ... (same scalar function under the hood as ts_data_quality)
```

`ts_data_quality` in particular exists to assess the quality of imperfect data; a single
malformed-timestamp row — exactly the kind of defect the function is meant to surface as a
score — now makes the whole call fail instead of producing a (lower) quality score. No test
added in iteration 1 exercises the macro-level GROUP BY + `LIST(date_col ...)` path with a
NULL date mixed into an otherwise valid group (the added tests only pass a hand-built
literal array containing a NULL directly to `_ts_stats_with_dates`/`_ts_data_quality`),
which is why this was not caught despite "all tests passed."

**Fix:** Either (a) make `ExtractListTimestamps` exclude the NULL-dated element (and its
paired value) from the series instead of throwing, matching `ts_stats_by`'s/the aggregate
functions' "drop this row" semantics, or (b) if the hard-error behavior is intentional for
the scalar entry points, filter NULL dates out of the `LIST(...)` aggregation in the macros
themselves (e.g. `LIST(date_col ORDER BY date_col) FILTER (date_col IS NOT NULL)`, pairing
the value list with the same filter) so a NULL-dated row never reaches
`ExtractListTimestamps` in the first place — but then also drop the matching value, and
document that this makes `ts_stats`/`ts_data_quality` silently exclude NULL-dated rows just
like `ts_stats_by` does, for consistency. Either way, add a regression test that runs the
*macro* (not the underlying scalar function directly) over a table with one NULL-dated row
mixed into an otherwise valid group, and assert it does not throw.

### CR-03: `fill_gaps`/`fill_forward` and `gap_counts`/`resolve_gap_step` disagree about how many periods are missing for the same calendar-frequency series — the two "shared gap counter" consumers are not actually consistent

**File:** `crates/anofox-fcst-core/src/gaps.rs` — `fill_gaps_calendar` (lines 154-197,
quarter/month/year-*index*-based grid walk anchored on the previous observed date) vs
`step_count` (lines 397-423, month-*difference*-based round-to-nearest-whole-step), both
reachable via `FrequencyType::Monthly/Quarterly/Yearly` from `ts_fill_gaps_by`/
`ts_fill_forward_by` (the first) and `ts_stats_by`/`ts_data_quality_by`/`ts_stats`/
`ts_data_quality` (the second)

**Issue:** The doc comments across `gaps.rs` describe `gap_counts`/`resolve_gap_step` as
"the shared missing-period gap counter" that `ts_fill_gaps`/`ts_fill_forward` and
`ts_stats`/`ts_data_quality` are both built on (D-04). In fact they use two structurally
different algorithms for calendar (Monthly/Quarterly/Yearly) frequencies:

- `fill_gaps_calendar` measures the gap between two observed dates in **period-granularity
  index units** (`quarter_index`/`month_index`/year, via `idx_diff`) and walks a grid
  anchored at the *previous observed date*, inserting a point at every `period_count`
  multiple strictly between `0` and `idx_diff` — i.e. it never rounds; it reports exactly
  how many full periods fit in the gap.
- `step_count` (used by `gap_counts`/`resolve_gap_step`, i.e. `n_gaps`/`expected_length` for
  `ts_stats`/`ts_data_quality`) measures the gap in exact **months**
  (`month_index` difference) and divides by the total months-per-step `m`, **rounding to
  the nearest whole step** (round-half-away-from-zero), then reports `k - 1` missing
  periods.

These disagree whenever the elapsed distance is not a clean multiple of the step and the
remainder is large enough to round away. Reproduced live, same series, same declared
frequency, period_count defaulted to 1 (**this reproduces even without the CR-01 multiplier
— it is not new to this phase's multiplier fix, but the fix neither caught nor fixed it,
and the multiplier makes it worse**):

```sql
CREATE TABLE q3 AS SELECT 'q3' AS id, ds::TIMESTAMP AS ds, 100.0 AS v
FROM (VALUES ('2023-01-01'), ('2023-06-01')) AS t(ds);  -- 5 months apart

SELECT * FROM ts_fill_gaps_by('q3', id, ds, v, '1q');
-- Jan, Jun only -- fill_gaps_calendar inserts NOTHING (idx_diff in quarters = 1, not > period_count=1)

SELECT n_gaps, expected_length FROM ts_stats_by('q3', id, ds, v, '1q');
-- n_gaps = 1, expected_length = 3 -- gap_counts says ONE period is missing
```

With the CR-01 multiplier in play, the same divergence reproduces more sharply:

```sql
CREATE TABLE tq AS SELECT 'tq' AS id, ds::TIMESTAMP AS ds, 100.0 AS v
FROM (VALUES ('2023-01-01'), ('2024-01-01')) AS t(ds);  -- 12 months apart, '3q' = 9-month step

SELECT * FROM ts_fill_gaps_by('tq', id, ds, v, '3q');
-- inserts ONE NULL row at 2023-10-01 (fill_gaps says: 1 missing period)

SELECT n_gaps FROM ts_stats_by('tq', id, ds, v, '3q');    -- 0
SELECT n_gaps FROM ts_data_quality_by('tq', id, ds, v, 1, '3q');  -- 0
-- gap_counts says: 0 missing periods, for the IDENTICAL series and frequency
```

So for the same input, `ts_fill_gaps_by` materializes a NULL row claiming a period is
missing, while `ts_stats_by`/`ts_data_quality_by` report `n_gaps = 0` ("no gaps") for that
same pair of observations. Both cannot be right simultaneously, and a user who runs both
tools (a very natural workflow — e.g. `ts_fill_gaps_by` to materialize a gapped series, then
`ts_stats_by` to characterize it) gets contradictory signals about whether their data has a
gap. The fixer's own iteration-1 report flags "the rounding semantics I chose for
non-exact-multiple gaps... was not explicitly specified... a human should confirm this
rounding convention is the intended one" for `step_count`'s rounding rule in isolation, but
does not note that this rounding rule actively *disagrees* with `fill_gaps_calendar`'s
non-rounding grid walk for the same input — no test in either `gaps.rs` or the SQL suite
cross-checks `fill_gaps`'s inserted-row count against `gap_counts`'s `n_gaps` for a
non-exact-multiple calendar gap, which is why this was not caught.

**Fix:** Pick one semantic and make both algorithms implement it. The simplest unification:
have `gap_counts`/`step_count`'s `Months(m)` branch reuse the exact same grid-walk logic
`fill_gaps_calendar` already has (walk from the previous date in `m`-month increments,
counting how many grid points fall strictly before the next observed date) instead of
rounding the total elapsed distance — this also sidesteps the "which rounding rule" question
entirely, since `fill_gaps` only ever inserts a point when it actually fits inside the
gap, never based on how close the total elapsed time is to a round number of steps. Add a
property-style regression test that asserts, for a representative set of calendar gaps
(exact multiple, just-under, just-over, with `period_count` ∈ {1, 2, 3}),
`gap_counts(dates, step).n_missing_periods == fill_gaps(dates, ..., step's frequency_type,
period_count).1.iter().filter(|v| v.is_none()).count()` for every consecutive pair.

---

_Reviewed: 2026-10-10T23:06:02Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
