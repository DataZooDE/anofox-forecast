# Requirements: anofox-forecast v0.11.0 Issue Sweep: Contract, Bugs, Crate Fixes

**Defined:** 2026-10-22
**Core Value:** SQL users can produce, validate, combine and interval-bound time-series forecasts entirely within DuckDB. Every statistic is correct and computed in Rust.
**Milestone goal:** close all 15 open GitHub issues (#227, #258, #259, #272, #279-#289).

## v1 Requirements

### Bugs

- [x] **BUG-01**: `ts_fill_forward_operator` no longer runs a nested query that hangs (native) or traps (WASM). It becomes internal (underscore name, undocumented) or is removed; if it is kept, its identifiers are quoted and `frequency` is honoured. `ts_fill_forward_by` is unaffected. (#289)
- [x] **BUG-02**: `ts_features_list()` and `ts_features_config_template()` run in DuckDB-Wasm (wasm_eh) without trapping and return 117 rows. (#289)
- [ ] **BUG-03**: `ts_mase_by` returns the same value whatever the input row order: it sorts by date within each series before computing the in-sample naive scale. (#285)
- [x] **BUG-04**: `ts_classify_seasonality_by` classifies a strong weekly series as seasonal; `seasonal_strength` is consistent with its own `cycle_strengths` and with `ts_stats_by`. (#286)
- [ ] **BUG-05**: `ts_detect_changepoints_by` / `_agg` gain an opt-in `deseasonalize := true` option (user decision 2026-10-23: defaults unchanged). With it, the troughs of a regular seasonal cycle are not flagged, and a real level shift is still found (the #287 panel: none on A/C, B on 2024-02-20). (#287)
- [x] **BUG-06**: In `ts_detect_changepoints_agg`, `is_changepoint` agrees with `changepoint_probability` and the threshold. (#287)
- [x] **BUG-07**: `ts_detect_periods_by` finds period 7 on the #287 panel, pinned by a regression test. (#287)

### Distribution

- [x] **DIST-01**: `scripts/extension-upload.sh` produces WASM artifacts with exactly one `duckdb_signature` custom section: the placeholder signature is replaced, not followed by a second section. (#272)
- [x] **DIST-02**: A check after upload downloads each artifact, counts its `duckdb_signature` sections (must be 1), and LOADs the WASM build in DuckDB-Wasm on Node. (#272)
- [x] **DIST-03**: The `wasm_mvp` build LOADs in DuckDB-Wasm, or the `_setThrew is not defined` root cause is documented and a decision recorded (fix, or stop shipping wasm_mvp). (#272)
- [x] **DIST-04**: The exact get.erpl.io CORS rule (GET/HEAD from `*`) and the list of stale old-version paths are documented and handed to the owner. (#272)
- [ ] **DIST-05**: The community registry serves the current release (`INSTALL anofox_forecast FROM community` reports the current version on v2.0.0 and v1.5.6), verified with FORCE INSTALL, and #288 is closed. (#288)

### Output contract (anofox-visualization `integration-contract.md`)

- [ ] **CTR-01**: `anofox_contract_version()` is registered and returns `'1'`. (#279)
- [ ] **CTR-02**: Every result producer can be discovered via `tags['anofox.output']` and `tags['anofox.contract']` in `duckdb_functions()`, following anofox-statistics' pattern. (#279)
- [ ] **CTR-03**: Conformal outputs (`ts_conformal_by`, `ts_conformal_apply_by`, `ts_conformal_calibrate`, `ts_interval_width_by`) keep the caller's group and date column names and use `yhat` / `yhat_lower` / `yhat_upper`. Delivered additively: existing columns stay unchanged permanently (API-01); the contract names come as new columns, a new function or an opt-in parameter. (#280)
- [ ] **CTR-04**: `ts_forecast_exog_by` output can be consumed with a `ds` column, added additively; the existing `date` column stays (permanent compatibility, API-01). (#280)
- [ ] **CTR-05**: `ts_forecast_var_by` returns the `ts_forecast_by` columns (`id` = the variable, `yhat`, plus intervals where the model provides them), delivered additively, with the existing columns kept permanently (API-01). (#280)
- [ ] **CTR-06**: Every forecast and conformal output that has an interval carries a `level` column. (#280)
- [ ] **CTR-07**: A long-form `ts_metrics_by` returns one row per (id, model, metric, value), matching the contract's `summary` schema, alongside the existing wide macros. (#281)
- [ ] **CTR-08**: Long-form companions carrying `id` and `ds` exist for `ts_forecast`, `ts_forecast_agg`, `ts_mstl_decomposition_by`, `ts_forecast_explain_by`, `ts_detrend_by`, `ts_detect_peaks_by` and `ts_detect_periods_by`. (#282)
- [ ] **CTR-09**: `ts_forecast_explain_by` fills its `residual` component. (#282)
- [ ] **CTR-10**: `ts_forecast_inspect_by` with AutoARIMA returns fitted values, residuals and named coefficients (ar1, ma1, …). (#282)
- [ ] **CTR-11**: Changepoints are available as an events output, one row per changepoint, in the contract's `events` schema (`id, ds, kind = 'changepoint', severity, value`). (#283)
- [ ] **CTR-12**: `ts_durbin_watson_by` (and the `dw_*` fields of `ts_residual_diagnostics_by`) return a documented approximate `p_value` and `method = 'Durbin-Watson'`. (#284)

### API compatibility (user directive 2026-10-23)

- [ ] **API-01**: `docs/API_STABILITY.md` defines the compatibility policy: no public name, parameter, parameter type or output column is ever removed, renamed or retyped; changes are additive only; deprecations are permanent working aliases; `_`-prefixed names are internal.
- [ ] **API-02**: A checked-in API surface snapshot (public functions/macros/aggregates with parameters and types, plus table-function/macro output schemas), baselined on release v2026.10.22, and a CI gate that fails on removals, renames, retypes or changed output columns, and requires an API CHANGELOG entry for additions.
- [ ] **API-03**: Earlier removal plans reversed: `ts_fill_forward_operator` and all #280 old names are permanent aliases; no "removed next release" wording remains.
- [ ] **API-04**: The milestone's changes (Phases 19-20) pass the gate against the v2026.10.22 baseline; any break is fixed, not waived.

### Upstream crates

- [ ] **CRATE-01**: AutoETS (crate anofox-forecast) suppresses trend initialisation on spiky or intermittent series: on the spike-runaway fixture (10 series, period 12, spike share ≈ 0.25), the mean forecast level stays at or below 2.0× the spike-robust median. (#259)
- [ ] **CRATE-02**: The fold-level ridge solver (crate anofox-regression) chooses λ according to the design matrix's conditioning: a small λ for well-conditioned folds, λ = 1.0 when ill-conditioned. (#258)
- [ ] **CRATE-03**: The regression feature engineering (lags, auto_lags, trend_type, differencing, Fourier, rolling, exog features, event_distance) is exposed as standalone, streaming-friendly builders (`(series, t, cfg) -> feature row`, writing into a reusable scratch row), used by `RegressionForecaster` and callable from a global or panel fit. (#227)
- [ ] **CRATE-04**: The crate releases are published to crates.io and consumed here with no `[patch]`, git or path overrides, and the existing R-parity and correctness suites still pass.

### Release

- [ ] **REL-01**: The CHANGELOG (including the deprecation note for the #280 aliases) and release notes are written, a CalVer release is cut from a main merge commit, the community descriptor is bumped, and every issue is closed with a reference to its fixing PR.

## Future Requirements

- Remove `ts_fill_forward_operator` and its `anofox_fcst_ts_fill_forward_operator` alias (the release after v0.11.0's release, with the #280 aliases). (#289)
- v0.10.0 tech-debt candidates carried in PROJECT.md (integer `frequency` casts, params-header misuse checks, strict numeric MAP parsing, and so on).

## Out of Scope

| Feature | Reason |
|---------|--------|
| Applying the get.erpl.io S3/CDN CORS rule and deleting stale paths | Owner action (decision 2026-10-22); the milestone only documents the exact changes |
| Dropping DuckDB 1.4.x LTS | Owned by jrosskopf's PR #292; not touched here |
| DuckDB-Wasm runtime test on v2.0.0 | No @duckdb/duckdb-wasm release bundles engine v2.0.0 yet (checked 2026-10-22) |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| API-01 | Phase 20.1 | Pending |
| API-02 | Phase 20.1 | Pending |
| API-03 | Phase 20.1 | Pending |
| API-04 | Phase 20.1 | Pending |
| BUG-01 | Phase 19 | Complete |
| BUG-02 | Phase 19 | Complete |
| DIST-01 | Phase 19 | Complete |
| DIST-02 | Phase 19 | Complete |
| DIST-03 | Phase 19 | Complete |
| DIST-04 | Phase 19 | Complete |
| BUG-03 | Phase 20 | Pending |
| BUG-04 | Phase 20 | Complete |
| BUG-05 | Phase 20 | Pending |
| BUG-06 | Phase 20 | Complete |
| BUG-07 | Phase 20 | Complete |
| CRATE-01 | Phase 21 | Pending |
| CRATE-02 | Phase 21 | Pending |
| CRATE-03 | Phase 22 | Pending |
| CTR-01 | Phase 23 | Pending |
| CTR-03 | Phase 23 | Pending |
| CTR-04 | Phase 23 | Pending |
| CTR-05 | Phase 23 | Pending |
| CTR-06 | Phase 23 | Pending |
| CTR-02 | Phase 24 | Pending |
| CTR-07 | Phase 24 | Pending |
| CTR-08 | Phase 24 | Pending |
| CTR-09 | Phase 24 | Pending |
| CTR-11 | Phase 24 | Pending |
| CTR-12 | Phase 24 | Pending |
| CRATE-04 | Phase 25 | Pending |
| CTR-10 | Phase 25 | Pending |
| DIST-05 | Phase 25 | Pending |
| REL-01 | Phase 25 | Pending |

**Coverage:**

- v1 requirements: 33 total
- Mapped to phases: 29 (Phases 19-25)
- Unmapped: 0

---
*Requirements defined: 2026-10-22*
*Last updated: 2026-10-22 after roadmap creation (traceability filled)*
