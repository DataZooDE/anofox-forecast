# Phase 20: Detection & Metric Bug Fixes - Research

**Researched:** 2026-10-23
**Domain:** Rust statistical correctness (BOCPD changepoints, STL-based seasonality strength, Hyndman-Koehler MASE) behind a DuckDB C++ FFI marshalling layer
**Confidence:** HIGH (every root cause below is `[VERIFIED]` against a live build or an opened source file; BUG-03's exact mechanism is the one open item, `[ASSUMED]` + flagged)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**#287 — changepoints (BUG-05, BUG-06, BUG-07)**
- OPT-IN, not default: add a `deseasonalize` option (default `false`) to `ts_detect_changepoints_by` and `ts_detect_changepoints_agg` (MAP param, consistent with existing params such as `hazard_lambda`). When `true`: estimate the seasonal component with STL at the given `seasonal_period` param, or the auto-detected period if none is given, and subtract ONLY the seasonal component (keep trend and level) before BOCPD, so level shifts survive. Default results must stay byte-identical to current behaviour; pin that with a test.
- Agg consistency: in `ts_detect_changepoints_agg`, `is_changepoint` must be true exactly when `changepoint_probability >= threshold` (the same rule as the table function). Fix the agg's decision rule; no unflagged row at p = 0.996.
- `ts_detect_periods_by` returns period 7 on the #287 panel: verify it on main and pin it with a regression test (already fixed on main per #287, release-only bug).
- Fixture: the #287 panel (3 series x 84 days; B has +15 from day 50 = 2024-02-20). With deseasonalize: no flags on A/C, B flagged at 2024-02-20 (allow the documented BOCPD detection-lag tolerance only if justified by a reference).
- All computation in Rust (`crates/anofox-fcst-core`); C++ only marshals (`make audit`).

**#286 — seasonality strength (BUG-04)**
- One shared Rust implementation: `ts_classify_seasonality_by` / `_agg` must use the SAME seasonal-strength function as `ts_stats_by` (Hyndman `max(0, 1 - Var(R)/Var(S+R))` on the detrended STL decomposition). Remove the divergent computation.
- `is_seasonal` keeps its existing threshold, applied to the corrected strength. The #286 weekly series -> seasonal; never NaN on a regular complete daily series; `seasonal_strength` consistent with `cycle_strengths`.
- Reference check against R (`forecast`/`feasts` STL strength) or the existing v0.10.0 STL-strength fixtures (`validation/reference`), per the project's reference-fixture rule.

**#285 — row order (BUG-03)**
- `_ts_mase_native` sorts by date within each series before computing the in-sample naive scale.
- AUDIT every native metric / `_by` table function and aggregate for the same row-order dependence (anything that assumes input arrives date-sorted). Fix each one found, and pin every metric with a test that feeds date-ordered, reversed and shuffled input (including the #285 UNION ALL + scalar-subquery shape) and asserts identical results.

### Claude's Discretion
- Exact option name spelling (`deseasonalize` preferred) and its MAP-parsing location (shared params header).
- How the shuffle test is generated (deterministic seed).

### Deferred Ideas (OUT OF SCOPE)
- Making `deseasonalize` the default for changepoints (user chose opt-in; revisit later if users ask).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BUG-03 | `ts_mase_by` order-independent (sort by date before in-sample scale) | See "BUG-03" below: sort already exists at file:line, yet the bug reproduces — root cause is NOT a missing sort; flagged as an open diagnostic item, not a known fix |
| BUG-04 | `ts_classify_seasonality_by` seasonality strength consistent with `ts_stats_by`, never NaN | Root cause pinpointed exactly: `classify_seasonality` (seasonality.rs:654-690) skips the detrend step that its sibling `seasonal_strength_variance` (seasonality.rs:513-544) already applies, and uses a different formula from the one `ts_stats_by` uses (stats.rs `seasonal_strength_rule`, line 528) |
| BUG-05 | `ts_detect_changepoints_by`/`_agg` opt-in `deseasonalize` | STL/MSTL building blocks identified; reusable `detrend_linear` pattern identified; no existing deseasonalize path to reuse, must be built |
| BUG-06 | agg `is_changepoint` agrees with `changepoint_probability` and threshold | TWO independently confirmed root causes: (1) the agg's MAP-param parser is a no-op — `hazard_lambda`/any param is silently ignored; (2) `is_changepoint` is computed by a MAP-run-length-drop heuristic in Rust (changepoint.rs:504-511), not a probability-threshold rule — this is a NEW concept not yet in the codebase |
| BUG-07 | `ts_detect_periods_by` returns period 7 on #287 panel | Confirmed already correct on current HEAD; only a regression test is needed |
</phase_requirements>

## Summary

Three issues, three independently-verified root causes, reproduced live against the current HEAD build (`fc00721`, DuckDB v2.0.0, `./build/release/duckdb`). None of the three bugs are "obviously the fix CONTEXT.md's prose implies" — in every case the research surfaced a *more specific* mechanism than the issue bodies guessed at, which changes what the planner should scope:

1. **BUG-04 (seasonality strength) is the cleanest fix.** `classify_seasonality` (`crates/anofox-fcst-core/src/seasonality.rs:654-690`) hands the RAW, undetrended series straight to `fdars_classify_seasonality`. A sibling function in the exact same file (`seasonal_strength_variance`, lines 513-544) already carries a comment documenting this *identical* bug class and its fix: "fdars 0.47's own `seasonal_strength_variance` measures variance explained on the RAW series, so a trend inflates total variance and crushes the seasonal-share ratio ... measured 0.00296 instead of ~1.0" — and fixes it by calling `detrend_linear` first. `classify_seasonality` never received the same fix. Additionally, CONTEXT specifies the target formula is `ts_stats_by`'s MSTL-remainder-based `seasonal_strength_rule` (stats.rs:528), which is a *third*, different implementation from `seasonal_strength_variance` — so the fix is "wire `classify_seasonality`'s `seasonal_strength` field to stats.rs's MSTL-remainder rule," not "call `seasonal_strength_variance`."

2. **BUG-06 (agg `is_changepoint`) has two stacked bugs, not one.** First: `TsChangepointsAggUpdate`/`Finalize` in `src/aggregate_functions/ts_changepoints_agg.cpp` never parses the `params` MAP at all — the comment at line 105 ("Parse params MAP if valid") is a stub with no code under it, and `hazard_lambda` is hardcoded to the constructor default (250.0) regardless of what the caller passes. Verified empirically: passing `MAP{'hazard_lambda': '50'}` vs `MAP{}` to the aggregate produces byte-identical output (A:6, B:2, C:6, max_prob 0.996 in both cases), while the table function's own `hazard_lambda` handling changes its flagged-date list when the param changes. Second, deeper bug: even once params are parsed, `is_changepoint` is NOT currently derivable from `changepoint_probability >= threshold` anywhere in the codebase — in Rust, `bocpd_trace` (`changepoint.rs:504-511`) sets `is_changepoint[i] = true` when the BOCPD MAP run-length estimate *decreases* relative to the previous step, a structurally different decision rule from thresholding the posterior probability `prob[t]`. There is no `threshold` parameter anywhere in the changepoint pipeline today (confirmed by grep — zero hits). CONTEXT's target behavior ("`is_changepoint` true exactly when `changepoint_probability >= threshold`") is a new contract to build, not a wiring fix.

3. **BUG-03 (MASE row order) does NOT have a missing sort.** `TsMaseNativeFinalize` (`src/table_functions/ts_metrics_native.cpp:846-849`) already sorts each group's rows by `date_int` before computing `t0` and the in-sample/evaluation split. The repro still returns NULL for the "literal cut-off" variant on the current build (flipped from the issue body, which showed the *subquery* variant failing and the *literal* variant succeeding — on this build it's the reverse). This means the described fix ("sort by date") is already in place and the bug persists regardless, so the real defect is upstream of the sort — most likely in how DuckDB's query plan feeds rows/chunks into the `in_out_function` across a `UNION ALL` + correlated-subquery-filtered branch (dependent-join rewrite, batching, or column-index staleness), not in `_ts_mase_native`'s own logic. This needs a diagnostic task (EXPLAIN-based, or deliberately re-tested per DuckDB plan shape) before a fix can be written; blindly re-adding a sort will not move the needle since one already exists.

**Primary recommendation:** Treat BUG-04 and the deseasonalize half of BUG-05 as "well-understood Rust fixes, reuse existing STL/detrend code." Treat BUG-06 as "build a new probability-threshold contract in Rust shared by both consumers, plus fix a completely missing MAP-param parser in the agg." Treat BUG-03 as a diagnostic task first — do not let the plan assume "add a sort" is the fix, because that code already exists and the bug still reproduces.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| BOCPD changepoint probability + is_changepoint decision | Rust core (`changepoint.rs`) | C++ marshals LIST<STRUCT> / scalar row | Project rule: no statistics in C++; `make audit` enforces it |
| Deseasonalize transform (STL seasonal subtraction) | Rust core (new fn, likely in `changepoint.rs` or `decomposition.rs`, calling existing MSTL/STL) | C++ passes `seasonal_period`/`deseasonalize` MAP flags through | Reuses the v0.10.0 STL-based MSTL (fdars 0.47) already used for `ts_stats_by`/`ts_mstl_decomposition_by` |
| Seasonal-strength computation | Rust core (`stats.rs::seasonal_strength_rule` becomes the single source of truth) | C++ exposes via `ts_classify_seasonality_by`/`_agg` and `ts_stats_by` | `seasonality.rs::classify_seasonality` must stop delegating raw-series strength to fdars and instead call the same MSTL-remainder rule stats.rs uses |
| MASE in-sample scale / row-order independence | Rust core (`anofox_ts_mase` FFI call) + C++ sort-before-call (already present) | DuckDB query planner (dependent-join/UNION ALL row delivery order) — out of this repo's control, must be defensive instead | The sort exists at the C++ layer (`ts_metrics_native.cpp:846-849`); the remaining defect is upstream of it |
| MAP param parsing for changepoints (`hazard_lambda`, new `deseasonalize`, new `threshold`) | C++ (parse) → Rust (consume as typed args) | None | C++ may parse a MAP into typed scalars (not a "statistic"); currently done ad hoc per-function (regex in table fn, nothing at all in the agg) — should move to one shared, tested parser used by both |

## Standard Stack

No new external packages are introduced by this phase (pure bug-fix phase against the existing `anofox-forecast` 0.15.10/0.16.x Rust core and the existing fdars-core 0.3 (0.47-API) dependency already pinned in `Cargo.toml`). **Package Legitimacy Audit is N/A** — no `npm install`/`cargo add`/`pip install` of a new dependency is in scope for BUG-03..07.

### Reused crate functions (all `[VERIFIED]` by reading the source this session)

| Function | File:line | Purpose | Already used by |
|----------|-----------|---------|------------------|
| `detrend_linear` | `crates/anofox-fcst-core/src/detrending.rs` (imported at `seasonality.rs:6`) | Linear detrend before a variance-ratio strength calc | `seasonal_strength_variance`, `seasonal_strength_spectral`, `seasonal_strength_windowed` (seasonality.rs:539, 574, 630) |
| `seasonal_strength_rule(remainder, seasonal)` | `crates/anofox-fcst-core/src/stats.rs:528-553` | Hyndman `max(0, min(1, 1 - Var(remainder)/Var(remainder+seasonal)))` on MSTL components — the exact formula CONTEXT names as canonical | `ts_stats_by`'s `TsStats::seasonality_strength` (stats.rs:307-373, 588-595) |
| MSTL / STL decomposition that produces `remainder`/`seasonal` | Already wired into the `ts_stats_by` code path in stats.rs (fdars 0.47 MSTL, per project history "Phase 13/15") | Produces the `remainder`/`seasonal` arrays `seasonal_strength_rule` consumes | `ts_stats_by`, `ts_mstl_decomposition_by` |
| `fdars_classify_seasonality` | External crate call at `seasonality.rs:688` | Produces `timing_classification`, `modulation_type`, `cycle_strengths`, `timing_variability` — NOT just `seasonal_strength` | `ts_classify_seasonality_by`/`_agg` (current, buggy, strength field) |
| `bocpd_trace` | `crates/anofox-fcst-core/src/changepoint.rs:387-535` | Core BOCPD run-length posterior; produces `prob[t]` (probability) and the current MAP-run-length-drop `is_changepoint[t]` | `detect_changepoints_bocpd` (changepoint.rs:565-585), called by both `ts_detect_changepoints_by` and `ts_detect_changepoints_agg` via `anofox_ts_detect_changepoints_bocpd` FFI |

**Version verification:** No new package versions to verify (bug-fix phase, existing `anofox-forecast` 0.15.10 / `fdars-core` 0.3 pinned versions apply unchanged — per `.claude/CLAUDE.md` Key Dependencies).

## Package Legitimacy Audit

**N/A for this phase.** No new external package is installed. All fixes are implemented against already-vendored crate functions (see table above) plus existing FFI plumbing. If the deseasonalize implementation (BUG-05) discovers it needs a crate-level STL accessor not yet exposed from `anofox-fcst-core`, that is an **in-repo code change**, not a new dependency, and does not require this gate.

## Architecture Patterns

### System Architecture Diagram

```
SQL caller
  │
  ├─ ts_detect_changepoints_by(table, grp, ds, y, MAP{...})          ts_detect_changepoints_agg(ds, y, MAP{...})
  │      │                                                                   │
  │      ▼                                                                   ▼
  │  TsDetectChangepointsByBind/Execute/Finalize                    TsChangepointsAggUpdate/Finalize
  │  (src/table_functions/ts_changepoints.cpp)                      (src/aggregate_functions/ts_changepoints_agg.cpp)
  │      │  parses hazard_lambda via regex ParseHazardLambda()              │  *** currently parses NOTHING from params MAP ***
  │      │  sorts rows by date per group                                   │  sorts rows by timestamp (line 134-145, OK)
  │      ▼                                                                   ▼
  │  anofox_ts_detect_changepoints_bocpd(values, hazard_lambda, incl_probs)  (same FFI fn, same signature)
  │      │                                                                   │
  │      └──────────────────────► detect_changepoints_bocpd (changepoint.rs:565)
  │                                      │
  │                                      ▼
  │                                 bocpd_trace (changepoint.rs:387)
  │                                   - prob[t] = P(run-length==1)           <- changepoint_probability
  │                                   - is_changepoint[t] = (map run-length DROPS)   <- is_changepoint (NOT prob>=threshold today)
  │                                   *** deseasonalize (BUG-05) would subtract an STL seasonal
  │                                       component from `values` BEFORE this call, in Rust ***
  │
  ├─ ts_classify_seasonality_by(table, grp, ds, y, period)
  │      │
  │      ▼
  │  classify_seasonality (seasonality.rs:654)
  │      │  *** passes RAW (undetrended) values straight to fdars ***
  │      ▼
  │  fdars_classify_seasonality(mat, argvals, period, ...)  →  seasonal_strength field (WRONG formula + no detrend)
  │
  │  compare: ts_stats_by → TsStats (stats.rs:307) → MSTL decompose → seasonal_strength_rule(remainder, seasonal)  (CORRECT, reused target)
  │
  └─ ts_mase_by(source, date_col, actual_col, forecast_col, seasonal_period)
         │
         ▼
     _ts_mase_native  (src/table_functions/ts_metrics_native.cpp:555-940)
         │  TsMaseNativeInOut: collects rows per group across chunks (mutex-guarded)
         │  TsMaseNativeFinalize: ALREADY sorts by date_int (line 846-849), splits t0 (first non-NULL forecast)
         ▼
     anofox_ts_mase(eval_actuals, eval_forecasts, insample_values, seasonal_period)
         *** bug persists despite the sort — root cause is upstream (DuckDB plan / row delivery), not this function ***
```

### Pattern: MAP-param parsing is NOT currently centralized for detection functions
**What:** `src/include/ts_forecast_params.hpp` is a shared parser, but its header comment (line 3-16) scopes it explicitly to `ts_forecast_by`/`ts_forecast_agg` ("Both the scalar path ... previously carried their own (divergent) params MAP handling ... This header is the single source of truth for the closed [forecast] params key set"). It does **not** cover changepoints.
**Evidence:** `ts_changepoints.cpp`'s own parser, `ParseHazardLambda` (lines 255-277), is a hand-rolled regex over `params_value.ToString()` — not using `ForecastParamString`/`ForecastParamSet`. `ts_changepoints_agg.cpp` has NO parser at all — `params_vec` is read into a `UnifiedVectorFormat` (line 85) but the comment at line 105 ("Parse params MAP if valid") is never implemented.
**Recommendation:** Build ONE shared parser (new header, e.g. `src/include/ts_changepoints_params.hpp`, same shape as `ts_forecast_params.hpp`) covering `hazard_lambda`, the new `deseasonalize` (bool), the new `threshold` (double, if BUG-06's fix needs one), and `seasonal_period`. Use it from both `ts_changepoints.cpp` and `ts_changepoints_agg.cpp` so they cannot drift again — this directly fixes BUG-06's "params silently ignored" half, and gives `deseasonalize` one parsing location (per CONTEXT's Claude's Discretion note).

### Pattern: STL-based deseasonalize for BUG-05
**What:** No existing function in `crates/anofox-fcst-core` currently exposes "run STL/MSTL at a given period, return only the seasonal component, subtract it from the input, keep trend+remainder." The closest building blocks:
- `detrend_linear` (used by `seasonal_strength_variance` et al.) — removes a LINEAR trend only, not a full STL decomposition; not sufficient alone (CONTEXT requires subtracting "ONLY the seasonal component... so level shifts survive," which needs STL/MSTL's seasonal component specifically, not a linear detrend).
- The MSTL/STL path already wired for `ts_stats_by` / `ts_mstl_decomposition_by` (stats.rs, decomposition.rs) is the right decomposition to reuse — it already separates `trend`/`seasonal`/`remainder`. Subtracting `seasonal` from the raw series and feeding `trend + remainder` into `bocpd_trace` is the described transform.
**Needs a plan task:** locate the exact public decomposition entry point in `crates/anofox-fcst-core/src/decomposition.rs` that returns a `seasonal` component vector given `(values, seasonal_period)` and add a new Rust function (e.g. `deseasonalize_for_changepoints(values, seasonal_period) -> Vec<f64>`) that calls it and returns `values[i] - seasonal[i]`. Auto-detect period when none given — reuse `ts_detect_periods_by`'s underlying Rust function (`periods.rs`) rather than hand-rolling period detection again (`Don't Hand-Roll` below).

### Anti-Patterns to Avoid
- **Re-deriving `is_changepoint` from `changepoint_probability` in C++ as a one-off `if` in `ts_changepoints_agg.cpp` only:** violates "all computation in Rust" / `make audit`. Even though a `>=` comparison looks trivial, CONTEXT explicitly requires it be "the same rule as the table function" — so it must be a function in Rust, called identically by both the table function and the aggregate, not duplicated ad hoc in one C++ file.
- **Assuming BUG-03's fix is "add a sort":** the sort already exists (`ts_metrics_native.cpp:846-849`) and the bug still reproduces on the current build. Treat the first planning task as a diagnostic, not a fix.
- **Hand-rolling period auto-detection inside the deseasonalize path:** `ts_detect_periods_by` already exists and (per BUG-07 research) already returns the correct period on the #287 panel; call its underlying Rust function instead of re-implementing FFT/ACF period detection.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|--------------|-----|
| STL/MSTL decomposition for deseasonalize | A new seasonal-decomposition routine | The existing MSTL/STL path in `crates/anofox-fcst-core/src/decomposition.rs` already used by `ts_stats_by`/`ts_mstl_decomposition_by` | Already correct, already tested against R reference fixtures (v0.10.0 Phase 13/15); reimplementing risks a second divergent decomposition (exactly the bug class BUG-04 is) |
| Period auto-detection for "no seasonal_period given" | A second FFT/ACF period estimator | `periods.rs`'s existing function behind `ts_detect_periods_by` | BUG-07 confirms it already returns period 7 correctly on the #287 panel; reuse, don't reinvent |
| Seasonal-strength formula | A fourth strength implementation (seasonality.rs already has THREE divergent ones: `seasonal_strength_variance`, `seasonal_strength_spectral`, `seasonal_strength_windowed`, plus stats.rs's `seasonal_strength_rule` makes four) | `stats.rs::seasonal_strength_rule` specifically (the one `ts_stats_by` uses, and the one CONTEXT names) | CONTEXT is explicit: "use the SAME seasonal-strength function as `ts_stats_by`" — not variance-based, not spectral, not windowed |
| MAP param parsing for changepoints | Another regex-based ad hoc parser (like the table function's current `ParseHazardLambda`) | A shared typed parser modeled on `src/include/ts_forecast_params.hpp` | The project already paid down this exact tech debt once for forecast params (see that header's own comment describing the "divergent params MAP handling" it replaced); changepoints are now in the same state forecast params used to be in |

**Key insight:** every one of BUG-04/05/06 is, structurally, "a second/third divergent implementation of something the project already has one correct implementation of." The fix pattern across all three is *consolidation onto the existing correct Rust implementation*, not new statistical code.

## Common Pitfalls

### Pitfall 1: Assuming the agg/table-function disagreement in BUG-06 is caused by the `is_changepoint` semantics alone
**What goes wrong:** Fixing only `is_changepoint = prob >= threshold` without also fixing the agg's completely-missing MAP parsing leaves `hazard_lambda` (and the new `deseasonalize`) silently ignored by the aggregate — it will keep using the hardcoded default (`250.0`) while the table function honors the caller's value, so the two will *still* disagree on any query that passes a non-default `hazard_lambda`.
**Why it happens:** The two bugs are independent and both reproduce on the current build (verified above); CONTEXT's prose focuses on one symptom (`is_changepoint` vs threshold) but the live repro shows the param-ignoring bug too.
**How to avoid:** Plan both fixes as part of BUG-06 (or split them, but both must land before the agg is considered fixed).
**Warning signs:** A test that only varies `changepoint_probability` thresholds but never varies `hazard_lambda` through the agg would pass without catching the param-parsing bug.

### Pitfall 2: Changing the table function's `is_changepoint` semantics as a side effect of fixing the agg
**What goes wrong:** Both consumers currently call the SAME Rust function (`detect_changepoints_bocpd`/`bocpd_trace`) and get the SAME `is_changepoint` array (MAP-run-length-drop heuristic). If the fix changes `bocpd_trace`'s own `is_changepoint` field to be probability-threshold-based, the TABLE FUNCTION's default behavior changes too — but Roadmap Phase 20 success criteria only call out the `deseasonalize` option's defaults as needing to stay byte-identical, and say nothing about the table function's `is_changepoint` semantics changing or not changing.
**Why it happens:** CONTEXT's phrase "the same rule as the table function" is ambiguous about whether the table function already implements the target rule (it does not — verified, no `threshold` param exists anywhere in the pipeline) or whether both should be migrated to a new shared rule.
**How to avoid:** Flag this explicitly as an open question for discuss-phase/user confirmation before planning locks in a specific implementation (see Open Questions below) — do not silently decide table-function default behavior may change.
**Warning signs:** A plan task that touches `bocpd_trace`'s `is_changepoint` field without an explicit decision recorded about whether `ts_detect_changepoints_by`'s un-flagged default output (the weekly-trough flags on A/C) is expected to change.

### Pitfall 3: Treating #285's literal-cutoff vs subquery-cutoff flip as noise
**What goes wrong:** The issue body reports literal-cutoff correct, subquery-cutoff NULL. The live repro on the current HEAD build shows the OPPOSITE: literal-cutoff NULL, subquery-cutoff correct. If a fix is validated only against the issue's exact reported symptom (subquery variant), it may "pass" while leaving the literal-cutoff path equally broken, since the underlying cause (row/chunk delivery order from the query plan) is plan-shape-dependent, not query-text dependent.
**Why it happens:** DuckDB's optimizer can choose a different physical plan (parallelism, pipeline breaker placement) between DuckDB versions (issue reported against v1.5.6; repro run against v2.0.0) or even between runs, so "which branch reaches the in_out_function in what order" is not a stable property of the SQL text.
**How to avoid:** The regression test must assert correctness on ALL THREE variants from the issue (literal cutoff, subquery cutoff, subquery cutoff pre-sorted) plus an explicit reversed/shuffled-order variant, not just whichever one the issue happened to report as broken.
**Warning signs:** A test file that only pins the "subquery cutoff" case.

### Pitfall 4: Conflating `seasonal_strength_variance`'s fix with the fix BUG-04 needs
**What goes wrong:** `seasonal_strength_variance` (seasonality.rs:513) already detrends via `detrend_linear` before calling fdars — a tempting one-line "just add the same detrend call to `classify_seasonality`" fix. But CONTEXT specifies the TARGET formula is `ts_stats_by`'s MSTL-remainder-based `seasonal_strength_rule` (stats.rs:528), a *different* algorithm (variance ratio on MSTL remainder/seasonal components, not on a linearly-detrended raw series via fdars's variance-explained measure). Patching `classify_seasonality` to call `detrend_linear` + `fdars_seasonal_strength_variance` would produce a THIRD divergent number, not consistency with `ts_stats_by`.
**Why it happens:** Three similarly-named, similarly-shaped functions exist in the same file; the "nearest" fix by code proximity is not the one CONTEXT asks for.
**How to avoid:** The fix must call (or reproduce) `stats.rs::seasonal_strength_rule` applied to the SAME MSTL decomposition `ts_stats_by` computes, not `seasonality.rs::seasonal_strength_variance`.
**Warning signs:** A diff that touches `classify_seasonality` and adds a `detrend_linear` call but does not touch or call anything from `stats.rs`.

## Runtime State Inventory

Not applicable — this is a bug-fix phase, not a rename/refactor/migration phase. No stored data, live service config, OS-registered state, secrets, or build artifacts carry any of the changed names (BUG-03..07 change computed *values*, not identifiers).

## Code Examples

### The agg's missing MAP-param parsing (the bug itself, as evidence)
```cpp
// Source: src/aggregate_functions/ts_changepoints_agg.cpp:100-108 (read this session)
if (!state.data) {
    state.data = new TsChangepointsAggStateData();
}

if (!state.data->initialized) {
    // Parse params MAP if valid
    // Default values already set in constructor
    state.data->initialized = true;
}
```
`params_vec` (the 3rd aggregate argument, `inputs[2]`) is converted to `UnifiedVectorFormat` at line 85 but never read inside this block — `state.data->hazard_lambda` stays at its constructor default (`250.0`, line 20) for every call, regardless of the caller's `MAP{'hazard_lambda': ...}`.

### The existing table-function MAP parser to model the shared parser on (or replace)
```cpp
// Source: src/table_functions/ts_changepoints.cpp:255-277 (read this session)
static double ParseHazardLambda(const string &params_str) {
    string trimmed = params_str;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) trimmed = trimmed.substr(1);
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
    try { return std::stod(trimmed); } catch (...) {}
    std::regex lambda_regex("hazard_lambda['\"]?\\s*[:=]\\s*['\"]?([0-9.]+)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(params_str, match, lambda_regex)) return std::stod(match[1].str());
    return 250.0;  // Default
}
```

### The BOCPD `is_changepoint` decision (current rule — NOT a probability threshold)
```rust
// Source: crates/anofox-fcst-core/src/changepoint.rs:484-512 (read this session)
let m_t = if p_next.len() > 1 {
    let (idx, _) = p_next[1..].iter().enumerate().fold(
        (0usize, f64::NEG_INFINITY),
        |(bi, bv), (i, &v)| if v > bv { (i, v) } else { (bi, bv) },
    );
    idx + 1
} else { 0 };

prob[t] = if p_next.len() > 1 { p_next[1] } else { 0.0 };   // <- changepoint_probability
map_run_length[t] = m_t;

if let Some(prev) = prev_m {
    if m_t < prev {
        let i = t as isize - m_t as isize + 1;
        if i >= 1 && (i as usize) < n {
            is_changepoint[i as usize] = true;               // <- is_changepoint: NOT prob[t] >= threshold
        }
    }
}
prev_m = Some(m_t);
```

### The seasonal-strength divergence (BUG-04 root cause)
```rust
// Source: crates/anofox-fcst-core/src/seasonality.rs:654,681-690 (read this session)
pub fn classify_seasonality(
    values: &[f64], period: f64,
    strength_threshold: Option<f64>, timing_threshold: Option<f64>,
) -> Result<SeasonalityClassification> {
    // ... validation omitted ...
    let argvals = make_argvals(n);
    let mat = single_series_matrix(values)?;   // <- RAW values, no detrend_linear call
    let result = fdars_classify_seasonality(&mat, &argvals, period, strength_threshold, timing_threshold);
    Ok(result.into())
}
```
```rust
// Source: crates/anofox-fcst-core/src/seasonality.rs:513,534-543 (read this session)
// -- sibling function, SAME bug class, ALREADY fixed here:
pub fn seasonal_strength_variance(values: &[f64], period: f64, n_harmonics: Option<usize>) -> Result<f64> {
    // ...
    // D-12, G19: linearly detrend FIRST -- fdars 0.47's own
    // seasonal_strength_variance measures variance explained on the RAW
    // series, so a trend inflates total variance and crushes the
    // seasonal-share ratio (0.5*t + 2*sin(2*pi*t/12), n=120 measured
    // 0.00296 instead of ~1.0 for the sine alone).
    let detrended = detrend_linear(values)?.detrended;
    let mat = single_series_matrix(&detrended)?;
    Ok(fdars_seasonal_strength_variance(&mat, &argvals, period, harmonics))
}
```
```rust
// Source: crates/anofox-fcst-core/src/stats.rs:524-532 (read this session)
// -- the TARGET formula CONTEXT names as canonical (used by ts_stats_by):
fn seasonal_strength_rule(remainder: &[f64], seasonal: &[f64]) -> Option<f64> {
    let n = remainder.len();
    let remainder_plus: Vec<f64> = (0..n).map(|i| remainder[i] + seasonal[i]).collect();
    let var_rp = sample_var(&remainder_plus);
    let vare = sample_var(remainder);
    // ... NaN/degenerate handling ...
}
```

### MASE's sort (already present — do not re-add, diagnose why it isn't enough)
```cpp
// Source: src/table_functions/ts_metrics_native.cpp:846-860 (read this session)
vector<size_t> indices(grp.dates.size());
for (size_t j = 0; j < indices.size(); j++) indices[j] = j;
std::sort(indices.begin(), indices.end(),
    [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

idx_t t0 = indices.size();
for (idx_t j = 0; j < indices.size(); j++) {
    if (grp.forecast_valid[indices[j]]) { t0 = j; break; }
}
```

## Live Reproduction Evidence (built extension, DuckDB v2.0.0, commit `fc00721`)

All three run via `./build/release/duckdb` with `LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';` against the exact repro SQL from the issues (full session output captured this research session, not reproduced in full here for length — paths to the scratch SQL files used are in Open Questions below for the planner to re-run).

**#287 (defaults):** `ts_detect_changepoints_by(..., MAP{})` flags A at `[2024-01-22, 01-29, 02-05, 02-12, 02-19, 02-26]` (weekly troughs, no real shift in A), C identically, B at `[2024-01-29, 2024-02-20]` (one spurious + the real shift). Matches the issue's "main" description.

**#287 (agg, `hazard_lambda: 50` vs `MAP{}` defaults):** BOTH give `A:6/0.996, B:2/0.996, C:6/0.996` — byte-identical `n_flagged`/`max_prob` regardless of the MAP param, confirming the agg's param parser is a no-op. The table function's own count for `hazard_lambda=50` is `A:8, B:6, C:6` dates — a different count than the agg's `6/2/6`, confirming current agg/table-function disagreement persists (differently shaped from the issue's "release flags 0 everywhere," but still a disagreement).

**#287 (periods):** `ts_detect_periods_by` returns `period=7.0`, `primary_period=7.0` for A, B, C — matches expected; BUG-07 confirmed already fixed, needs only a regression test.

**#286:** `ts_classify_seasonality_by` returns `seasonal_strength=0.1563, is_seasonal=false, cycle_strengths=[1.0]*11`; `ts_stats_by` on the same series returns `seasonality_strength=0.9661`. Matches the issue's "main" description exactly (not the release's NaN — that's `[CITED: #288]` as a separately-tracked, release-only symptom).

**#285:** `literal cut-off` -> NULL for all 3 series; `subquery cut-off` and `subquery cut-off, sorted` -> `0.4765/0.2655/0.4760` for A/B/C (both correct, matching each other). This is the OPPOSITE pairing from the issue body (which reported literal-cutoff correct, subquery-cutoff NULL) — `[VERIFIED: live repro this session]` that the defect is real but plan-shape-dependent, not query-text-dependent.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | BUG-03's root cause is upstream of `_ts_mase_native` (DuckDB plan/row-delivery order), not inside it, since a per-group date sort already exists at ts_metrics_native.cpp:846-849 and the bug still reproduces | Summary, Pitfall 3 | If actually a subtler in-function bug (e.g., `date_int` computed incorrectly per-chunk, or a mutex/threading race despite `SET threads=1` reproducing it), the planner's first "diagnostic" task could still miss the true cause; recommend the plan's first BUG-03 task be EXPLAIN-based tracing, not a direct fix attempt |
| A2 | The right target decision rule for BUG-06's `is_changepoint` is `changepoint_probability >= threshold` computed in a NEW shared Rust function, applied identically to both table function and aggregate (possibly changing the table function's current default output) | Pitfall 2, Open Questions | If the user actually wants the table function's current behavior preserved and ONLY the aggregate changed, the fix shape is different (agg would need its own override logic, which conflicts with "no statistics in C++") — must be confirmed before planning locks this in |
| A3 | `classify_seasonality`'s fix should call/reproduce `stats.rs::seasonal_strength_rule` on an MSTL decomposition of the input series (not `seasonal_strength_variance`) | Pitfall 4 | If CONTEXT's "same seasonal-strength function as ts_stats_by" was loosely worded and any detrended Hyndman-style strength is acceptable, over-specifying to exactly `seasonal_strength_rule` adds unnecessary refactor scope; low risk since the formula CONTEXT quotes verbatim (`max(0, 1 − Var(R)/Var(S+R))`) matches `seasonal_strength_rule`'s formula shape, not `seasonal_strength_variance`'s |
| A4 | No existing public Rust function returns "only the seasonal component" of an MSTL decomposition in a form directly reusable for the deseasonalize transform — a small new Rust function will be needed, not just a reused one | Architecture Patterns (STL deseasonalize) | Not independently verified by opening `decomposition.rs` this session (budget-limited) — the planner should verify the exact public signature in `crates/anofox-fcst-core/src/decomposition.rs` before estimating this as a "small" addition |

## Open Questions

1. **Does fixing BUG-06's `is_changepoint` rule change `ts_detect_changepoints_by`'s (the table function's) default output?**
   - What we know: both consumers currently call the same Rust function (`detect_changepoints_bocpd`) and get the same MAP-run-length-drop-based `is_changepoint`. No `threshold` parameter exists anywhere in the pipeline today.
   - What's unclear: whether CONTEXT's "the same rule as the table function" means the table function is assumed to already be correct (it is not, by the `prob>=threshold` definition) or whether both consumers should migrate to a new shared rule, changing both outputs.
   - Recommendation: surface this to discuss-phase/user explicitly before planning. Roadmap Phase 20 success criterion #4 only mentions the aggregate's `is_changepoint`; it is silent on whether the table function's weekly-trough-flagging default behavior (success criterion #1, which is about `deseasonalize`, not about the baseline `is_changepoint` rule) is allowed to shift as a side effect.

2. **What threshold value should the new `is_changepoint = prob >= threshold` rule use?**
   - What we know: "no unflagged row at p = 0.996" is the only hard constraint from CONTEXT/Roadmap.
   - What's unclear: whether `threshold` should be a new MAP param (with what default) or a hardcoded Rust constant.
   - Recommendation: make it a MAP param (e.g., `threshold`, default `0.5` — the common BOCPD run-length-reset convention) parsed by the new shared changepoints params header; document the default explicitly since it's a new, currently-absent knob.

3. **Exact public signature for extracting an MSTL seasonal-only component in `decomposition.rs`.**
   - What we know: the project already has an MSTL/STL path (reused by `ts_stats_by`, `ts_mstl_decomposition_by`); `[ASSUMED]` not opened this session.
   - What's unclear: whether it returns `seasonal` as a standalone accessible field/function, or only as part of a larger decomposition struct that would need extending.
   - Recommendation: planner's first BUG-05 task should open `crates/anofox-fcst-core/src/decomposition.rs` and `stats.rs`'s MSTL call site to confirm the exact struct/function shape before estimating effort.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | DuckDB sqllogictest (`.test` files), native build harness (`make test` / `build/release/duckdb -unittest`) |
| Config file | none — convention-based: `test/sql/correctness/*.test`, `require anofox_forecast` header |
| Quick run command | `./build/release/duckdb -unittest --test-dir test/sql/correctness '*metrics.test'` (per-file) |
| Full suite command | `make test` (native); inventory gate `python3 scripts/check_correctness_inventory.py` (requires built `build/release/duckdb`) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| BUG-03 | `ts_mase_by` identical result for date-sorted, reversed, shuffled, and the #285 UNION ALL + scalar-subquery shape | sqllogictest value pin | `./build/release/duckdb -unittest --test-dir test/sql/correctness 'metrics.test'` | ✅ `test/sql/correctness/metrics.test` exists, extend it |
| BUG-04 | `ts_classify_seasonality_by` strength consistent with `ts_stats_by` on the #286 weekly series | sqllogictest value pin + cross-function consistency assertion | same file, `detection_decomposition.test` | ✅ `test/sql/correctness/detection_decomposition.test` exists, extend it |
| BUG-05 | `deseasonalize:=true` flags only B at 2024-02-20 on the #287 panel; `deseasonalize:=false` (default) byte-identical to pre-fix | sqllogictest value pin (regression pin for the default, new pin for the option) | same file | ✅ extend `detection_decomposition.test` |
| BUG-06 | agg `is_changepoint` == `changepoint_probability >= threshold` for every row, including p=0.996 case; agg honors `hazard_lambda`/other MAP params | sqllogictest value pin, including a param-sensitivity assertion (agg output changes when `hazard_lambda` changes) | same file | ✅ extend `detection_decomposition.test` |
| BUG-07 | `ts_detect_periods_by` returns period 7 on #287 panel | sqllogictest regression pin | same file | ✅ extend `detection_decomposition.test` (new pin; function already correct) |

### Sampling Rate
- **Per task commit:** targeted single-file sqllogictest run (`-unittest --test-dir test/sql/correctness '<file>.test'`) plus `cargo test` for any touched `crates/anofox-fcst-core` module, plus `cargo fmt --check` (project memory: rustfmt gates CI and subagent-written Rust is often not fmt-clean).
- **Per wave merge:** `make test` (full native suite) + `python3 scripts/check_correctness_inventory.py` (requires the function names to appear in the CHANGELOG `## [Unreleased]` section AND be exercised in `test/sql/correctness/*.test` — both must be updated together) + `make audit` (no statistics in C++).
- **Phase gate:** full suite green, `make audit` green, `cargo fmt --check` green, `cargo clippy --all-targets -- -D warnings` green, before `/gsd-verify-work`.

### Wave 0 Gaps
None — existing test infrastructure (`test/sql/correctness/metrics.test`, `detection_decomposition.test`) already covers the affected functions; this phase extends those files rather than creating new ones. The inventory gate (`scripts/check_correctness_inventory.py`) requires each touched function name to be backticked in `CHANGELOG.md`'s `## [Unreleased]` section — the plan must update CHANGELOG.md as part of each fix, not just the test file, or the gate will flag it as uncovered.

## Security Domain

Not applicable — `security_enforcement` is not referenced for this project's config, and this phase touches pure numerical/statistical computation (no auth, session, input trust boundary, or cryptography surface). The ASVS categories (V2/V3/V4/V6) do not apply; V5 (Input Validation) is already handled by the existing MAP-param validation patterns being extended, not newly introduced.

## Sources

### Primary (HIGH confidence — opened this session)
- `crates/anofox-fcst-core/src/changepoint.rs` (lines 69-78, 208-535, 543-587) — BOCPD trace, `is_changepoint` decision rule, `ChangepointResult`/`BocpdTrace` structs
- `crates/anofox-fcst-core/src/seasonality.rs` (lines 1-6, 513-691) — `seasonal_strength_variance`/`_spectral`/`_windowed`, `classify_seasonality`
- `crates/anofox-fcst-core/src/stats.rs` (lines 307-373, 487-595) — `TsStats`, `seasonal_strength_rule`
- `src/table_functions/ts_metrics_native.cpp` (lines 71-932) — `_ts_metrics_native`, `_ts_mase_native` bind/InOut/Finalize
- `src/table_functions/ts_changepoints.cpp` (lines 187-600) — table function bind/finalize, `ParseHazardLambda`
- `src/aggregate_functions/ts_changepoints_agg.cpp` (full file, 292 lines) — agg Update/Finalize/Combine, confirmed missing param parse
- `src/include/ts_forecast_params.hpp` (lines 1-60) — shared-params-header precedent and its own documented history of the same "divergent handling" bug class
- `src/macros/ts_macros.cpp:2518-2531` — `ts_mase_by` macro definition
- `test/sql/correctness/metrics.test`, `test/sql/correctness/detection_decomposition.test` (headers) — test-file conventions
- `scripts/check_correctness_inventory.py` (docstring + logic) — inventory gate mechanics
- Live reproductions against `./build/release/duckdb` (commit `fc00721`, DuckDB v2.0.0) for all three issues' exact repro SQL — see "Live Reproduction Evidence" above
- `gh issue view 285/286/287 -R DataZooDE/anofox-forecast` — issue bodies, exact repro SQL and expected/observed outputs

### Secondary (MEDIUM confidence)
- `.planning/phases/20-detection-metric-bug-fixes/20-CONTEXT.md`, `.planning/REQUIREMENTS.md`, `.planning/ROADMAP.md` — locked decisions and acceptance criteria (authoritative for scope, not independently re-verified against crate internals)

### Tertiary (LOW confidence)
- None — every claim above is either a locked decision, an opened-file citation, or a live repro; no claim here rests on unverified training-data recall about this specific codebase.

## Metadata

**Confidence breakdown:**
- BUG-04 root cause and fix target: HIGH — exact file:line, matching historical precedent comment in the same file, live repro confirms symptom
- BUG-06 root cause (both halves) and fix shape: HIGH for "what's broken," MEDIUM for "exact fix shape" (threshold value and table-function-default-change question are open, flagged for discuss-phase)
- BUG-05 (deseasonalize) building blocks: MEDIUM — STL/MSTL reuse path identified but exact `decomposition.rs` signature not opened this session (budget-limited); flagged as Open Question 3
- BUG-07: HIGH — directly reproduced correct on current build
- BUG-03 root cause: MEDIUM — the "sort already exists" finding is HIGH confidence (file read, live repro), but the TRUE upstream root cause (DuckDB plan/row-delivery mechanics) is not pinned down; flagged as Assumption A1 / needs a diagnostic-first plan task

**Research date:** 2026-10-23
**Valid until:** This phase (single-milestone bug fix; re-verify against HEAD before executing if more than ~7 days pass, since Phase 19 commits landed same day as this research and the crate/build surface is actively moving)
