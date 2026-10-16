---
phase: 13-core-statistics-metrics-data-quality
reviewed: 2026-10-11T00:00:00Z
depth: standard
files_reviewed: 8
files_reviewed_list:
  - crates/anofox-fcst-core/src/gaps.rs
  - crates/anofox-fcst-core/src/stats.rs
  - crates/anofox-fcst-core/src/quality.rs
  - crates/anofox-fcst-core/src/calendar.rs
  - src/macros/ts_macros.cpp
  - src/table_functions/ts_fill_gaps_native.cpp
  - src/table_functions/ts_stats.cpp
  - src/table_functions/ts_data_quality.cpp
findings:
  critical: 0
  warning: 1
  info: 0
  total: 1
status: issues_found
---

# Phase 13: Code Review Report (re-review, iteration 3 — final)

**Reviewed:** 2026-10-11T00:00:00Z
**Depth:** standard
**Files Reviewed:** 8
**Status:** issues_found

## Summary

Final re-review of the `--auto` fix loop for the two iteration-2 Critical findings
(CR-02, CR-03), fixed by commits `147f4d4` and `1d01594`. Built the extension at the
current HEAD (`1d01594`, matching the already-built `build/release/` artifacts) and
reproduced every scenario live against `build/release/duckdb -unsigned` with
`LOAD build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`,
rather than trusting the fixer's report.

**CR-02 and CR-03 are both genuinely fixed, reproduced live, no regressions in the
exact review scenarios:**

- **CR-02**: `ts_macros.cpp`'s `ts_stats`, `ts_data_quality`, `ts_data_quality_summary`,
  and `ts_data_quality_by` macros now add `FILTER (WHERE date_col IS NOT NULL)` to
  both the paired `LIST(value_col ...)` and `LIST(date_col ...)` aggregates. Verified
  the two lists stay aligned under scrambled insertion order (a hand-built table with
  one NULL-dated row interleaved out of sorted order; the surviving 5 values/dates
  paired correctly, `mean=3.0`/`min=1.0`/`max=5.0` as expected). Reproduced the
  review's exact 10-row/1-NULL-date table: `ts_stats_by`, `ts_stats`, `ts_data_quality`,
  and `ts_data_quality_by` all now agree (`length=9, n_gaps=1, n_gap_runs=1`), none
  throw. The defensive `ExtractListTimestamps` throw is correctly left in place for a
  hand-built literal list.
- **CR-03**: `calendar_grid_gap_points` is now the single shared grid-walk used by
  both `fill_gaps_calendar` and `step_count`'s `Months` branch. Reproduced both of the
  review's exact divergent cases live: `'1q'` over a 5-month gap (`q3` table) —
  `ts_fill_gaps_by` inserts 1 NULL row, `ts_stats_by` reports `n_gaps=1` (previously
  0/1 mismatch); `'3q'` over 12 months (`tq` table) — both report 1 missing period
  (previously 1/0 mismatch), and `ts_data_quality_by` agrees too. Additionally
  verified: a month-end `'1q'` series (Jan 31 → Apr 30, skipping Feb) — both sides
  agree (0 gaps); an off-grid `'2mo'` series (Jan 1 → Mar 1 exact → Jun 1 off-grid) —
  both sides agree (1 gap); duplicate-timestamp handling in `ts_stats_by` (`gap_counts`
  dedups dates, so a duplicate-date row contributes 0 extra gaps, `expected_length`
  reflects distinct dates only — consistent, not a regression); a single-point series
  (0 gaps, `expected_length=1`); and a 9998-year span at `'1mo'` resolution (119,975
  grid points) completed in 30ms — no correctness or practical performance issue in
  the shared grid walk. `step_count`'s new `Result` return threads through `gap_counts`
  → `compute_ts_stats_with_dates_and_type`/`compute_data_quality` via `?` with no FFI
  signature change (both functions already returned `Result` before this fix, confirmed
  by diff), so no new unhandled-error surface was introduced at the FFI boundary.

**One new defect surfaced by this review, introduced by CR-02's fix** (reproduced live)
— see Warning below.

## Warnings

### WR-01: CR-02's NULL-date `FILTER` turns an all-NULL-dated group into a silently
dropped (not scored) row, and `ts_data_quality_summary` then miscounts it — bucket
totals no longer sum to `n_total`, and the worst-quality series in a table is silently
excluded from the quality report `ts_data_quality` exists to produce

**File:** `src/macros/ts_macros.cpp:170-197` (`ts_data_quality_summary`'s
`n_good`/`n_fair`/`n_poor`/`avg_score` aggregation over `(_quality).overall_score`),
root-caused by the `FILTER (WHERE date_col IS NOT NULL)` added to the same macro (and
`ts_stats`/`ts_data_quality`/`ts_data_quality_by`) in commit `147f4d4`

