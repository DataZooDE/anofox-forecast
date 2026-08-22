---
phase: 03-classical-multivariate-models
reviewed: 2026-08-22T00:00:00Z
depth: standard
files_reviewed: 1
files_reviewed_list:
  - crates/anofox-fcst-core/src/forecast.rs
findings:
  critical: 0
  warning: 0
  info: 0
  total: 0
status: clean
---

# Phase 03: Code Review Report (Iteration 3 — Final Fix Verification)

**Reviewed:** 2026-08-22
**Depth:** standard
**Files Reviewed:** 1
**Status:** clean

## Summary

Narrowly scoped iteration-3 review verifying the two fixes called for in iteration 2:

1. **CR-01** — `forecast_with_exog()` must carry the same GARCH|Kalman guard as `forecast()`
   so neither entry point produces spurious synthetic confidence intervals for those models.
2. **WR-01** — `list_models()` doc comment must say "35 models" to match the actual vector length.

Both fixes are correctly implemented. No new critical or warning defects were introduced. All
reviewed files meet quality standards.

---

## Fix Verification

### CR-01 — CI guard now present in `forecast_with_exog()` (lines 930–933)

```rust
// forecast_with_exog(), lines 930–933
let (lower, upper) = match options.model {
    ModelType::GARCH | ModelType::Kalman => (vec![], vec![]),
    _ => calculate_confidence_intervals(&result.point, &clean_values, options.confidence_level),
};
```

Correct. The guard is structurally identical to the one already present in `forecast()` at
lines 742–745. Exhaustive path analysis confirms GARCH/Kalman cannot reach
`calculate_confidence_intervals` through any call path:

- **`forecast()` entry (line 742):** Guarded — verified in iteration 2, still correct.
- **`forecast_with_exog()` entry (line 930):** Now guarded — this was the missing branch.
- **`forecast_with_model()` (called from `forecast_with_exog()` when no exog data or model
  does not support exog, line 911):** This helper dispatches to `forecast_garch` /
  `forecast_kalman` whose returned `ForecastOutput` structs have `lower: vec![]` and
  `upper: vec![]` by construction. The helper does not call `calculate_confidence_intervals`
  at all; its return value flows into `result` in `forecast_with_exog()`, after which the
  guard at line 930 runs (returning `(vec![], vec![])` again — redundant but harmless).

No panic or length-mismatch risk: empty lower/upper vecs propagate correctly through the
allocation layer (empty slice → `null_mut()`) and are null-guarded on both C++ callsites.
No over-gating: all other models still reach `calculate_confidence_intervals`.

### WR-01 — `list_models()` doc comment count matches vector length (line 2781)

```rust
/// List all available model names (35 models matching C++ extension).
pub fn list_models() -> Vec<String> {
```

Doc comment now reads "35 models". Counting the actual entries in the returned vector:

| Group | Entries | Count |
|---|---|---|
| Automatic Selection | AutoETS, AutoARIMA, AutoTheta, AutoMFLES, AutoMSTL, AutoTBATS | 6 |
| Basic | Naive, SMA, SeasonalNaive, SES, SESOptimized, RandomWalkDrift | 6 |
| Exponential Smoothing | Holt, HoltWinters, SeasonalES, SeasonalESOptimized, SeasonalWindowAverage | 5 |
| Theta (non-auto) | Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta | 4 |
| State Space (non-auto) | ETS | 1 |
| ARIMA (non-auto) | ARIMA | 1 |
| Multiple Seasonality (non-auto) | MFLES, MSTL, TBATS | 3 |
| Intermittent Demand | CrostonClassic, CrostonOptimized, CrostonSBA, ADIDA, IMAPA, TSB | 6 |
| Distributional | Laplace | 1 |
| Classical | GARCH, Kalman | 2 |
| **Total** | | **35** |

Doc comment matches actual vector length. Fix is correct.

---

_Reviewed: 2026-08-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Iteration: 3 (final fix verification)_
