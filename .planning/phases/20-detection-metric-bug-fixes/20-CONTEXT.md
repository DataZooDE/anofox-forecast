# Phase 20: Detection & Metric Bug Fixes - Context

**Gathered:** 2026-10-23
**Status:** Ready for planning

<domain>
## Phase Boundary

Correct answers, independent of row order, on the data from #285, #286 and #287. Requirements: BUG-03, BUG-04, BUG-05, BUG-06, BUG-07. Issue bodies: GitHub #285, #286, #287 (each contains exact repro SQL and expected outcomes; use them as fixtures).

</domain>

<decisions>
## Implementation Decisions

### #287: changepoints (BUG-05, BUG-06, BUG-07)
- OPT-IN, not default (user decision): add a `deseasonalize` option (default false) to `ts_detect_changepoints_by` and `ts_detect_changepoints_agg` (MAP param, consistent with existing params such as hazard_lambda). When true: estimate the seasonal component with STL at the given `seasonal_period` param, or the auto-detected period if none is given, and subtract ONLY the seasonal component (keep trend and level) before BOCPD, so level shifts survive. Default results must stay byte-identical to the current behaviour; pin that with a test.
- Agg consistency: in `ts_detect_changepoints_agg`, `is_changepoint` must be true exactly when `changepoint_probability >= threshold` (the same rule as the table function). Fix the agg's decision rule; no unflagged row at p = 0.996.
- `ts_detect_periods_by` returns period 7 on the #287 panel: verify it on main and pin it with a regression test (already fixed on main per #287, release-only bug).
- Fixture: the #287 panel (3 series × 84 days; B has +15 from day 50 = 2024-02-20). With deseasonalize: no flags on A/C, B flagged at 2024-02-20 (allow the documented BOCPD detection-lag tolerance only if justified by a reference).
- All computation in Rust (crates/anofox-fcst-core); C++ only marshals (make audit).

### #286: seasonality strength (BUG-04)
- One shared Rust implementation: `ts_classify_seasonality_by` / `_agg` must use the SAME seasonal-strength function as `ts_stats_by` (Hyndman `max(0, 1 − Var(R)/Var(S+R))` on the detrended STL decomposition). Remove the divergent computation.
- `is_seasonal` keeps its existing threshold, applied to the corrected strength. The #286 weekly series → seasonal; never NaN on a regular complete daily series; `seasonal_strength` consistent with `cycle_strengths`.
- Reference check against R (`forecast`/`feasts` STL strength) or the existing v0.10.0 STL-strength fixtures (validation/reference), per the project's reference-fixture rule.

### #285: row order (BUG-03)
- `_ts_mase_native` sorts by date within each series before computing the in-sample naive scale.
- AUDIT every native metric / `_by` table function and aggregate for the same row-order dependence (anything that assumes input arrives date-sorted). Fix each one found, and pin every metric with a test that feeds date-ordered, reversed and shuffled input (including the #285 UNION ALL + scalar-subquery shape) and asserts identical results.

### Claude's Discretion
- Exact option name spelling (`deseasonalize` preferred) and its MAP parsing location (shared params header).
- How the shuffle test is generated (deterministic seed).

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `crates/anofox-fcst-core/src/{changepoint,seasonality,stats,periods}.rs`: Rust implementations; STL strength already used by ts_stats_by (v0.10.0 Phase 13/15).
- `src/table_functions/ts_changepoints.cpp`, `src/aggregate_functions/ts_changepoints_agg.cpp`, `src/table_functions/ts_seasonality.cpp`, `src/table_functions/ts_periods.cpp`.
- `ts_mase_by` → `_ts_mase_native` (src/macros/ts_macros.cpp:2523); metric natives under src/table_functions.
- `test/sql/correctness/*` suite and the inventory gate (scripts/check_correctness_inventory.py).

### Established Patterns
- Statistics only in Rust; `make audit` enforces it. Reference fixtures in validation/reference (R + uv Python generators).
- NULL handling via validity; optional NULL args need SPECIAL_HANDLING (memory).

### Integration Points
- Shared params header for MAP options; docs/api + skills (detection, backtest) must document the new option; `make check-docs`.

</code_context>

<specifics>
## Specific Ideas

- Use the exact repro SQL from #285/#286/#287 as sqllogictests in test/sql/correctness/.

</specifics>

<deferred>
## Deferred Ideas

- Making deseasonalize the default for changepoints (user chose opt-in; revisit later if users ask).

</deferred>

<decisions_addendum>
## Decisions added after research (2026-10-23)

### #287 is_changepoint vs probability (BUG-06), user decision: opt-in threshold
- `ts_detect_changepoints_agg` must parse its `params` MAP (today it silently ignores it; research 20-RESEARCH.md) and mirror `ts_detect_changepoints_by` EXACTLY: same heuristic, same defaults, same parameters. With identical inputs and params, both give identical is_changepoint/probability. Default outputs of both functions stay unchanged.
- Add a new optional `threshold` param (default: unset) to both. When set, is_changepoint = (changepoint_probability >= threshold), decided in Rust and shared by both consumers. When unset, the existing heuristic applies.
- The #287 acceptance check for "no unflagged row at p = 0.996" is evaluated with `threshold` set (e.g. 0.5), plus a test proving that agg == table function with defaults.

### #285 (BUG-03)
- Research shows the per-series sort already exists in `_ts_mase_native` (ts_metrics_native.cpp:846-849) and the bug still reproduces (literal-cutoff case now NULL). Plan a diagnostic-first task: find the true cause (row/chunk delivery across UNION ALL + subquery, finalize/combine handling, or the date-based in-sample split) before fixing. The fix must make every input order give identical MASE, pinned by sorted, reversed, shuffled and both #285 shapes.
</decisions_addendum>

<decisions_addendum_2>
## Decisions after planning (2026-10-23, user)

Chosen: D1-b, D2-ii
- D1-b: `deseasonalize := true` removes the STL seasonal component AND a straight-line (linear) trend before BOCPD, so level shifts stay as steps. It is still opt-in; defaults are unchanged.
- D2-ii: when `threshold` is set, is_changepoint = (changepoint_probability >= threshold) for every row AFTER THE FIRST of each series. The first row is never flagged (its probability is always 1 − 1/hazard_lambda). Roadmap criterion 4 reads "every row after the first".
- UNION ALL scope expansion: the same UNION-ALL truncation fix also applies, in Phase 20, to `ts_aggregate_hierarchy`, `ts_split_keys`, `ts_validate_separator` and the internal `_ts_forecast_native` (with the forecast macros that use it), with the same reversed/shuffled/UNION-ALL pin tests. Requirement BUG-03 covers it. Remove these from deferred-items.md.
</decisions_addendum_2>
