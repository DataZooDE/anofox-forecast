---
phase: 13-core-statistics-metrics-data-quality
plan: 02
subsystem: statistics
tags: [rust, ffi, duckdb, stl, supsmu, r-port, stats, seasonality]

# Dependency graph
requires:
  - phase: 13-core-statistics-metrics-data-quality
    plan: 01
    provides: TsStats struct shape, compute_ts_stats/compute_ts_stats_with_dates_and_type
      call sites, Phase 13 snippet harness under .planning/phases/13-core-statistics-metrics-data-quality/snippets/
provides:
  - R-faithful stl::stl (Cleveland et al. 1990 STL, ported from R's stl.c) and
    supsmu::supsmu (Friedman's super smoother, ported from R's ppr.f) in
    crates/anofox-fcst-core/src, reusable by any future seasonal-decomposition work
    (Phase 15 DET-04 named explicitly in the plan)
  - stats::stl_strengths: the full D-02 contract (single period / forecast::mstl
    iteration for several / supsmu fallback for none), returning Option<f64> trend_strength
    and seasonality_strength
  - seasonal_period parameter (0 auto-detect / 1 non-seasonal / >=2 explicit) on
    compute_ts_stats, compute_ts_stats_with_dates_and_type, the FFI layer, and every SQL
    surface (ts_stats_by, ts_stats macro, ts_stats_agg always auto-detects)
  - r_stl_strength reference fixture family (9 cases) + gen_stl_strength_reference.R
affects: [13-03-data-quality-metrics]

actuals:
  tokens: 49262
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "R Fortran/C algorithm ports kept structurally close to the source (1-indexed loop
      arithmetic preserved where it matters for the 1e-8 fixture tolerance) rather than
      rewritten idiomatically, with #[allow(clippy::needless_range_loop)] where the
      co-indexed-slice pattern is intentional (stlest)."
    - "Near-zero-variance epsilon guard (f64::EPSILON on both variances, not exact ==0.0)
      for the seasonal-strength 0/0 case, since a genuinely constant series' STL
      decomposition carries ~1e-30-scale floating rounding noise even in R itself —
      confirmed by reproducing R's own non-reproducible ~0.056 output on rep(5, 48) and
      choosing a numerically robust None instead of chasing non-portable noise."

key-files:
  created:
    - crates/anofox-fcst-core/src/stl.rs
    - crates/anofox-fcst-core/src/supsmu.rs
    - crates/anofox-fcst-core/tests/stl_reference.rs
    - validation/reference/r/gen_stl_strength_reference.R
    - validation/reference/fixtures/r_stl_strength/cases.json
    - .planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md
  modified:
    - crates/anofox-fcst-core/src/stats.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-core/tests/stats_reference.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/include/anofox_fcst_ffi.h
    - src/table_functions/ts_stats.cpp
    - src/aggregate_functions/ts_stats_agg.cpp
    - src/macros/ts_macros.cpp
    - test/sql/ts_stats_reference.test
    - docs/api/03-statistics.md
    - docs/guides/02-model-selection.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - CHANGELOG.md
    - .planning/phases/13-core-statistics-metrics-data-quality/snippets/synopsis.txt
    - .planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-02.tsv

