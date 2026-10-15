---
phase: 11-upstream-anofox-forecast-correctness-fixes
verified: 2026-10-15T22:10:00Z
status: human_needed
score: 5/7 must-haves verified, 2 flagged for human sign-off (no must-have FAILED)
covered_files:
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-01-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-01-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-02-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-02-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-03-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-03-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-04-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-04-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-05-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-05-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-06-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-06-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-07-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-07-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-08-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-08-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-09-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-09-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-10-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-10-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-11-PLAN.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-11-SUMMARY.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-CONTEXT.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-RESEARCH.md"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-baseline-lint.patch"
  - ".planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-upstream-base.env"
  - ".planning/REQUIREMENTS.md"
  - "Cargo.lock"
  - "Cargo.toml"
covered_digest: "v1:sha256:82ff61d24c65018f54657d068456ea76fd96f71065caf16dbbfc9f97c6cce772"
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: none
  previous_score: n/a
  gaps_closed: []
  gaps_remaining: []
  regressions: []
human_verification:
  - test: "Accept or reject the ETS AirPassengers ETS(M,A,M) alpha/gamma deviation from the ROADMAP's literal '≈0.40' target"
    expected: "ROADMAP success criterion 3 states 'AirPassengers MAM α/γ ≈ 0.40 (previously 0.9999)'. The corrected Σ ln|ŷ| likelihood is proven exact against R to <1e-12 at R's own parameters (test `ets_loglik_at_r_params_all_models`, independently re-run, 8/8 pass on v0.16.1). But the crate's own optimiser, run from scratch, converges to alpha≈0.790/beta≈0.0001/gamma≈0.210 — a *different*, slightly *better* local optimum (loglik -681.11 vs R's -682.40) than R's alpha≈0.395/gamma≈0.400 — not the ≈0.40 the ROADMAP names. The executor widened `ets_mam_parameters_near_r` from a literal alpha/gamma window to a fit-quality + 'moved off the old 0.9999 corner' check, with the exact numbers recorded in CHANGELOG.md and the test's own comments (not hidden)."
    why_human: "This is a value judgment: does proving the likelihood formula exact at R's own parameters (stronger, independently reproduced evidence) satisfy the intent of the literal '≈0.40' number, or does the ROADMAP's wording require the optimiser itself to land near R's specific values? Grep/test-presence cannot resolve which reading the project wants; a human should explicitly accept or ask for a multi-start optimiser fix (RC-03, already drafted upstream)."
  - test: "Accept or reject that TBATS's prediction-interval formula fix does not make TBATS interval widths match statsforecast within tolerance on 2 of 4 fixtures, and does not fall back to NULL intervals either"
    expected: "ROADMAP success criterion 4: 'TBATS/MSTL/GARCH interval widths match statsforecast/arch within tolerance, or those models return NULL intervals with documentation.' TBATS's `forecast_variance` formula is proven correct two independent ways (20000-path Monte-Carlo self-consistency <3% at every h, 97.6% empirical coverage against a true DGP) — but on 2 of 4 real-series fixtures (`multiplicative_seasonal`, `trend_seasonal`) the resulting width still diverges from statsforecast's own TBATS fit by up to 5.9x, because the crate's (pre-existing, unchanged) Nelder-Mead optimiser lacks the `D=F-g*w'` eigenvalue admissibility check statsforecast's optimiser enforces and can land on unstable parameter sets (e.g. alpha=0.999). The plan's own must-have assertion (`tbats_width_vs_statsforecast`, a `[0.5,2.0]` ratio bound) was demoted to a diagnostic print plus a finiteness/positivity sanity check; this is documented in the test's doc comment, CHANGELOG.md, and the PR #225 body."
    why_human: "Neither disjunct of the literal ROADMAP criterion holds for TBATS on these 2 fixtures (not 'within tolerance', not 'NULL with documentation' — it returns a non-null, formula-correct-but-statsforecast-divergent interval). The team's own root-cause (separate optimizer admissibility gap, not an interval-formula bug) is well-evidenced, but whether this satisfies 'within tolerance' as the ROADMAP intended, or requires a follow-up optimizer fix or a NULL-interval fallback for the affected fit shapes, is a product decision a verifier cannot make unilaterally."
