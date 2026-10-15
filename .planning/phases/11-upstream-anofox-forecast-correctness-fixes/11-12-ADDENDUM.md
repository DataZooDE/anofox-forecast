---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 12
type: addendum
backfilled: 2026-10-16
source: reconstructed from upstream git history and `gh pr view 223` in ~/projects/rust/anofox-forecast (no 11-12-PLAN.md was ever written)
requirements: [UPST-01]
---

# Phase 11 Addendum: untracked "11-12" AutoARIMA follow-up (backfilled)

## Why this file exists

The tracked Phase 11 plan corpus (11-01 .. 11-11) ends with `11-11-SUMMARY.md` halting on AutoARIMA,
with upstream PR #223 still a draft. The AutoARIMA work was later finished and merged outside that
tracking. Upstream commits refer to it as "11-12", but there is no 11-12 plan file in either repository.
The deferred Phase 11 verification (`11-VERIFICATION.md`, 2026-10-16) flagged the gap between the
record and what actually happened. The user decided to backfill the record rather than leave it.

## What happened (from upstream history)

| When | Upstream ref | What |
|------|--------------|------|
| 2026-10-09 | PR #223 opened (`fix/auto-arima-order-selection`) | KPSS d, seasonal-strength D-then-d, constant-allowance gating, SARIMA AICc correction (tracked through 11-11) |
| 2026-10-10 22:05 | `5489bad` | feat(arima): port R `stats::arima` estimation path; AutoARIMA reproduces `auto.arima` (D-06, 11-12). Adds `src/models/arima/r_arima.rs` (~1.4k lines) |
| 2026-10-10 22:05 | `07ca427` | feat(arima): AutoARIMA reproduces R `auto.arima` via the `stats::arima` port (D-06, 11-11/11-12) |
| 2026-10-10 22:06 | `db3f61a` | docs(changelog): AutoARIMA R parity (`r_arima` port), resolves the AICc known gap (11-12) |
| 2026-10-14 18:47 | PR #223 merged as `611dcb8` (12 commits) | Out of draft and merged to upstream `main` |
| 2026-10-14 | PR #228, Release v0.16.1 | Published to crates.io; consumed by this extension in Phase 17 (17-01) |

Across the three 11-12 commits: 6 files changed, +2294 / -313 lines, including
`tests/auto_arima_r_reference.rs`.

## Outcome and evidence

- UPST-01 (AutoARIMA order selection at R `auto.arima` parity) is closed. Evidence:
  - Phase 17 plan 17-03 ran a seeded selection-frequency simulation (60 series, 60/60 exact
    agreement with R).
  - AutoARIMA order and point forecasts are at hard parity on every R fixture series
    (`crates/anofox-fcst-core/tests/r_forecast_parity.rs`).
  - `11-VERIFICATION.md` independently re-ran the upstream suite on a clean `v0.16.1` checkout,
    and it was green.
- One AutoARIMA residual was carried forward: the AirPassengers 95% interval (RC-05, SARIMA
  prediction variance). Upstream PR #229 fixes it (R `KalmanForecast` variances, within 8e-10),
  and release PR #230 ships it as v0.16.2. The user decided on 2026-10-15 to fold the v0.16.2 bump
  into v0.10.0 Phase 18 once it is published.

## Process note

This work bypassed plan/summary tracking because it ran as a direct continuation of the 11-11
halt, while extension phases 12-16 were in flight. The final state is verified. The lesson, already
in project memory ("verify executor reports vs git+disk"), applies here too: always reconcile upstream
PR state against the tracked plans before closing a phase.
