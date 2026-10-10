---
phase: 13-core-statistics-metrics-data-quality
plan: 03
subsystem: statistics
tags: [rust, ffi, duckdb, data-quality, gap-fill, calendar, scipy, pandas]

# Dependency graph
requires:
  - phase: 13-core-statistics-metrics-data-quality
    plan: 01
    provides: gaps::gap_counts/resolve_gap_step shared missing-period gap counter,
      gap_counts/cases.json fixture (incl. fill_cases), Phase 13 snippet harness
  - phase: 13-core-statistics-metrics-data-quality
    plan: 02
    provides: nothing consumed directly (STL/seasonal-strength work is independent of
      data quality / gap fill)
provides:
  - compute_data_quality(values, dates, frequency_micros, frequency_type, n_short) on
    the shared gap counter; new behavioral_score formula (D-06); new n_gap_runs/is_short
    fields
  - _ts_data_quality(values, dates, frequency, n_short) 4-arg scalar; n_gap_runs/is_short
    columns on ts_data_quality/ts_data_quality_by/ts_data_quality_summary/ts_data_quality_agg
  - calendar.rs step_months()/all_month_end() shared by future_dates (forecast) and
    gaps.rs (gap fill/fill-forward) — month-end-preserving monthly/quarterly/yearly fill
  - quality_scores reference-fixture family + gen_quality_reference.py generator
  - tests/quality_gaps_reference.rs (quality_scores + fill_cases + fill_forward assertions)
affects: []

actuals:
  tokens: 39696
  tasks: 3
  commits: 4

plan_head_before: daf1b39964b27552a51eb93698e05caf5eb7d65b6

tech-stack:
  added: []
  patterns:
    - "Behavioral score = constancy + Iglewicz-Hoaglin modified-z-score robust outliers
      + longest-zero-run ratio, computed over 'present' values with a NULL/NaN entry
      explicitly breaking a zero run (the run scan reads the ORIGINAL Option<f64> slice,
      not the filtered present vec, specifically so a gap doesn't silently bridge two
      zero runs into one)."
    - "step_months(base, months, month_end)/all_month_end(dates) extracted from
      future_dates' Months branch into calendar.rs as pub(crate) helpers, reused
      unmodified by gaps.rs's fill_gaps_calendar/fill_forward — one calendar rule, two
      consumers (forecast dates, D-12; gap fill, D-09), instead of two independent
      almost-identical implementations drifting apart."

key-files:
  created:
    - validation/reference/python/gen_quality_reference.py
    - validation/reference/fixtures/quality_scores/cases.json
    - crates/anofox-fcst-core/tests/quality_gaps_reference.rs
    - test/sql/ts_data_quality_reference.test
    - test/sql/ts_fill_gaps_month_end.test
    - .planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-03.tsv
  modified:
    - crates/anofox-fcst-core/src/quality.rs
    - crates/anofox-fcst-core/src/gaps.rs
    - crates/anofox-fcst-core/src/calendar.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/include/anofox_fcst_ffi.h
    - src/table_functions/ts_data_quality.cpp
    - src/aggregate_functions/ts_data_quality_agg.cpp
    - src/macros/ts_macros.cpp
    - test/sql/ts_data_quality.test
    - test/sql/ts_fill_gaps_native.test
    - test/sql/ts_fill_forward_native.test
    - test/sql/extension_comparison.test
    - docs/api/03-statistics.md
    - docs/api/04-data-preparation.md
    - docs/api/22-supported-frequencies.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md
    - examples/data_preparation/synthetic_data_prep_examples.sql
    - CHANGELOG.md
    - validation/reference/README.md
    - .planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md

