---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 09
subsystem: infra
tags: [rust, garch, prediction-intervals, volatility, upstream-crate, python-reference, statsforecast]

requires:
  - phase: 11-01
    provides: "11-upstream-base.env (BASE_REF=1ae5cfc), 11-baseline-lint.patch"
provides:
  - "Branch fix/tbats-mstl-garch-intervals (part 1/2 — plan 11-10 adds TBATS/MSTL before this PR is marked ready)"
  - "validation/reference/python/garch_reference.py — statsforecast GARCH reference generator over two series simulated from a TRUE GARCH(1,1) DGP plus the real heteroscedastic/ar1 series"
  - "tests/data/r_reference/garch_reference.json — fixture with true parameters, simulated series, statsforecast fit/forecast/interval numbers"
  - "tests/garch_reference.rs — garch_parameter_recovery, garch_forecast_variance_matches_closed_form, garch_interval_coverage_on_true_dgp (asserting), garch_width_vs_statsforecast"
  - "src/models/garch.rs — predict_with_intervals centred on the fitted conditional mean instead of predict()'s simulated-innovation draw"
  - "Draft PR #225 (fix/tbats-mstl-garch-intervals -> main) open with the findings table"
affects: [17-upstream-crate-consumption]

actuals:
  tokens: 35500
  tasks: 2
  commits: 3
  plan_head_before: "n/a — upstream commits measured directly against BASE_REF (1ae5cfc13ef98afdbb17e27def95a632cbb74fe7..HEAD); this plan's own repo (duckdb/anofox-forecast) commit count is measured separately below"

tech-stack:
  added: []
  patterns:
    - "Diagnose-before-decide (D-03/D-10): when a validation report shows model A's interval width is Nx a reference implementation's, measure coverage against the TRUE data-generating process FIRST, not just the width ratio against the reference — the reference itself can be the miscalibrated party (confirmed here: statsforecast's own GARCH predict()/forecast() multiplies the z-quantile by variance instead of std dev, an error invisible exactly when sigma2 happens to be near 1)."
    - "When a model's predict() intentionally returns a single fixed simulated draw (documented 'matches library X's behaviour' compatibility choice), predict_with_intervals must NOT inherit that draw as its interval centre — centre on the model's own closed-form conditional mean/forecast instead, and prove it via Monte-Carlo coverage against continuations simulated from the true (or fitted) parameters, not merely against another library's forecast."

key-files:
  created:
    - /home/simonm/projects/rust/anofox-forecast/validation/reference/python/garch_reference.py
    - /home/simonm/projects/rust/anofox-forecast/tests/data/r_reference/garch_reference.json
    - /home/simonm/projects/rust/anofox-forecast/tests/garch_reference.rs
  modified:
    - /home/simonm/projects/rust/anofox-forecast/src/models/garch.rs
    - /home/simonm/projects/rust/anofox-forecast/CHANGELOG.md

key-decisions:
  - "D-10 outcome: FIX, not point-only. The root cause (H1, confirmed by measurement) was a one-line centring bug, forecast_variance()'s closed-form recursion was independently confirmed correct to 1e-10 relative, and the fix was proven to restore coverage to [0.92, 0.98] against the TRUE GARCH(1,1) DGP (not merely against statsforecast) — all three of D-10's preconditions for shipping FIX were met."
  - "H5 (statsforecast's own interval definition) is the primary explanation for the audit's reported '0.13x' width ratio, not a crate defect: reading statsforecast/models.py's GARCH.predict/.forecast shows `lo = mean - quantiles * sigma2` — the z-quantile is multiplied by sigma2 (variance), not sqrt(sigma2) (std dev). This is invisible when sigma2 happens to be close to 1 (ratio measured 0.9947 on the mu5-shifted series) but inflates statsforecast's own width by a factor of sqrt(sigma2) otherwise (ratio measured 0.34 on the zero-mean series, sigma2≈2.85, sqrt(2.85)≈1.69, 1/1.69≈0.59 of the inflation visible plus statsforecast's own optimiser failing to recover the true low-variance parameters on top). Width-ratio-vs-statsforecast was therefore NOT used as the pass/fail criterion for the fix — true-DGP coverage was."
  - "H3 (sigma2 recursion uses raw series values, not demeaned residuals, in the ARCH/GARCH term) was investigated and left unfixed: reading statsforecast's own garch_sigma2 confirms it also uses raw x (not residuals) — this is a faithful match to statsforecast's documented convention (the crate's own doc comment: 'matches statsforecast behavior'), not a crate-introduced bug, and it does not affect the zero-mean series used for the coverage proof (raw == demeaned when mean is 0). Flagged as a theoretical concern for series with mean >> volatility scale but out of scope for this plan's D-10 decision, which only required measuring and resolving the reported interval-width defect."
  - "predict() is deliberately left unchanged (still returns the documented statsforecast-compatible simulated-innovation path) — only predict_with_intervals's centring changed. The extension's FFI boundary (Phase 10, FCST-01) uses sqrt(forecast_variance(h)) as its GARCH point value and only passes predict_with_intervals's bounds through, so this change is a pure correctness improvement with no downstream migration needed once 0.16.0 is consumed (Phase 17)."
  - "Two-commit split mirrors the plan's task structure exactly: commit 1 (test(garch): ...) is the Task-1 diagnostic harness with a print-only/harness-sanity coverage check against the UNFIXED predict_with_intervals; commit 2 (fix(garch): ...) tightens that same assertion to [0.92, 0.98] in the same edit that lands the fix, so `git log -p` on this branch shows the exact before/after the plan's acceptance criteria describe."