**Issue:** CR-02's fix correctly handles a group with *some* NULL-dated rows (they are
dropped, the rest of the group is scored normally — verified above). But when *every*
row in a group has a NULL `date_col` — a realistic case (e.g. an upstream ETL emitting
a placeholder/dateless row for a given key, or a key whose date column failed to parse
entirely) — `FILTER (WHERE date_col IS NOT NULL)` removes every row from both the
value and date `LIST(...)` aggregates for that group. DuckDB's `LIST()` aggregate over
zero filtered-in rows returns `NULL` (not an empty list), so `_ts_data_quality` receives
a NULL list argument and (correctly, per its existing NULL-propagation contract) returns
a NULL quality struct for the whole row instead of throwing. Reproduced live:

```sql
CREATE TABLE mixed2 AS
SELECT * FROM (VALUES
  ('bad', NULL::TIMESTAMP, 1.0), ('bad', NULL::TIMESTAMP, 2.0),
  ('good', '2023-01-01'::TIMESTAMP, 10.0), ('good', '2023-01-02'::TIMESTAMP, 11.0),
  ('good', '2023-01-03'::TIMESTAMP, 12.0)
) t(id, ds, v);

SELECT * FROM ts_data_quality_summary('mixed2', id, ds, v, 1);
-- n_total=2 | n_good=1 | n_fair=0 | n_poor=0 | avg_score=1.0
```

The `'bad'` group — every single row lacking a timestamp, the worst possible input for
a date-aware quality function — is counted in `n_total` (2) but appears in none of
`n_good`/`n_fair`/`n_poor` (1+0+0=1, not 2) and is silently excluded from `avg_score`
(DuckDB's `AVG` ignores NULL, so `avg_score=1.0` reflects only the `'good'` group). A
caller reading this summary sees "100% average quality, 1 of 2 series good" with no
indication that the other series was entirely unassessable — the exact opposite of
CR-02's own stated goal ("`ts_data_quality` ... exists to assess the quality of
imperfect data; a single malformed-timestamp row ... is exactly the kind of defect the
function is meant to surface as a score", from the CR-02 finding this fix addresses).

This is a regression in failure visibility, not just a missed edge case: **before**
`147f4d4`, this exact input (a group with every date NULL) hit the same
`ExtractListTimestamps` NULL-element throw CR-02 was filed against, so the whole query
failed loudly. **After** the fix, it fails silently and produces a numerically
self-inconsistent, misleadingly rosy report. `ts_data_quality`/`ts_data_quality_by`
(the non-summary forms) show the same root cause directly — the `'bad'`-equivalent
group's row is present but every field is NULL:

```sql
SELECT * FROM ts_data_quality('allnull_table', id, date, val, 3, '1d');
-- structural_score=NULL, temporal_score=NULL, ..., overall_score=NULL, is_short=NULL
```

Separately (not the primary finding, but worth noting for consistency): `ts_stats_by`
handles the identical all-NULL-dated group differently again — it drops the group
entirely (0 rows emitted), rather than a NULL row. So the extension now has three
different behaviors for the same "group with zero valid dates" input depending on
which entry point is used: silently vanish (`ts_stats_by`), emit a NULL row (`ts_stats`/
`ts_data_quality`/`ts_data_quality_by`), or emit a NULL row that then corrupts bucket
totals downstream (`ts_data_quality_summary`).

**Fix:** In `ts_data_quality_summary` (and ideally `ts_quality_report`, which has the
same `SUM(CASE WHEN ... THEN 1 ELSE 0 END)` shape over a stats table), make the buckets
exhaustive by adding an explicit "unassessed" bucket, or treat a NULL `overall_score` as
the worst score rather than excluding it:

```sql
SUM(CASE WHEN (_quality) IS NULL THEN 1 ELSE 0 END) AS n_unassessed,
SUM(CASE WHEN (_quality).overall_score >= 0.8 THEN 1 ELSE 0 END) AS n_good,
SUM(CASE WHEN (_quality).overall_score >= 0.5 AND (_quality).overall_score < 0.8 THEN 1 ELSE 0 END) AS n_fair,
SUM(CASE WHEN (_quality).overall_score < 0.5 THEN 1 ELSE 0 END) AS n_poor,
AVG((_quality).overall_score) AS avg_score
```

so `n_good + n_fair + n_poor + n_unassessed == n_total` always holds, and a caller can
tell "0 good, 0 fair, 0 poor, 1 unassessed" apart from "this series simply didn't
exist." Add a regression test with a table containing one all-NULL-dated group mixed
with one normal group, asserting the bucket counts sum to `n_total`.

---

_Reviewed: 2026-10-11T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