key-decisions:
  - "Rejected the crate's own STL (anofox_forecast::seasonality::stl::STL, 0.15.3): it uses
    a degree-0 kernel smoother with truncated symmetric windows, no cycle-subseries end
    extension, and no jump interpolation, so it cannot reproduce R's stats::stl to any
    useful tolerance. Ported stats::stl from R's own C translation of the Fortran
    (stl.c) instead, per D-02's 'prefer the crate's STL if correct vs R stats::stl, else
    core'. stl.rs is explicitly available for Phase 15 DET-04 to reuse."
  - "tsfeatures::stl_features (built on forecast::mstl -> stats::stl) used as the
    reference implementation instead of feasts (D-02's named choice), which is not
    installed in this repo's R environment per validation/reference/README.md section 5."
  - "Combined the plan's Task 1 (single-period tracer) and Task 2 (full contract) Rust/FFI
    work into one commit: stl_strengths' single/multi/none branches share the
    trend_strength_rule/seasonal_strength_rule variance-ratio helpers, and splitting them
    would have required a throwaway intermediate function signature with no test
    coverage of its own. C++ wiring and the SQL-level tests still landed as the planned
    second commit."
  - "seasonal_strength_rule treats both variances as exactly zero below f64::EPSILON
    (not literal ==0.0): a genuinely constant series' STL decomposition carries pure
    floating-point rounding noise at the ~1e-30 scale even under R's own stats::stl
    (reproduced directly: rep(5, 48) gives tsfeatures::stl_features seasonal_strength
    0.056, not NA) — not a reproducible value to pin at 1e-8, and not what the plan's
    constant_48 acceptance criterion (seasonality_strength None) intends."

requirements-completed: [STAT-02]

coverage:
  - id: D1
    description: "trend_strength/seasonality_strength are the Wang/Hyndman STL F_T/F_S
      from an R-faithful STL (stats::stl port) for one period, the forecast::mstl
      iteration for several, and a supsmu trend when no period is usable; seasonal_period
      parameter (0/1/>=2) resolves the period on every ts_stats surface"
    requirement: STAT-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/stl_reference.rs (3 tests: single-period components+strengths, msts per-period strengths, non-seasonal/short/degenerate supsmu+strengths)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_stats_reference.test (15 new STL-strength assertions, 46 total)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Audit random walk's seasonality_strength is ~0.0865 under tsfeatures'
      s.window=11 (ROADMAP SC2's 'about 0.05' was the s.window='periodic' variant),
      not 0.96 as before"
    requirement: STAT-02
    verification:
      - kind: integration
        ref: "test/sql/ts_stats_reference.test rw150 block: abs(seasonality_strength - 0.0865166431060249) < 1e-8"
        status: pass
    human_judgment: false
  - id: D3
    description: "Docs/skills describe the shipped F_T/F_S definitions, seasonal_period,
      NULL rules, and 0.3/0.6 rule-of-thumb thresholds with no Phase 13 fix-planned note
      left on these two rows"
    verification:
      - kind: other
        ref: ".planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-02.tsv (4 PASS, 2 SYNOPSIS, 0 FAIL); stale-note greps both return 0"
        status: pass
    human_judgment: false

duration: ~5h
completed: 2026-10-10
status: complete
---

# Phase 13 Plan 02: Seasonal/Trend Strength (STL) Summary

**R-faithful ports of `stats::stl` and `stats::supsmu` drive `ts_stats`'s `trend_strength`/`seasonality_strength` (Wang/Hyndman F_T/F_S) through a new `seasonal_period` parameter on every surface, replacing the old ad-hoc linear-fit/ACF-peak formulas and pinned against R to 1e-8.**

## Performance

- **Duration:** ~5h
- **Tasks:** 3 (plan's task 1+2 Rust/FFI work landed as one commit — see Deviations;
  task 3 as planned)
- **Files modified:** 20 (6 created, 14 modified)

## Accomplishments

- `crates/anofox-fcst-core/src/stl.rs`: a line-for-line Rust port of R's `stats::stl`
  (Cleveland, Cleveland, McRae & Terpenning 1990), ported from R's own C translation of
  the original Fortran (`stl.c`, fetched from `r-source` trunk) — degree-0/1 local loess
  fits with tricube weights, cycle-subseries smoothing with one-point end extension, the
  three-moving-average + loess low-pass filter, jump-interpolation, and the
  robustness-weight outer loop. `StlParams::r_default` reproduces R's exact `t.window`/
  `l.window`/jump formulas (verified against R's own `fit$win`/`fit$jump` for periods 12
  and 7).
- `crates/anofox-fcst-core/src/supsmu.rs`: a port of Friedman's super smoother
  (`stats::supsmu`, via R's `ppr.f`), restricted to the call shape `stats::supsmu(seq_len(n), x)`
  actually uses (non-periodic, `span = "cv"`, `bass = 0`).
- `stats::stl_strengths` (new): the full D-02 contract — `F_T = max(0, 1 - Var(R)/Var(T+R))`,
  `F_S = max(0, 1 - Var(R)/Var(S+R))`, matching `tsfeatures::stl_features`'s exact rules
  (trend 0 for a zero-variance series; seasonal `None` for the `0/0` case). One usable
  period runs a single `stl()` call (`s.window = 11`); several run the
  `forecast::mstl` iteration (2 passes, `s.window = 7 + 4*position` by ascending period,
  trend from the last fit, seasonality reported as the **maximum** per-period `F_S_i`);
  none runs `supsmu` and reports seasonality `None`.
- `compute_ts_stats`/`compute_ts_stats_with_dates_and_type` gain a `seasonal_period`
  parameter (`0` auto-detect via `seasonality::detect_seasonality`, `1` none, `>= 2`
  explicit); `trend_strength`/`seasonality_strength` become `Option<f64>`; the old
  `compute_strength_metrics` (linear-fit `|r|` / `max(|ACF|)` at lags 2/4/7/12) is deleted.
- New `r_stl_strength` reference-fixture family (9 cases: 4 single-period, 1 two-period
  `msts`, 2 non-seasonal, 1 short/insufficient-data, 1 degenerate/constant), generated by
  `gen_stl_strength_reference.R` against R's `stats::stl`, `forecast::mstl`,
  `stats::supsmu`, and `tsfeatures::stl_features` — every component (`1e-8`) and strength
  (`1e-8`) pinned in `stl_reference.rs` (3 tests, all passing).
- FFI (`anofox_ts_stats`/`anofox_ts_stats_with_dates_and_type`) gains the `seasonal_period`
  argument; `TsStatsResult` gains `has_trend_strength`/`has_seasonality_strength`; header
  regenerated (cbindgen, via the FFI crate's own `build.rs`) and committed.
- C++: `_ts_stats_by_native` (table function) and `_ts_stats_with_dates` (scalar behind
  the `ts_stats` macro) both gain a `seasonal_period` argument (NULL -> 0, negative ->
  `InvalidInputException` naming the macro); `ts_stats_by`/`ts_stats` macros gain a named
  `seasonal_period := 0` parameter; `ts_stats_agg` always passes `0` (it has no period
  argument, documented as "always auto-detects"); every writer (table function, scalar,
  aggregate) emits `NULL` for the strengths per the `has_*` flags.
- 15 new SQL assertions (46 total in `ts_stats_reference.test`), each individually
  verified against the built extension before being written as sqllogictest blocks:
  `rw150` (the audit random walk, as 150 monthly rows) with `seasonal_period := 12`
  matches R to `1e-8`; the `ts_stats` macro agrees with `ts_stats_by` on the same call;
  `det_seasonal84` (generated in SQL) with `seasonal_period := 7` matches its fixture;
  `det_trend60` with `seasonal_period := 1` gives `seasonality_strength NULL` and a
  matching `trend_strength`; the first 14 `det_seasonal84` rows with
  `seasonal_period := 7` give `NULL` (too short); `seasonal_period := 0` on
  `det_seasonal84` agrees with `ts_stats_agg` and is `NOT NULL`; `seasonal_period := -1`
  errors.
- Docs (`docs/api/03-statistics.md`), the model-selection guide, and the EDA/detection
  skills document the shipped `F_T`/`F_S` definitions, `seasonal_period`, the `NULL`
  rules, and 0.3/0.6 rule-of-thumb strength thresholds; no "fix planned for v0.10.0,
  Phase 13" note remains on these two rows.

## Task Commits

1. **Rust/FFI: R-faithful STL/supsmu port + full stl_strengths contract** — `f35ad9f` (feat)
2. **C++: seasonal_period + nullable strengths through table/scalar/aggregate/macros** — `484e378` (feat)
3. **Docs/skills/CHANGELOG + snippet harness** — `3e673bf` (docs)

## Files Created/Modified

- `crates/anofox-fcst-core/src/stl.rs` — R `stats::stl` port (`stl`, `StlParams`, `StlComponents`)
- `crates/anofox-fcst-core/src/supsmu.rs` — R `stats::supsmu` port (`supsmu`)
- `crates/anofox-fcst-core/src/stats.rs` — `StlStrengths`, `stl_strengths`,
  `trend_strength_rule`/`seasonal_strength_rule`; `compute_ts_stats`/
  `compute_ts_stats_with_dates_and_type` gain `seasonal_period`; `trend_strength`/
  `seasonality_strength` become `Option<f64>`; `compute_strength_metrics` deleted
- `crates/anofox-fcst-core/src/lib.rs` — re-exports `stl`/`StlComponents`/`StlParams`,
  `supsmu`, `stl_strengths`/`StlStrengths`
- `crates/anofox-fcst-core/tests/stl_reference.rs` (new) — 3 tests against `r_stl_strength`
- `crates/anofox-fcst-core/tests/stats_reference.rs` — updated `compute_ts_stats(...)` call
  sites for the new `seasonal_period` parameter
- `validation/reference/r/gen_stl_strength_reference.R` (new), `validation/reference/fixtures/r_stl_strength/cases.json` (new)
- `crates/anofox-fcst-ffi/src/types.rs`/`lib.rs` — `seasonal_period` FFI argument,
  `has_trend_strength`/`has_seasonality_strength`
- `src/include/anofox_fcst_ffi.h` — regenerated, committed
- `src/table_functions/ts_stats.cpp` — `seasonal_period` on the table function bind +
  both scalar paths; `NULL`-strength writers
- `src/aggregate_functions/ts_stats_agg.cpp` — passes `seasonal_period=0`; `NULL`-strength writer
- `src/macros/ts_macros.cpp` — `seasonal_period := 0` named param on `ts_stats_by`/`ts_stats`
- `test/sql/ts_stats_reference.test` — 15 new assertions (46 total)
- `docs/api/03-statistics.md`, `docs/guides/02-model-selection.md`,
  `plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md`,
  `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md`, `CHANGELOG.md` —
  shipped semantics documented
- `.planning/phases/13-core-statistics-metrics-data-quality/snippets/synopsis.txt`,
  `pass-list-13-02.tsv` — 2 signature blocks allow-listed as non-executable synopses; 4
  PASS / 2 SYNOPSIS / 0 FAIL
- `.planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md` (new) —
  pre-existing, unrelated `cargo clippy` failure logged (see Issues Encountered)

## Decisions Made

- The crate's own STL (`anofox_forecast::seasonality::stl::STL`, 0.15.3) was rejected as
  not R-faithful (degree-0 kernel smoother, truncated windows, no end extension, no
  jumps) — `stats::stl` was ported into core instead, per D-02. `stl.rs` is available for
  Phase 15 DET-04 to reuse directly.
- `tsfeatures::stl_features` (built on `forecast::mstl` -> `stats::stl`) is the reference
  implementation used throughout, since `feasts` (D-02's named choice) is not installed
  in this repo's R environment (`validation/reference/README.md` section 5 already
  documents this).
- Plan tasks 1 (single-period tracer) and 2 (full contract) were combined into one Rust/
  FFI commit: `stl_strengths`'s three branches (none/one/several periods) share the same
  `trend_strength_rule`/`seasonal_strength_rule` helpers, and the single-period-only
  intermediate the plan describes would have been a throwaway signature with no
  independent test coverage. The C++ wiring and SQL-level tests still landed as a
  distinct second commit, matching the plan's task-2 boundary for that half of the work.
- `seasonal_strength_rule`'s `0/0 -> None` guard uses `f64::EPSILON` on both variances
  rather than literal `== 0.0` (see Issues Encountered for why).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `stl_strengths`'s non-seasonal branch passed the wrong array as `deseason`**
- **Found during:** Running `stl_reference.rs`'s non-seasonal-cases test against the fixture
- **Issue:** `trend_strength_rule(values, &trend, &remainder)` passed the `supsmu` trend
  itself as the `deseason` argument; it should be `values` (since `deseason = trend +
  remainder = values` exactly when there's no seasonal component to remove). This gave
  `det_trend60` a `trend_strength` of `0.9732...` instead of the fixture's `0.9742...`.
- **Fix:** Pass `values` directly as `deseason` (no need to re-derive it).
- **Files modified:** `crates/anofox-fcst-core/src/stats.rs`
- **Commit:** `f35ad9f`

**2. [Rule 1 - Bug] model-selection guide's `ts_stats` snippet was invalid SQL**
- **Found during:** Task 3, writing the doc fix (plan step 2 names this explicitly)
- **Issue:** `(ts_stats(LIST(quantity ORDER BY date))).*` and `.trend_strength` treat
  `ts_stats` as a scalar function callable inline in a `SELECT` list; it is a table
  macro. Confirmed as a parser error against the built extension before fixing.
- **Fix:** Rewrote as two runnable `SELECT ... FROM ts_stats(...)` calls, the second
  using `seasonal_period := 7`; both verified against the built extension.
- **Files modified:** `docs/guides/02-model-selection.md`
- **Commit:** `3e673bf`

**3. [Rule 3 - Blocking] Snippet harness flagged the two changed `→ TABLE` signature lines as FAIL**
- **Found during:** Task 3, first harness run after adding `seasonal_period` to the
  `ts_stats_by`/`ts_stats` doc signatures
- **Issue:** The harness only re-executes snippet blocks that changed since `BASE`;
  adding `seasonal_period := 0` to the signature lines made them newly "changed" and
  thus newly selected for execution — but a signature synopsis (`foo(...) → TABLE`) is
  not valid SQL and was never meant to run.
- **Fix:** Added both exact `first_non_comment_line` keys to `synopsis.txt` (the
  harness's existing allow-list mechanism for exactly this situation).
- **Files modified:** `.planning/phases/13-core-statistics-metrics-data-quality/snippets/synopsis.txt`
- **Commit:** `3e673bf`

### Auto-added Functionality

**4. [Rule 2 - Missing correctness guard] `seasonal_strength_rule`'s `0/0` guard needed an
epsilon, not exact equality**
- **Found during:** Task 1, `constant_48` fixture case failing
- **Issue:** The plan's `constant_48` acceptance criterion is `seasonality_strength
  None`. A literal `== 0.0` guard on `Var(R)`/`Var(S+R)` failed this: even a perfectly
  constant input series' STL decomposition carries floating-point rounding noise at the
  `~1e-30` scale (not exactly zero), and dividing two such noise-scale variances is
  numerically unstable — different STL implementations (even R's own `stats::stl`
  itself, reproduced directly: `tsfeatures::stl_features(ts(rep(5,48), frequency=12))`
  gives `seasonal_strength = 0.056`, not `NA`) land on different non-reproducible values.
- **Fix:** Guard on `var_rp.abs() < f64::EPSILON && vare.abs() < f64::EPSILON` instead of
  exact zero — both are below any meaningful float precision for realistic input scales,
  so this is a safe, deterministic stand-in for "no real seasonal variance" without
  chasing non-portable noise.
- **Files modified:** `crates/anofox-fcst-core/src/stats.rs`
- **Commit:** `f35ad9f`

---

**Total deviations:** 4 (2 Rule 1 bug fixes, 1 Rule 2 correctness guard, 1 Rule 3 harness
fix). None changed scope; all were necessary for the plan's own stated acceptance
criteria to hold.

## Issues Encountered

- **Pre-existing `cargo clippy --all-targets -- -D warnings` failure, unrelated to this
  plan.** `crates/anofox-fcst-core/src/decomposition.rs:70` and `features.rs:53` both
  carry `#[allow(clippy::manual_is_multiple_of)]`, an unrecognized lint name under both
  the default `stable` toolchain (clippy 0.1.99) and the pinned `1.89.0` toolchain
  (clippy 0.1.89) installed in this environment — `-D unknown-lints` turns that into a
  hard error. `periods.rs:851,874` separately trip `clippy::needless_range_loop`. `git
  diff HEAD -- <those three files>` confirms none were touched by this plan. Logged to
  `deferred-items.md` per the scope-boundary deviation rule rather than fixed; `stl.rs`/
  `supsmu.rs`/`stats.rs` (this plan's own files) are themselves clippy-clean, confirmed
  by running the same command and checking that no finding references them.
- **`test/sql/ts_stats.test` (json-gated, skipped on this native build) needed no
  changes.** Grepped for `trend_strength`/`seasonality_strength` and found no references
  (it only exercises `_ts_stats`/`ts_stats_by` on other columns), so no manual CLI
  verification against new values was needed for that file this time, unlike 13-01's
  equivalent check.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `stl.rs`/`supsmu.rs` are general-purpose R-faithful decomposition primitives, not
  tied to `ts_stats` — Phase 15 DET-04 (named explicitly in the plan) can call `stl::stl`
  directly for its own decomposition needs rather than re-porting the algorithm.
- `trend_strength`/`seasonality_strength` are stable `Option<f64>` outputs across every
  `ts_stats` surface; 13-03's data-quality scoring work can read them without further
  changes if it needs to gate on trend/seasonal strength.
- No blockers for 13-03.

---
*Phase: 13-core-statistics-metrics-data-quality*
*Completed: 2026-10-10*

## Self-Check: PASSED

All created files verified present on disk (`stl.rs`, `supsmu.rs`, `stl_reference.rs`,
`gen_stl_strength_reference.R`, `fixtures/r_stl_strength/cases.json`,
`deferred-items.md`); all three task commit hashes (`f35ad9f`, `484e378`, `3e673bf`)
verified present in `git log --oneline --all`.