---

# Phase 11: Upstream anofox-forecast Correctness Fixes Verification Report

**Phase Goal:** The upstream crate's stationarity tests, AutoARIMA order selection, ETS, Theta and interval-producing models are statistically correct against R / statsmodels / arch, delivered as reviewed PR(s) the user can merge and publish while extension-side phases continue.
**Verified:** 2026-10-15T22:10:00Z
**Status:** human_needed
**Re-verification:** No — initial verification (this phase's status in `.planning/state.json` was `in_progress`; no prior `11-VERIFICATION.md` existed). This was explicitly deferred per the task brief until Phase 17 consumed a released version of the crate (now done: `anofox-forecast = "0.16.1"`, crates.io registry source confirmed in `Cargo.lock`).

## Methodology note

Per the adversarial-verification mandate, SUMMARY.md claims were **not** trusted at face value. For every success criterion this report either (a) independently re-ran the upstream crate's own Rust tests on a **clean `git worktree` checkout of tag `v0.16.1`** (the exact version this repo's `Cargo.toml`/`Cargo.lock` pin), or (b) read the merged diff / CHANGELOG directly. All worktrees were removed after use; the user's own upstream working tree (`~/projects/rust/anofox-forecast`, branch `fix/auto-arima-order-selection`) was never switched or modified.

## Goal Achievement

### Observable Truths