key-decisions:
  - "n_missing counts NULL and NaN (Phase 12 missing-data policy); every score
    (structural/magnitude/behavioral) is computed on the remaining 'present' values in
    original order, while the zero-run scan inside behavioral_score reads the full
    original Option<f64> slice so a NULL/NaN entry correctly breaks a run."
  - "Temporal score denominator is gap_counts' expected_length (not the raw value-list
    length n) -- expected_length==0 (no dates / unresolvable step) is 'no information',
    scored 1.0, not 'many gaps' scored 0.0."
  - "behavioral_score (D-06) drops the ACF1>0.95 penalty entirely: constant -> 0.0;
    otherwise clamp(1 - 2*outlier_ratio - zero_run_ratio, 0, 1) with Iglewicz-Hoaglin
    modified z-score (MADN=1.482602218505602*median(|x-median|), threshold 3.5,
    outlier_ratio=0 when MADN=0 so intermittent series aren't flagged wholesale) and
    longest-zero-run/n_present."
  - "ts_data_quality_agg fixes Phase 12 deferred item (a) by mirroring ts_stats_agg's
    already-shipped fix exactly: IgnoreNull()=false, keep NULL-valued rows (counted in
    n_missing, position preserved), skip only NULL-timestamp rows. n_short is hardcoded
    to 30 (documented as the pre-v0.10.0 fixed structural scale) since the aggregate
    takes no n_short argument."
  - "step_months/all_month_end extracted from future_dates' Months branch (unchanged
    formulas) into calendar.rs pub(crate) helpers, reused by gaps.rs's
    fill_gaps_calendar (bases from the PREVIOUS OBSERVED DATE, not start-of-period) and
    fill_forward's calendar branches (bases from the LAST OBSERVED DATE). This is
    exactly the D-12 forecast-date rule, now also applied to gap fill (D-09)."
  - "Logged (not fixed) a pre-existing, unrelated bug: ParseFrequencyWithType's numeric
    multiplier for calendar frequencies (e.g. '3mo' meaning 3 months) is discarded by
    every consumer except the forecast-date path (ts_future_dates.hpp), including the
    already-shipped ts_stats.cpp::ParseFrequencyForStats this plan's
    ParseFrequencyForDataQuality deliberately mirrors. Not this plan's scope (D-09 is
    month-end preservation, not the multiplier bug); routed to deferred-items.md."

requirements-completed: [WRAP-02, WRAP-03, STAT-04]

coverage:
  - id: D1
    description: "ts_data_quality_by/ts_data_quality/ts_data_quality_summary pass real
      dates, honour frequency (incl. NULL auto-detect) and n_short; a 60-day daily
      series with a 20-day hole reports n_gaps 20 (missing periods), n_gap_runs 1, and
      temporal_score below 1.0; a declared '2d' frequency changes n_gaps to 10;
      n_short drives structural_score/is_short exactly (0.82 at n_short=100); tied-step
      data gives identical results across 3 repeated runs"
    requirement: WRAP-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/quality.rs tests (test_real_gap_count_and_temporal_below_one, test_frequency_changes_gap_count, test_n_short_drives_structural_score_and_is_short, test_determinism_on_tied_step_data)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_data_quality_reference.test (38 assertions, dq_gap20/dq_gap3/dq_tie/dq_macros blocks)"
        status: pass
    human_judgment: false
  - id: D2
    description: "behavioral_score drops the ACF1>0.95 penalty and scores constancy /
      robust (Iglewicz-Hoaglin) outliers / zero-runs only, pinned against an
      independent numpy/scipy reimplementation across 8 cases"
    requirement: STAT-04
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/quality_gaps_reference.rs#quality_scores_fixture_matches_compute_data_quality"
        status: pass
      - kind: integration
        ref: "test/sql/ts_data_quality_reference.test (smooth-sine behavioral_score=1.0 block); ts_data_quality_agg equals ts_data_quality_by(n_short:=30, frequency:=NULL) on a NULL-containing table"
        status: pass
    human_judgment: false
  - id: D3
    description: "Monthly/quarterly/yearly gap fill and fill-forward preserve
      calendar alignment (month-end stays month-end, 1st-of-month unchanged, other
      days clamped) instead of snapping to the 1st; detect_frequency's microsecond
      unit documented at the Rust/FFI/SQL-doc level"
    requirement: WRAP-03
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/quality_gaps_reference.rs#fill_gaps_fixture_matches_gap_counts_fill_cases, #fill_forward_month_end_matches_calendar_rule"
        status: pass
      - kind: integration
        ref: "test/sql/ts_fill_gaps_month_end.test (53 assertions)"
        status: pass
      - kind: other
        ref: "grep -ci microsecond crates/anofox-fcst-core/src/gaps.rs == 14; docs/api/22-supported-frequencies.md#frequency-auto-detection"
        status: pass
    human_judgment: false
  - id: D4
    description: "Docs/skills/CHANGELOG describe the shipped data-quality and
      gap-fill behaviour with no stale 'fix planned for v0.10.0, Phase 13' note left,
      and no internal phase-number reference in the 5 user-facing doc/skill files"
    verification:
      - kind: other
        ref: ".planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-03.tsv (8 PASS, 2 SYNOPSIS, 0 FAIL); git grep -e 'Phase 13' over the 5 files returns 0 lines"
        status: pass
    human_judgment: false

