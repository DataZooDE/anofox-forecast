# Requirements: anofox-forecast — v0.8.0 Ensemble Forecasting

**Defined:** 2026-08-30
**Core Value:** SQL users can combine multiple forecasting models per series — automatically or explicitly — with distribution-free prediction intervals and weight introspection, all without leaving DuckDB.

## v1 Requirements

Requirements for the v0.8.0 milestone. Each maps to a roadmap phase.

### ENS — Ensemble surfaces

- [ ] **ENS-01**: User can produce an **AutoEnsemble** forecast per series — the extension auto-fits ARIMA/ETS/Theta families, ranks them, and combines the top-K; user sets `top_k`, `combination_method`, and `seasonal_period`.
- [ ] **ENS-02**: User can produce an **explicit-member** ensemble forecast per series by naming the member models and a combination method; the extension fits each member and combines them.

### COMB — Combination methods

- [ ] **COMB-01**: User can select **Mean** or **Median** combination for an ensemble.
- [ ] **COMB-02**: User can select **WeightedMSE** or **InverseAIC** (error/information-weighted) combination for an ensemble.
- [ ] **COMB-03**: User can select **Stacking** (learned non-negative weights on an in-sample holdout) combination for an ensemble.
- [ ] **COMB-04**: User can select **HorizonAdaptive** (per-horizon-step weights from rolling-origin errors) combination for an ensemble.

### EPI — Ensemble prediction intervals

- [ ] **EPI-01**: User can attach distribution-free prediction intervals to an ensemble point forecast via the existing conformal path (learn + apply).

### INSP — Ensemble introspection

- [ ] **INSP-01**: User can inspect which member models an ensemble selected and their combination weights, per series.

## Future Requirements

Acknowledged but deferred beyond v0.8.0.

### ENS (future)

- **ENS-F1**: Custom hand-supplied combination weights (`CombinationMethod::Custom`) — user passes an explicit weight vector.
- **ENS-F2**: Ensembling the panel/global (`ts_forecast_panel_by`) or multivariate (`ts_forecast_var_by`) surfaces — cross-series / cross-variable ensembles.

## Out of Scope

Explicitly excluded from v0.8.0. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Panel / multivariate ensembling | This milestone is per-series dispatch mirroring `ts_forecast_by`; panel/VAR ensembles are a distinct I/O shape, deferred (ENS-F2). |
| Custom hand-supplied weights | The six auto/learned combination methods cover the milestone; user-weight passthrough deferred (ENS-F1). |
| New crate feature flags | Ensemble APIs live under already-enabled crate features; staying on `anofox-forecast` 0.15.3, no flag changes. |

## Verification (Definition of Done)

Ensembles have no external reference library (unlike statsmodels/arch parity in v0.7.0). DoD per requirement:

- Runnable, verified `examples/*.sql` against the built extension (established project rule).
- **Internal-consistency cross-check**: combined forecast equals the manual weighted combination of member forecasts computed independently (SQL or Python), verifying each `CombinationMethod`'s math against hand-computed weights.
- `docs/api/` + `docs/reference/models/` updated; every doc SQL snippet run through the built extension (PR #230 rule).
- Clean-machine load verified across Linux/macOS/Windows + WASM; OpenSSL stays statically linked.

## Traceability

Which phases cover which requirements. Populated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| ENS-01 | Phase 4 | Pending |
| COMB-01 | Phase 4 | Pending |
| COMB-02 | Phase 4 | Pending |
| COMB-03 | Phase 4 | Pending |
| COMB-04 | Phase 4 | Pending |
| ENS-02 | Phase 5 | Pending |
| EPI-01 | Phase 6 | Pending |
| INSP-01 | Phase 6 | Pending |

**Coverage:**
- v1 requirements: 8 total
- Mapped to phases: 8 ✓
- Unmapped: 0

---
*Requirements defined: 2026-08-30*
*Last updated: 2026-08-30 after roadmap creation (Phases 4-6)*