| # | Truth (ROADMAP success criterion) | Status | Evidence |
|---|------|--------|----------|
| 1 | ADF augmented (lagged Δy), constant/constant+trend, AIC/BIC lag selection, MacKinnon p-values; statistic matches statsmodels/urca; Monte-Carlo 5% rejection on IMA(1,1) unit root ≈5% (was 63.5%); KPSS p-values bounded [0.01,0.10] matching tseries | ✓ VERIFIED | Independently re-run on clean `v0.16.1` worktree: `cargo test --test adf_kpss_reference` → 11 passed, 1 ignored (documented Monte-Carlo variant). CHANGELOG `[0.16.0]`: IMA(1,1) θ=-0.5 rejection 63.5%→8.70% (2000-rep MC), RW size 6.95% (within nominal [3.5%,7.5%]). `THIRD_PARTY_NOTICES.md` documents the MacKinnon/statsmodels provenance; `mackinnon_tables_match_statsmodels` passes. PR #221 merged into `main` (`gh pr view 221` → MERGED), landed in released `v0.16.0`/`v0.16.1`. |
| 2 | AutoARIMA selects d by KPSS, D by seasonal test, ranks by AICc on common likelihood basis; RW → (0,1,0) ≈70% (was 1%); AR(0.7) → d≥1 ≈19% (was 98%) | ✓ VERIFIED | Independently re-run on `v0.16.1`: `cargo test --test auto_arima_r_reference` → 9 passed, 1 ignored (Monte-Carlo variant) — `ndiffs_matches_r`, `nsdiffs_matches_r`, `auto_arima_ar07_d_share_matches_r`, `auto_arima_d_matches_r`, `auto_arima_airpassengers_matches_r`, `airpassengers_full_trace_matches_r`, `airpassengers_scores_match_r_trace`, `d4047_candidates_match_r_css_ml` all pass. `cargo test --test m4_daily_accuracy_regression --release` → 3/3 pass (the gate that blocked this work for 3 sessions). CHANGELOG `[Unreleased]`/`[0.16.1]`: RW `(0,1,0)` share **0.86 vs R's 0.86** (all 50 picks identical), AirPassengers selects R's exact `ARIMA(2,1,1)(0,1,0)[12]`, M4 D4047 at 1.000x baseline. PR #223 merged (`gh pr view 223` → MERGED 2026-10-14). See **Process Finding F1** below — this result was NOT reached inside the tracked Phase 11 plan corpus; it required untracked follow-up work. |
| 3 | ETS multiplicative-error likelihood uses Σ ln\|ŷ\|; AirPassengers MAM α/γ ≈0.40 (was 0.9999); class1/2 analytic intervals, class3 simulated, MC coverage verified | ⚠ PARTIAL (see human-verification item 1) | Likelihood/constraints/candidate-pool: independently re-run `cargo test --test ets_r_reference` → 8/8 pass, incl. `ets_loglik_at_r_params_all_models` (24 models exact to <1e-12 at R's parameters) and `auto_ets_airpassengers_selection` (AICc within 2.0 of R's best). **α/γ≈0.40 numeric target not met**: crate's own optimiser converges to α≈0.790/γ≈0.210 (a different, better local optimum, loglik -681.11 vs R's -682.40), not ≈0.40. Executor-documented, not hidden (CHANGELOG, test comments). Intervals: `cargo test --test ets_intervals_reference` → 4/4 pass — class1/2 exact to 1e-6 relative, class3 within 5%, reduced MC coverage 0.9500 (in-range). PR #224 merged. |
| 4 | Theta (STM) α by SES optimisation, standard interval formula, no RW bias vs thetaf; TBATS/MSTL/GARCH widths match statsforecast/arch within tolerance or NULL+documented | ⚠ PARTIAL (see human-verification item 2) | Theta: independently re-run `cargo test --test theta_thetaf_reference` → 4/4 pass — `theta_stm_matches_thetaf_rw` confirms the h=1 point forecast at -21.78656053 vs thetaf's -21.78656052 (was RW-biased). GARCH: `cargo test --test garch_reference` → 4/4 pass, 95% coverage 94.8–96.3% (was 63.3%). MSTL: `cargo test --test tbats_mstl_intervals_reference` → 5/7 pass, 2 ignored (documented MC variants) — width matches statsforecast within 4% on all 4 fixtures; MC coverage 90–91% (target [92,98]), explained (STL boundary bias, shared with statsforecast, not crate-specific) but **literally below the stated band**. TBATS: formula proven correct (two independent methods) but **width diverges from statsforecast by up to 5.9x on 2/4 fixtures** — neither "within tolerance" nor "NULL with documentation" as literally required. PR #225 merged. |
| 5 | All changes on fix branch(es) with PR(s) merged and released (0.16.0/0.16.1 on crates.io/GitHub); upstream cargo test/fmt/clippy green; CHANGELOG entries for each numerical change | ✓ VERIFIED | `gh pr list --state merged`: #221 (ADF/KPSS), #222 (Theta), #223 (AutoARIMA), #224 (ETS), #225 (TBATS/MSTL/GARCH) all MERGED into `main`; tags `v0.16.0` and `v0.16.1` exist; `Cargo.lock` in this repo resolves `anofox-forecast 0.16.1` from `registry+https://github.com/rust-lang/crates.io-index` (not a git/path/patch override). On a clean `git worktree` of tag `v0.16.1`: `cargo fmt --all --check` → clean (exit 0); `cargo clippy --all-features --all-targets -- -D warnings` → clean (no warnings/errors); full targeted test run across all 7 reference-test files → 0 failures. `git grep -n -E "TBD\|FIXME\|XXX"` across every file this phase touched → 0 hits. CHANGELOG.md has detailed `[0.16.0]`/`[0.16.1]`/`[Unreleased]` entries with before/after numbers for every change. |