duration: ~3h
completed: 2026-10-10
status: complete
---

# Phase 13 Plan 03: Data Quality, Gap Fill & Calendar Month-End Summary

**`ts_data_quality(_by)`/`_summary`/`_agg` now score real dates/frequency/n_short on the shared missing-period gap counter with a redesigned no-ACF1-penalty `behavioral_score` (D-05/D-06), and monthly/quarterly/yearly gap fill/fill-forward preserve month-end/day-of-month alignment instead of snapping to the 1st (D-09) — both pinned against independent fixtures and the built extension.**

## Performance

- **Duration:** ~3h
- **Tasks:** 3 (plan's task 2 executed as a 2-commit RED/GREEN TDD cycle per its
  `tdd="true"` tag; tasks 1 and 3 as planned `type="tracer"`/`type="auto"`)
- **Files modified:** 28 (6 created, 22 modified)

## Accomplishments

- `compute_data_quality` gained `dates: Option<&[i64]>`, `frequency_micros`,
  `frequency_type`, and `n_short: usize` parameters, delegating gap/temporal scoring
  to plan 13-01's shared `gaps::gap_counts`/`resolve_gap_step`: `n_gaps` is now the
  number of **missing periods** (same meaning as `ts_stats`'s `n_gaps`, D-04), a new
  `n_gap_runs` column preserves the pre-v0.10.0 meaning, `temporal_score =
  clamp(1 - 5*n_gaps/expected_length, 0, 1)` (previously always `1.0`), the structural
  length factor is `min(1, n_present/n_short)` (previously a fixed `/30`), and a new
  `is_short` flag reports whether the series is below `n_short`. `n_short == 0` is a
  hard `InvalidInput` error. The old `count_gaps`/`find_mode` (`HashMap`-based, a
  nondeterministic tie-break on duplicate-step counts) are deleted; determinism now
  comes from `gaps::detect_step`'s smaller-step tie rule.
- `behavioral_score` (D-06, STAT-04) is redesigned from scratch: drops the
  `|ACF1| > 0.95` penalty entirely (a predictable/smooth series is easy to forecast,
  not a quality defect); scores constancy (`0.0`), robust outliers via the
  Iglewicz-Hoaglin modified z-score (`MADN = 1.482602218505602 *
  median(|x - median|)`, threshold `3.5`, `outlier_ratio = 0` when `MADN == 0` so an
  intermittent series isn't flagged wholesale), and the longest run of consecutive
  exact zeros as a fraction of present values — with a `NULL`/`NaN` entry correctly
  breaking a zero run (the scan reads the original, unfiltered series). Pinned against
  a new independent numpy/scipy fixture family (`quality_scores`, 8 cases: smooth sine,
  white noise, constant, 2-outlier→0.96, 30-zero-run→0.7, NaN+NULL, dated 20-day-gap
  series, short-vs-n_short) via `gen_quality_reference.py`.
- FFI `anofox_ts_data_quality` gained `dates`/`frequency_micros`/`frequency_type`/
  `n_short`; `DataQualityResult` gained `n_gap_runs`/`is_short`; an `InvalidInput`
  error now maps to `ErrorCode::InvalidInput` (previously always `ComputationError`).
  C++ `_ts_data_quality(values, dates, frequency, n_short)` replaces the 1-argument
  overload, with `FunctionNullHandling::SPECIAL_HANDLING` set so a `NULL` `frequency`
  (meaning "auto-detect") actually reaches the function body instead of DuckDB's
  default NULL-in-NULL-out short-circuiting the whole call to `NULL`.
  `ts_data_quality`/`ts_data_quality_by`/`ts_data_quality_summary` macros pass dates
  and frequency/n_short through and select the new columns.
- `ts_data_quality_agg` (Phase 12 deferred item (a)) now keeps `NULL`-valued rows
  (counted in `n_missing`, position preserved in the date-ordered series — mirroring
  `ts_stats_agg`'s already-shipped fix) instead of silently dropping them; only a
  `NULL` **timestamp** is skipped. It takes no `n_short`/`frequency` argument: `n_short`
  is hardcoded to `30` (the pre-v0.10.0 fixed structural scale, documented) and
  frequency always auto-detects. Verified equal to `ts_data_quality_by(..., 30, NULL)`
  on a `NULL`-value-containing table, column-for-column, via the built extension.
- `calendar.rs` gained `pub(crate) step_months(base, months, month_end)` and
  `all_month_end(dates)`, extracted unchanged from `future_dates`'s `Months` branch
  (D-12) — `calendar_ffi`'s and `future_dates`'s own tests confirm zero behavior
  change there. `gaps.rs`'s `fill_gaps_monthly/quarterly/yearly` now share a single
  `fill_gaps_calendar(pairs, m)` that steps from the **previous observed date** (not
  the start of its period) via `step_months`; `fill_forward`'s calendar branches step
  from the **last observed date** the same way. Effect (D-09, G24): a month-end series
  stays month-end (`2024-01-31 → 02-29 → 03-31`), a 1st-of-month series is unchanged,
  and any other day-of-month keeps its own day (clamped to the target month's length)
  instead of snapping to the 1st. The now-unused `start_of_month/quarter/year` helpers
  are deleted.
- `detect_frequency`'s doc comment, `fill_forward_legacy`'s renamed
  `frequency_seconds → frequency_micros` parameter, and
  `anofox_ts_detect_frequency`'s FFI doc comment all now state the microsecond unit
  explicitly (Phase 12 deferred item (e), G23/WRAP-03); a new "Frequency
  Auto-Detection" section in `docs/api/22-supported-frequencies.md` documents the
  tie-break rule, the microsecond unit, the 28-31/89-92/365-366-day classification,
  and the month-end rule end-to-end.
- Docs/skills/CHANGELOG updated with no stale "fix planned for v0.10.0, Phase 13" note
  remaining on data-quality/gap-fill, and all internal "Phase 13" phase-number
  references rewritten to "v0.10.0" in the five doc/skill files this plan touches
  (per the plan's own doc-hygiene check) — `docs/api/03-statistics.md` (breaking-change
  callout + full column semantics for `ts_data_quality_by`/`_summary`/`_agg`),
  `docs/api/04-data-preparation.md` (month-end rule + runnable example, verified
  against the built extension), the EDA/data-prep skills (shipped columns, month-end
  rule). `examples/data_preparation/synthetic_data_prep_examples.sql`'s quality
  section now prints `n_gaps`/`is_short` with an explanatory comment, confirmed
  end-to-end.

## Task Commits

1. **Task 1 (tracer): wire dates/frequency/n_short through ts_data_quality** —
   `473d389` (feat)
2. **Task 2 RED: failing test for behavioral_score redesign** — `c6a65a8` (test)
3. **Task 2 GREEN: behavioral_score redesign + ts_data_quality_agg contract** —
   `751bb68` (feat)
4. **Task 3: month-end-preserving gap fill, detect_frequency microsecond docs** —
   `00a7364` (feat)

**Plan metadata:** (final docs commit, below)

## Files Created/Modified

- `crates/anofox-fcst-core/src/quality.rs` — dates/frequency/n_short-aware
  `compute_data_quality`; new `behavioral_score` (D-06); `n_gap_runs`/`is_short` fields;
  `count_gaps`/`find_mode` deleted
- `crates/anofox-fcst-core/src/gaps.rs` — `fill_gaps_calendar` (shared
  monthly/quarterly/yearly, steps from previous observed date); `fill_forward`'s
  calendar branches (steps from last observed date); `start_of_*` helpers deleted;
  `detect_frequency`/`fill_forward_legacy` doc/param updates
- `crates/anofox-fcst-core/src/calendar.rs` — `step_months`/`all_month_end` extracted
  (`pub(crate)`), reused by `future_dates`
- `crates/anofox-fcst-ffi/src/types.rs`/`lib.rs` — `anofox_ts_data_quality` signature;
  `DataQualityResult` fields; `anofox_ts_detect_frequency` doc
- `src/include/anofox_fcst_ffi.h` — regenerated, committed (both commits that touched FFI)
- `src/table_functions/ts_data_quality.cpp` — 4-arg `_ts_data_quality`,
  `ParseFrequencyForDataQuality`, `SPECIAL_HANDLING` null handling
- `src/aggregate_functions/ts_data_quality_agg.cpp` — NULL-row-keeping contract,
  `n_gap_runs`/`is_short` output fields
- `src/macros/ts_macros.cpp` — `ts_data_quality`/`_by`/`_summary` pass dates/frequency/n_short
- `validation/reference/python/gen_quality_reference.py`,
  `validation/reference/fixtures/quality_scores/cases.json` (new) — independent
  behavioral/structural/temporal/magnitude/overall reference
- `crates/anofox-fcst-core/tests/quality_gaps_reference.rs` (new) — 4 tests:
  quality_scores fixture, tied-step determinism, fill_cases, fill_forward month-end
- `test/sql/ts_data_quality_reference.test` (new, 38 assertions),
  `test/sql/ts_fill_gaps_month_end.test` (new, 53 assertions)
- `test/sql/ts_data_quality.test`, `test/sql/ts_fill_gaps_native.test`,
  `test/sql/ts_fill_forward_native.test`, `test/sql/extension_comparison.test` —
  updated for the new `_ts_data_quality` signature and the new month-end gap-fill
  values (confirmed via `./build/release/duckdb -unsigned`, these files are
  `require json`-gated and skipped by the local unittest harness)
- `docs/api/03-statistics.md`, `docs/api/04-data-preparation.md`,
  `docs/api/22-supported-frequencies.md`,
  `plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md`,
  `plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md`,
  `examples/data_preparation/synthetic_data_prep_examples.sql`, `CHANGELOG.md` —
  shipped semantics documented
- `validation/reference/README.md` — `quality_scores` family row + regeneration command
- `.planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md` —
  logged the pre-existing `'3mo'`-multiplier bug (see Decisions)
- `.planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-03.tsv`
  — 8 PASS / 2 SYNOPSIS / 0 FAIL

## Decisions Made

See frontmatter `key-decisions` for the full list. Summary: missing-data policy
(NULL+NaN) consistent with Phase 12; temporal score denominator is `expected_length`
not raw length; behavioral score formula per D-06 with an original-series zero-run
scan; `ts_data_quality_agg` mirrors `ts_stats_agg`'s NULL-row fix exactly with a
hardcoded `n_short=30`; `step_months`/`all_month_end` shared between the D-12
forecast-date rule and the D-09 gap-fill rule (one calendar rule, two consumers); a
pre-existing, out-of-scope `'3mo'`-multiplier bug logged rather than fixed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `ts_data_quality_agg.cpp` failed to compile against the new
`anofox_ts_data_quality` signature**
- **Found during:** Task 1, first `make release` after the FFI/C++ signature change
- **Issue:** `anofox_ts_data_quality` gained 4 new parameters (`dates`,
  `frequency_micros`, `frequency_type`, `n_short`); `ts_data_quality_agg.cpp`'s call
  site still used the old 5-argument form, causing `-fpermissive` type-mismatch
  compile errors (this task's own `<files>` list did not include this aggregate file —
  only task 2 does — but the signature change made it a hard compile error, not an
  optional improvement).
- **Fix:** Minimal compiling fix in Task 1's commit (pass dates, auto-detect
  frequency, `n_short=30` placeholder); the full NULL-row-keeping/output-column
  contract was completed in Task 2 as originally planned.
- **Files modified:** `src/aggregate_functions/ts_data_quality_agg.cpp`
- **Commit:** `473d389`

**2. [Rule 1 - Bug] `NULL` `frequency` on `_ts_data_quality` silently returned `NULL`
for the whole row**
- **Found during:** Task 1, first run of the new `ts_data_quality_reference.test`
  (the "NULL frequency (auto-detect)" case)
- **Issue:** DuckDB's default scalar-function NULL handling (NULL-in-NULL-out)
  short-circuited the call before `TsDataQualityFunction`'s own body (which correctly
  treats a NULL `frequency` as "auto-detect") ever ran.
- **Fix:** Set `ts_dq_func.null_handling = FunctionNullHandling::SPECIAL_HANDLING`
  (same pattern already used elsewhere in this codebase, e.g.
  `ts_forecast_inspect_scalar.cpp`).
- **Files modified:** `src/table_functions/ts_data_quality.cpp`
- **Commit:** `473d389`

**3. [Rule 1 - Bug] `test/sql/ts_fill_gaps_native.test`/`ts_fill_forward_native.test`
had three stale expected results under the new month-end rule**
- **Found during:** Task 3, inspecting json-gated files per the plan's own step 4
- **Issue:** `test_monthly_multi` (gap fill, days 01-15/05-15) expected `Feb/Mar/Apr
  01` (old start-of-month snapping); `test_monthly` and `test_yearly` (fill-forward,
  days `01-15/02-15` and `2020-06-15`) expected `Mar/Apr/May 01` and
  `2021/2022/2023-01-01` respectively (old start-of-month/start-of-year snapping).
  Under D-09 these all keep their observed day-of-month (`15`), since none of the
  series is month-end.
- **Fix:** Updated expected rows to `Feb/Mar/Apr 15` and `Mar/Apr 15` /
  `2021/2022/2023-06-15` respectively; confirmed via `./build/release/duckdb
  -unsigned` before editing (these files are skipped by the local `require json`
  harness, same precedent as plan 13-01).
- **Files modified:** `test/sql/ts_fill_gaps_native.test`,
  `test/sql/ts_fill_forward_native.test`
- **Commit:** `00a7364`

**4. [Rule 1/3 - Bug/Blocking] `_ts_data_quality`'s signature change broke two
json-gated test files not in this plan's declared file list**
- **Found during:** Task 3, grepping for every `_ts_data_quality(` call site
  repo-wide after Task 1's signature change
- **Issue:** `test/sql/ts_data_quality.test` (16 call sites) and
  `test/sql/extension_comparison.test` (2 call sites) used the old 1-argument form,
  which is now invalid SQL (wrong argument count) — a direct, mechanical consequence
  of Task 1's signature change, not a pre-existing bug.
- **Fix:** Rewrote every call to the new 4-argument form
  (`_ts_data_quality([...], NULL, NULL, 1)`); verified every rewritten statement
  against the built extension via `./build/release/duckdb -unsigned` (both files are
  `require json`-gated and skipped locally).
- **Files modified:** `test/sql/ts_data_quality.test`, `test/sql/extension_comparison.test`
- **Commit:** `00a7364`

**5. [Rule 1 - Bug, test authoring] My own `ts_fill_gaps_month_end.test` draft used
`'3mo'` for a quarterly case and got monthly stepping**
- **Found during:** Task 3, first run of the new `ts_fill_gaps_month_end.test`
- **Issue:** `ParseFrequencyWithType` parses `'3mo'` as `{seconds: 3, type: MONTHLY}`,
  but the gap-fill C++ layer maps `FrequencyType::MONTHLY` to a fixed
  `CalendarStep::Months(1)` regardless of the parsed count — a pre-existing,
  unrelated bug (see Decisions / deferred-items.md), not something this plan
  introduced or is scoped to fix.
- **Fix:** Changed the test to use the already-correct `'1q'` string instead of
  `'3mo'`; logged the underlying multiplier-discard bug to `deferred-items.md` rather
  than fixing it (out of scope for D-09).
- **Files modified:** `test/sql/ts_fill_gaps_month_end.test`,
  `.planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md`
- **Commit:** `00a7364`

---

**Total deviations:** 5 (2 Rule 1 bugs discovered via the plan's own required manual
verification steps, 1 Rule 3 compile-blocking fix, 1 Rule 1/3 mechanical signature-change
fix on out-of-plan-file-list test files, 1 test-authoring correction with a deferred,
unrelated bug logged). None changed scope; all were necessary for the build/tests to
pass or for the plan's own acceptance criteria to hold.

## Issues Encountered

- **`gsd_run check tdd-red-evidence` cannot classify a `cargo test` RED record.** Its
  TAP parser (`parseNodeTestSummary`/`tapFailedTestNames`) expects Node's `--test`
  runner output format (`# tests N`/`# pass N`/`# fail N`, `ok N - name`); `cargo test`
  emits a different text format entirely. RED was evidenced manually instead (see Task
  2's RED commit message): nonzero exit, the named target test
  (`quality_scores_fixture_matches_compute_data_quality`) failed on a real assertion
  comparing a documented formula value (`daily_gap20.behavioral_score: got 1, want
  0.975`), not a crash/panic/zero-test-discovery. Flagged here per the TDD gate
  reference's "## TDD Gate Compliance" convention — this is a tooling gap for Rust
  projects, not a process violation.
- **Pre-existing `cargo clippy --all-targets --all-features -- -D warnings` failure,
  unrelated to this plan** (same four findings 13-02 already logged:
  `decomposition.rs:70`, `features.rs:53`, `periods.rs:851`, `periods.rs:874`).
  Confirmed via `git diff` that none of those lines were touched by this plan, and
  that every file this plan DID touch is itself clippy-clean (`cargo clippy
  --all-targets --all-features -- -D warnings 2>&1 | grep -A3 '^error' | grep -- '-->'`
  shows only those same four pre-existing locations).
- **Discovered (not fixed) a second pre-existing bug while writing
  `ts_fill_gaps_month_end.test`**: `ParseFrequencyWithType`'s numeric multiplier is
  discarded for every calendar-frequency consumer except the forecast-date path. See
  Deviation 5 and `deferred-items.md` for the full writeup.

## TDD Gate Compliance

Plan frontmatter has no `type: tdd` (this is a `type: execute` plan with one
`tdd="true"` task), so the phase-wide gate sequence in `gsd-core/references/tdd.md`
does not apply to the whole plan — only Task 2 carried `tdd="true"`.

- **RED:** `c6a65a8` — `test(13-03): add failing test for behavioral_score redesign
  (D-06)` — present, named correctly.
- **GREEN:** `751bb68` — `feat(13-03): implement behavioral_score redesign (D-06) and
  ts_data_quality_agg contract` — present, named correctly.
- **REFACTOR:** none needed/committed (the RED→GREEN diff was already the minimal
  correct implementation; no follow-up cleanup commit).
- **Verdict tool caveat:** `gsd_run check tdd-red-evidence` could not programmatically
  classify the RED record (see Issues Encountered) — RED is evidenced manually in the
  RED commit message instead. This is a tooling-format gap (Node TAP vs. `cargo test`
  text output), not a process violation: the named target test genuinely failed on a
  real assertion before the implementation landed, and genuinely passed after.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `calendar.rs`'s `step_months`/`all_month_end` are now shared primitives (forecast
  dates D-12, gap fill D-09) — any future calendar-stepping surface should reuse them
  rather than re-deriving the month-end rule a third time.
- The `quality_scores` fixture family and its generator are available for any future
  phase that touches `compute_data_quality` again.
- Deferred, unrelated bug logged for a future wrapper-surface phase: calendar-frequency
  numeric multipliers (`'2mo'`, `'2q'`, `'2y'`, ...) are silently ignored outside the
  forecast-date path — see `deferred-items.md`.
- This completes Phase 13's data-quality and gap-fill/calendar scope (ROADMAP success
  criteria 4, 5, and the `behavioral_score` part of criterion 2). No blockers for later
  plans in this phase or Phase 15/16/17/18's previously-routed deferred items.

---
*Phase: 13-core-statistics-metrics-data-quality*
*Completed: 2026-10-10*

## Self-Check: PASSED

All created files verified present on disk (`validation/reference/python/gen_quality_reference.py`,
`validation/reference/fixtures/quality_scores/cases.json`,
`crates/anofox-fcst-core/tests/quality_gaps_reference.rs`,
`test/sql/ts_data_quality_reference.test`, `test/sql/ts_fill_gaps_month_end.test`,
`.planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-03.tsv`);
all four task commit hashes (`473d389`, `c6a65a8`, `751bb68`, `00a7364`) verified
present in `git log --oneline --all`.