patterns-established:
  - "See key-decisions above (duplicated here per the frontmatter's dual decision-record/forward-looking-pattern purpose)."

requirements-completed: []

coverage:
  - id: D1
    description: "A checked-in reference harness (statsforecast + true-parameter GARCH(1,1) simulation) measures and names which of H1-H5 are confirmed/rejected, with numbers, for the audit's reported GARCH interval-width mismatch"
    requirement: UPST-05
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/garch_reference.rs#garch_parameter_recovery, #garch_width_vs_statsforecast -- pass (printed findings, see Findings Table below)"
        status: pass
  - id: D2
    description: "forecast_variance(h) matches the closed-form GARCH(1,1) multi-step recursion at the crate's own fitted parameters to 1e-10 relative"
    requirement: UPST-05
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/garch_reference.rs#garch_forecast_variance_matches_closed_form -- pass"
        status: pass
  - id: D3
    description: "predict_with_intervals' empirical 95% coverage on continuations simulated from the TRUE GARCH(1,1) parameters is within [0.92, 0.98] at every h=1..12 (D-10 FIX outcome)"
    requirement: UPST-05
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/garch_reference.rs#garch_interval_coverage_on_true_dgp -- pass (measured 94.8%-96.3% across h=1..12, up from 63.3% at h=1 pre-fix)"
        status: pass
  - id: D4
    description: "Branch fix/tbats-mstl-garch-intervals is pushed and a draft PR against main is open containing the GARCH findings"
    requirement: UPST-05
    human_judgment: false
    verification:
      - kind: other
        ref: "git push -u origin fix/tbats-mstl-garch-intervals (new branch); gh pr create --draft --repo sipemu/anofox-forecast --base main --head fix/tbats-mstl-garch-intervals -> https://github.com/sipemu/anofox-forecast/pull/225 (isDraft:true, confirmed via gh pr list --json isDraft)"
        status: pass

duration: ~55min
completed: 2026-10-10
status: complete
---

# Phase 11 Plan 09: GARCH Interval Diagnosis and Conditional-Mean Fix Summary

**Root-caused the audit's "GARCH 0.13x interval width" to a bounds-centring bug (fix: centre on the conditional mean, not predict()'s simulated-innovation draw) compounded by a bug in statsforecast's own reference implementation (multiplies the z-quantile by variance instead of std dev); coverage restored from 63.3% to 94.8-96.3% against the true GARCH(1,1) DGP.**

## Performance

- **Duration:** ~55 min
- **Started:** 2026-10-10 (session start)
- **Completed:** 2026-10-10
- **Tasks:** 2
- **Files modified (upstream crate):** 5 (3 new: Python generator, JSON fixture, Rust test file; 2 modified: garch.rs, CHANGELOG.md) plus the shared baseline-lint patch file (1 file, from 11-01)

## Accomplishments