**Score:** 5/7 criteria cleanly verified; 2 (#3, #4) are **substantively achieved but contain a literal numeric/behavioral deviation from the ROADMAP's exact wording that the team self-documented rather than hid** — routed to human sign-off, not scored as failed (the underlying mechanism in both cases is independently proven correct; only a downstream optimizer-convergence/third-party-width-match target is unmet).

### Process Finding F1 — UPST-01 (AutoARIMA) was not actually closed within the tracked Phase 11 plan corpus

`11-04-SUMMARY.md` and `11-05-SUMMARY.md` explicitly document a **HALT**: every scoring-redesign variant that hit the RW-share target broke the protected M4-Daily accuracy gate, so the fix was reverted and the relevant tests left `#[ignore]`d. `11-11-SUMMARY.md` (the last plan in this phase, dated 2026-10-10) documents a second, more invasive attempt (exact-Gaussian-likelihood scorer) that is **also halted** — "PR #223 updated with the halt evidence, remains draft" — with the actual code preserved only on a local, unpushed `wip/11-11-exact-ml-evidence` branch.

Independently confirmed via `gh api .../issues/223/timeline`: PR #223 was created as a draft 2026-10-09, and was **not** marked `ready_for_review`/merged until **2026-10-14T18:47:31Z** — four days after the last Phase 11 plan (`11-11`) ran. The commits that actually closed the gap (`feat(arima): AutoARIMA reproduces R auto.arima via a port of stats::arima (D-06, 11-11/11-12)`, `07ca427`/`db3f61a`) reference an "11-12" that **does not exist as a PLAN/SUMMARY file in this phase directory, nor in the upstream repo's own (separate) `.planning/`**. This is real, completed, and independently verified work — but it happened outside GSD's tracked planning artifacts for this phase.

**This is exactly the scenario the adversarial-verification mandate warns about**: trusting `11-11-SUMMARY.md` alone would have produced a false `gaps_found` (PR #223 "remains draft, not merged"). Direct codebase/GitHub evidence shows the opposite is now true. The goal **was** achieved — AutoARIMA's final, released behavior matches R to the degree the ROADMAP asks (RW share 86% = R's 86%, AirPassengers order match, M4 gate green) — but the GSD-phase paper trail does not reflect it, and no closure plan exists to amend. Recommend: either backfill an `11-12-PLAN.md`/`SUMMARY.md` describing the r_arima-port work for audit continuity, or explicitly note in `.planning/REQUIREMENTS.md`/`ROADMAP.md` that UPST-01's crate-side closure happened via untracked upstream work consumed and re-verified in Phase 17.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `~/projects/rust/anofox-forecast` PR #221 (`fix/adf-kpss-correctness`) | Augmented ADF, MacKinnon p-values, bounded KPSS | ✓ VERIFIED | Merged; tests pass on `v0.16.1` |
| `~/projects/rust/anofox-forecast` PR #222 (`fix/theta-alpha-intervals`) | STM alpha/level joint SSE optimisation, thetaf intervals | ✓ VERIFIED | Merged; tests pass on `v0.16.1` |
| `~/projects/rust/anofox-forecast` PR #223 (`fix/auto-arima-order-selection`) | KPSS d, seasonal D, common-basis AICc, R-parity stepwise search | ✓ VERIFIED (see F1) | Merged 2026-10-14 (after a halt documented in tracked plans); tests pass on `v0.16.1` |
| `~/projects/rust/anofox-forecast` PR #224 (`fix/ets-likelihood-intervals`) | Σln\|ŷ\| likelihood, usual constraints, R candidate pool, Hyndman intervals | ✓ VERIFIED (α/γ caveat, see human item 1) | Merged; tests pass on `v0.16.1` |
| `~/projects/rust/anofox-forecast` PR #225 (`fix/tbats-mstl-garch-intervals`) | TBATS/MSTL/GARCH interval fixes | ✓ VERIFIED (TBATS width caveat, see human item 2) | Merged; tests pass on `v0.16.1` |
| `anofox-forecast` v0.16.0 / v0.16.1 tags + crates.io release | Published, consumable version | ✓ VERIFIED | Tags exist; `Cargo.lock` here resolves `0.16.1` from crates.io registry (not git/path); Phase 17 (`17-VERIFICATION.md`, status `passed`) independently confirmed this live against the built DuckDB extension |
| `CHANGELOG.md` | Entries for every numerical change | ✓ VERIFIED | `[0.16.0]`, `[0.16.1]`, `[Unreleased]` sections all present with detailed before/after numbers |
| `THIRD_PARTY_NOTICES.md` | MacKinnon/statsmodels provenance | ✓ VERIFIED | Present, references MacKinnon 1994/2010, statsmodels `adfvalues` |

### Key Link Verification

| From | To | Via | Status |
|------|-----|-----|--------|
| This repo's `Cargo.toml`/`Cargo.lock` | Upstream `anofox-forecast` 0.16.1 | crates.io registry dependency, no `[patch]` override | ✓ WIRED (confirmed: `grep anofox-forecast Cargo.toml` → `version = "0.16.1"`; `Cargo.lock` registry source) |
| Phase 11 upstream fix branches | Phase 17 consumption | Released crate version consumed and independently re-verified (`stationarity_reference`, `portmanteau_reference`, `r_forecast_parity`, `interval_coverage_mc` tests, all passing against 0.16.1 per `17-VERIFICATION.md`) | ✓ WIRED |

### Requirements Coverage

| Requirement | Source Plan(s) | Status | Evidence |
|-------------|----------------|--------|----------|
| DIAG-01 | 11-02 | ✓ SATISFIED | ADF augmentation + MacKinnon, tests re-run green on v0.16.1 |
| DIAG-02 | 11-02 | ✓ SATISFIED | KPSS bounded p-value, tests re-run green on v0.16.1 |
| UPST-01 | 11-04, 11-05, 11-11 (crate-side); formally re-mapped to **Phase 17** in `.planning/REQUIREMENTS.md`'s traceability table per Phase 17's own D-15 decision, and marked `SATISFIED` there (`17-VERIFICATION.md` requirement-coverage table) | ✓ SATISFIED (via Phase 17, crate-side work independently re-verified here) | See Process Finding F1 for the tracking gap between Phase 11's own plan corpus and the actual (later, untracked) closure |
| UPST-02 | 11-06 | ⚠ SATISFIED WITH CAVEAT | Likelihood/constraints/candidate pool exact; AirPassengers MAM α/γ target not literally met (human item 1) |
| UPST-03 | 11-07, 11-08 | ✓ SATISFIED | Class1/2/3 intervals + ES-family propagation, tests re-run green |
| UPST-04 | 11-03 | ✓ SATISFIED | Theta SES joint optimisation + thetaf formula, tests re-run green |
| UPST-05 | 11-09, 11-10 | ⚠ SATISFIED WITH CAVEAT | GARCH/MSTL good; TBATS width-vs-statsforecast gap on 2/4 fixtures (human item 2) |

**REQUIREMENTS.md cross-check:** All 7 requirement IDs in the task brief (DIAG-01, DIAG-02, UPST-01..05) are claimed by at least one Phase 11 plan's frontmatter `requirements:` field. No orphaned Phase-11 requirements found. **Stale bookkeeping noted**: `.planning/REQUIREMENTS.md`'s checkbox list still shows `[ ]` (unchecked) for DIAG-01, DIAG-02, UPST-02, UPST-03, UPST-04, UPST-05 despite this verification now confirming the underlying work exists and (mostly) passes — the checkboxes were evidently never flipped when Phase 11's PRs merged. Recommend updating them to `[x]` once the two human-verification items above are resolved.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | — | — | `git grep -n -E "TBD\|FIXME\|XXX"` across every file this phase's plans list as `files_modified` (upstream `src/`, `tests/`) → 0 hits. All `#[ignore]`d tests (5 across the touched test files) carry a documented `"monte-carlo: cargo test ... --ignored ... --nocapture"` reason, consistent with D-03's "reduced deterministic version must run in normal `cargo test`" requirement — none are silently skipped correctness checks. |

### Behavioral Spot-Checks (independently re-run by this verifier on a clean `git worktree` of tag `v0.16.1`, not reused from SUMMARY claims)

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| ADF/KPSS reference suite | `cargo test --test adf_kpss_reference` | 11 passed, 1 ignored | ✓ PASS |
| AutoARIMA R-parity suite | `cargo test --test auto_arima_r_reference` | 9 passed, 1 ignored | ✓ PASS |
| M4-Daily accuracy regression gate | `cargo test --test m4_daily_accuracy_regression --release` | 3 passed | ✓ PASS |
| ETS intervals reference suite | `cargo test --test ets_intervals_reference` | 4 passed, 1 ignored | ✓ PASS |
| ETS R-parity suite (likelihood, constraints, candidate pool) | `cargo test --test ets_r_reference` | 8 passed | ✓ PASS |
| GARCH reference suite | `cargo test --test garch_reference` | 4 passed | ✓ PASS |
| TBATS/MSTL intervals reference suite | `cargo test --test tbats_mstl_intervals_reference` | 5 passed, 2 ignored | ✓ PASS |
| Theta thetaf reference suite | `cargo test --test theta_thetaf_reference` | 4 passed | ✓ PASS |
| `cargo fmt --all --check` | — | clean, exit 0 | ✓ PASS |
| `cargo clippy --all-features --all-targets -- -D warnings` | — | clean, no warnings | ✓ PASS |
| `Cargo.lock` resolves `anofox-forecast` to crates.io, no override | `grep -A2 'name = "anofox-forecast"' Cargo.lock` | `version = "0.16.1"`, `source = "registry+...crates.io-index"` | ✓ PASS |
| PR merge status | `gh pr list --state merged` (upstream repo) | #221,#222,#223,#224,#225 all MERGED | ✓ PASS |

### Probe Execution

Not applicable — this phase has no `scripts/*/tests/probe-*.sh` convention; its verification is Rust `cargo test` against checked-in R/Python-generated reference fixtures, exercised directly above.

### Human Verification Required

1. **ETS AirPassengers MAM α/γ ≈0.40 deviation** — see frontmatter `human_verification[0]` for full detail. The likelihood correction is proven exact against R; the specific numeric target in the ROADMAP's wording is not hit because the (correctly implemented) optimiser finds a different, slightly better local optimum. Needs an explicit accept/reject.
2. **TBATS interval width vs. statsforecast on 2/4 fixtures** — see frontmatter `human_verification[1]` for full detail. The interval formula is proven correct two independent ways, but width still diverges up to 5.9x from statsforecast on affected fixtures due to a separate, documented, pre-existing optimizer-admissibility gap. Needs an explicit accept/reject, or a decision to open a follow-up fix (admissibility check in the TBATS Nelder-Mead optimiser) or NULL-interval fallback for affected fit shapes.

### Gaps Summary

No must-have **failed** outright. The five upstream PRs (#221–#225) are merged, released as `anofox-forecast` 0.16.0/0.16.1 on crates.io, consumed cleanly by this repo (confirmed via `Cargo.lock`, independently cross-checked against Phase 17's own passed verification), and pass `cargo fmt`/`clippy`/`test` on a clean checkout of the released tag — independently re-run by this verifier, not taken on SUMMARY's word. ADF/KPSS, Theta, ETS-likelihood, ETS-intervals and GARCH all hit their literal ROADMAP targets with margin.

Two items are **substantively achieved but literally deviate from the ROADMAP's exact wording**, each self-documented by the executor with strong, independently-reproduced supporting evidence rather than hidden or silently loosened:
- ETS AirPassengers MAM α/γ lands at a different (better) local optimum than R's ≈0.40, not ≈0.40 itself.
- TBATS interval width still diverges from statsforecast by up to 5.9x on 2 of 4 fixtures, traced to a separate pre-existing optimizer gap, not the interval formula this phase fixed.

Both are routed to human sign-off rather than marked failed, because the underlying mechanism in each case is independently proven correct against a stronger reference (R's own likelihood at R's own parameters; the model's own state-space recursion via Monte-Carlo self-consistency) — a verifier cannot unilaterally decide whether that satisfies the ROADMAP's literal numeric wording.

A separate **process finding (F1)** is reported for UPST-01/AutoARIMA: the tracked Phase 11 plan corpus (11-01..11-11) halts with PR #223 explicitly left in draft; the actual closure (an "11-12" r_arima port) happened later, untracked by any GSD plan/summary in either repo, but is real, merged, released, and independently re-verified here to fully meet the ROADMAP's AutoARIMA criteria. This is reported as a documentation/traceability gap, not a goal-achievement gap — recommend backfilling a closure record.

---

*Verified: 2026-10-15T22:10:00Z*
*Verifier: Claude (gsd-verifier)*
