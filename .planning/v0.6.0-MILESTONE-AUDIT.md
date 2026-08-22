---
milestone: "Close the Crate→Extension Gap (Diagnostics + Model Coverage)"
audited: 2026-08-22
status: passed
scores:
  requirements: 13/13
  phases: 3/3
  integration: 5/5
  flows: 5/5
gaps:
  requirements: []
  integration: []
  flows: []
tech_debt:
  - phase: cross-milestone
    items:
      - "Deferred (intentional): prediction intervals for panel/global (Phase 2) and GARCH/Kalman/VAR (Phase 3) — routed to the existing conformal path in a later increment, not built into these surfaces."
      - "Deferred (intentional): VAR automatic lag-order selection (AIC/BIC) — explicit `order`/`p` param only in v1."
      - "Deferred (intentional): per-panel VAR (group_col fan-out) and GARCH advanced coefficient overrides beyond p/q."
  - phase: "01"
    items:
      - "INTER-01 (intermittent-demand ADI/CV² classification) descoped — user has a more advanced approach, to be specified/scheduled separately."
  - phase: minor
    items:
      - "2 Info-severity code-review findings (Phase 3) left unaddressed by design (critical_warning fix scope); non-blocking."
      - "SUMMARY key-files parser occasionally misreads verify-command strings as filenames → benign phase.complete warnings; artifacts confirmed present by the verifiers."
---

# Milestone Audit — Close the Crate→Extension Gap (Diagnostics + Model Coverage)

**Status: PASSED** · 13/13 requirements satisfied · 3/3 phases verified · cross-phase integration FULLY WIRED

## Requirements Coverage (3-source cross-referenced)

| REQ-ID | Phase | VERIFICATION | SUMMARY | Traceability | Final |
|--------|-------|--------------|---------|--------------|-------|
| STAT-01 | 1 | passed | listed | Complete | ✅ satisfied |
| STAT-02 | 1 | passed | listed | Complete | ✅ satisfied |
| STAT-03 | 1 | passed | listed | Complete | ✅ satisfied |
| RESID-01 | 1 | passed | listed | Complete | ✅ satisfied |
| RESID-02 | 1 | passed | listed | Complete | ✅ satisfied |
| RESID-03 | 1 | passed | listed | Complete | ✅ satisfied |
| RESID-04 | 1 | passed | listed | Complete | ✅ satisfied |
| GLOB-01 | 2 | passed | listed | Complete | ✅ satisfied |
| GLOB-02 | 2 | passed | listed | Complete | ✅ satisfied |
| GLOB-03 | 2 | passed | listed | Complete | ✅ satisfied |
| CLAS-01 | 3 | passed | listed | Complete | ✅ satisfied |
| CLAS-02 | 3 | passed | listed | Complete | ✅ satisfied |
| CLAS-03 | 3 | passed | listed | Complete | ✅ satisfied |
| INTER-01 | — | — | — | Deferred (descoped) | ⏭ intentional deferral (not a gap) |

No unsatisfied requirements, no orphans.

## Phase Verifications

| Phase | Name | Verification | Score | Code Review |
|-------|------|--------------|-------|-------------|
| 1 | Statistical Diagnostics | passed | 7/7 must-haves | (pre-milestone; sealed this run) |
| 2 | Global / Panel Models | passed | 7/7 must-haves | converged clean (3 iters, 10 findings fixed) |
| 3 | Classical & Multivariate Models | passed | 10/10 must-haves | converged clean (3 iters, 7 findings fixed) |

## Cross-Phase Integration (integration-checker: FULLY WIRED)

1. Single loadable extension registers all functions from all 3 phases — no symbol collisions, no duplicate registrations.
2. `ForecastOptions` FFI ABI extended **additively** across Phases 2–3 (`garch_p`/`garch_q`/`kalman_model` appended) — backward-compatible; `anofox_fcst_ffi.h` matches the Rust struct (cbindgen).
3. CMakeLists explicitly lists all new non-globbed C++ sources (`diagnostics.cpp`, `ts_forecast_panel_native.cpp`, `ts_forecast_var_native.cpp`).
4. Built extension loads and a function from EACH phase works in the SAME session (ts_adf_by → ts_forecast_panel_by → ts_forecast_by 'GARCH'/'Kalman' → ts_forecast_var_by).
5. Every documented `examples/*.sql` runs end-to-end against the built extension (PR #230 rule).

## End-to-End Flows

| Flow | Phase | Status |
|------|-------|--------|
| Stationarity / residual diagnosis | 1 | ✅ wired |
| Panel forecasting (Global*) | 2 | ✅ wired |
| GARCH conditional volatility | 3 | ✅ wired |
| Kalman state-space | 3 | ✅ wired |
| Multivariate VAR | 3 | ✅ wired |
| All phases in one session | 1+2+3 | ✅ wired |

## Benchmark parity (real measured, committed)

- Phase 2: GlobalETS 0.963 vs AutoETS 0.947 (+1.8%); GlobalTheta 0.956 (beats AutoTheta); GlobalCroston 0.963 (beats CrostonOptimized).
- Phase 3: GARCH vol-ratio 0.897; Kalman 1.000 / 0.992; VAR 1.000 (exact — both OLS).

## Verdict

Milestone definition of done met: every active requirement shipped through the full FFI→C++→macro→example→docs pattern, verified against the built extension, benchmarked, and documented. Deferrals are intentional, documented scope decisions — not blockers. **Ready to complete.**