- Built a checked-in reference harness (`validation/reference/python/garch_reference.py`, locked statsforecast 2.0.3 / numpy 2.3.5 env) that simulates a TRUE GARCH(1,1) data-generating process (omega=0.1, alpha=0.1, beta=0.8, zero-mean and a mu=5-shifted copy) alongside the real `heteroscedastic`/`ar1` validation series, and fits statsforecast's own GARCH(1,1) on all four
- Measured all five candidate hypotheses (H1-H5) from the research doc against real numbers instead of guessing — see Findings Table below
- Confirmed `forecast_variance()`'s multi-step variance recursion is structurally correct (matches the textbook closed form to 1e-10 relative) — it was never the problem
- Found and fixed the actual root cause (H1): `predict_with_intervals` centred its bounds on `predict()`'s single fixed simulated-innovation draw instead of the fitted conditional mean, so the interval could sit several standard deviations away from the series' true distribution. Fixed by centring on `self.mean` instead; `predict()` itself is unchanged
- Found that the audit's reference comparison point (statsforecast) has its own GARCH interval bug — it multiplies the z-quantile by `sigma2` (variance) instead of `sqrt(sigma2)` (std dev) — which explains most of the reported width mismatch independent of any crate defect
- Verified empirical 95% coverage against 1000 continuations simulated from the TRUE DGP parameters: **63.3% (pre-fix, h=1) → 94.8-96.3% (post-fix, h=1..12)**
- Pushed branch `fix/tbats-mstl-garch-intervals` (created fresh from `BASE_REF=1ae5cfc`, baseline lint patch applied first) and opened draft PR [#225](https://github.com/sipemu/anofox-forecast/pull/225) against `main`, explicitly marked part 1/2 pending plan 11-10's TBATS/MSTL work

## Findings Table (H1-H5)

| Hypothesis | Measurement | Result |
|---|---|---|
| **H1**: bounds centred on `predict()`'s simulated-innovation draw, not the conditional mean | Empirical 95% coverage vs. 1000 continuations simulated from the TRUE GARCH(1,1) params: **63.3%** at h=1 (nominal 95%), pre-fix | **CONFIRMED** — root cause |
| **H2**: `omega` pinned to the optimizer's upper bound (10000) on level series | Crate's fitted `omega` on the simulated series = 0.1086 (true = 0.1); `garch_parameter_recovery` asserts `omega < 10000 - 1e-9` and passes | REJECTED |
| **H3**: `sigma2` recursion uses raw series values while the likelihood uses demeaned residuals | Crate's `compute_sigma2` uses raw `x`, exactly matching statsforecast's own `garch_sigma2` (also raw `x`, not residuals, verified by reading `statsforecast/garch.py`) — a documented compatibility choice, not a crate-introduced bug; irrelevant on the zero-mean series used for the coverage proof (raw == demeaned there) | REJECTED as the coverage cause here (flagged as a theoretical concern for high-mean series, out of scope) |
| **H4**: crate's Nelder-Mead optimiser fails to recover parameters | Crate recovers (omega=0.1086, alpha=0.0818, beta=0.8095) vs true (0.1, 0.1, 0.8) — close. statsforecast's own SLSQP badly fails on the identical series (omega=2.8478, alpha≈0.0001, beta≈0.0004) | REJECTED (crate's optimiser outperforms statsforecast's) |
| **H5**: statsforecast's own interval definition | Reading `statsforecast/models.py` GARCH `predict`/`forecast`: `lo = mean - quantiles * sigma2` — multiplies the z-quantile by **variance**, not `sqrt(variance)`. Invisible when sigma2≈1 (width ratio measured 0.9947 on the mu5-shifted series, where sigma2≈0.997) but inflates statsforecast's own width by roughly `sqrt(sigma2)` otherwise (width ratio measured 0.34 on the zero-mean series, sigma2≈2.85) | **CONFIRMED** — explains most of the audit's reported "0.13x" width gap; the statsforecast reference itself is miscalibrated for this comparison, not the crate |

## Task Commits

All commits are in the upstream crate repo (`/home/simonm/projects/rust/anofox-forecast`), branch `fix/tbats-mstl-garch-intervals`, created from `BASE_REF=1ae5cfc13ef98afdbb17e27def95a632cbb74fe7`:

0. **P4 baseline lint (shared, from 11-01's patch)** — `0a1fa5b` (style) — `style: baseline clippy/fmt fixes for current stable (no behaviour change)`
1. **Task 1: Tracer / spike — reference harness + root-cause finding** — `3e1e26b` (test) — `test(garch): reference harness diagnosing the GARCH interval mismatch (UPST-05)`
2. **Task 2: Ship the FIX outcome, CHANGELOG, push, draft PR** — `7e8d67f` (fix) — `fix(garch): centre prediction intervals on the conditional mean (UPST-05, D-10)`

**Plan metadata:** this commit, in the planning repo (`duckdb/anofox-forecast`), via `git commit --only` on `11-09-SUMMARY.md` (docs: complete plan).

## Files Created/Modified

Upstream crate (`/home/simonm/projects/rust/anofox-forecast`):
- `validation/reference/python/garch_reference.py` — statsforecast GARCH reference generator (new)
- `tests/data/r_reference/garch_reference.json` — fixture: true parameters, simulated series values, statsforecast fit/forecast/interval numbers, provenance (new)
- `tests/garch_reference.rs` — `garch_parameter_recovery`, `garch_forecast_variance_matches_closed_form`, `garch_interval_coverage_on_true_dgp` (asserting [0.92, 0.98]), `garch_width_vs_statsforecast` (new)
- `src/models/garch.rs` — `predict_with_intervals` now centres point/bounds on `self.mean` instead of `predict()`'s simulated draw; added inline regression test `garch_predict_with_intervals_centred_on_conditional_mean`; updated struct-level rustdoc
- `CHANGELOG.md` — new `[0.16.0] - Unreleased` section, `### Fixed` entry with the before/after coverage numbers

Planning repo (`duckdb/anofox-forecast`):
- `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-09-SUMMARY.md` — this file

## Decisions Made

See `key-decisions` in frontmatter above (D-10 FIX-outcome rationale, H5 statsforecast-bug finding, H3 scope decision, predict() left unchanged, two-commit task split).

## Deviations from Plan

None — plan executed exactly as written. The plan's own acceptance criteria anticipated either FIX or POINT-ONLY as valid outcomes contingent on the Task 1 findings; FIX was chosen per D-10's stated preference ("fix if tractable") because the root cause was a one-line centring bug with an already-correct variance recursion underneath it, and the fix was proven against the true DGP rather than merely asserted.

## Issues Encountered

None requiring problem-solving beyond the diagnostic work described above (which was the plan's intended Task 1 deliverable, not an issue).

## User Setup Required

None — no external service configuration required. Draft PR [#225](https://github.com/sipemu/anofox-forecast/pull/225) is open against `main` for the user's eventual review; per the plan, it stays in draft until plan 11-10 lands TBATS/MSTL fixes on the same branch.

## Next Phase Readiness

- Plan 11-10 can now check out `fix/tbats-mstl-garch-intervals` directly (already pushed, clean except `.planning/state.json`) and add TBATS + MSTL interval fixes, then mark PR #225 ready for review.
- Full P8 DoD (fmt, clippy -D warnings, test --all-features, rustdoc -D warnings) is green on the final state of this plan's two commits; `cargo test --all-features` shows 0 failures across the entire suite (3188+ tests), confirming no regression outside GARCH.
- `requirements-completed` is intentionally empty here: UPST-05 covers TBATS/MSTL/GARCH jointly, and this plan only closes the GARCH third — full UPST-05 closure (and the `requirements mark-complete` call) belongs to plan 11-10 once TBATS/MSTL land.

---
*Phase: 11-upstream-anofox-forecast-correctness-fixes*
*Completed: 2026-10-10*

## Self-Check: PASSED

- FOUND: /home/simonm/projects/rust/anofox-forecast/validation/reference/python/garch_reference.py
- FOUND: /home/simonm/projects/rust/anofox-forecast/tests/data/r_reference/garch_reference.json
- FOUND: /home/simonm/projects/rust/anofox-forecast/tests/garch_reference.rs
- FOUND: commit 0a1fa5b in upstream git log
- FOUND: commit 3e1e26b in upstream git log
- FOUND: commit 7e8d67f in upstream git log
- FOUND: draft PR https://github.com/sipemu/anofox-forecast/pull/225 (isDraft:true)
