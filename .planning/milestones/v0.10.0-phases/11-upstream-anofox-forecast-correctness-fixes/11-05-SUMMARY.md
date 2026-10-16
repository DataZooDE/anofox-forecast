---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 05
subsystem: infra
tags: [rust, arima, autoarima, aicc, kpss, upstream-crate, github-pr, r-reference]

requires:
  - phase: 11-04
    provides: "Branch fix/auto-arima-order-selection, draft PR #223, d-by-KPSS + D-then-d ordering + constant-allowance + SARIMA AICc correction, R fixture (tests/data/r_reference/auto_arima_r.json), and the documented Known Gap (per-candidate AICc scoring-window bug breaks the M4 gate when the common-basis fix is applied)"
provides:
  - "AutoARIMAConfig::max_order (R auto.arima's max.order, default 5) enforced by true_stepwise_search; AutoARIMAConfig::with_grid_stepwise() builder"
  - "AutoARIMA::fit no longer force-zeros the seasonal period when the seasonal-strength test is weak -- seasonal P/Q are searched even when D=0, matching R (seasonal terms drop only when period<2)"
  - "NEW evidence on the 11-04 Known Gap: the per-candidate AICc scoring-window bug also breaks AirPassengers (a real, canonical dataset), not only the synthetic RW/AR(0.7) Monte-Carlo fixtures -- R's actual pick scores ~170 AICc units worse than an overfit alternative under the crate's own formula"
  - "Direct Rscript forecast::auto.arima verification on M4-Daily D4047: R itself selects ARIMA(0,1,0), MAE 141.5, ratio 1.0x vs the statsforecast gate baseline -- confirms the M4 gate's 2x tolerance is evidence-backed and must not be loosened"
  - "auto_arima_airpassengers_matches_r test added (ignored, documented) alongside the two existing Known-Gap Monte-Carlo tests"
  - "PR #223 updated with this session's findings; remains draft (not marked ready) because the Known Gap is unresolved"
affects: [17-upstream-crate-consumption]

actuals:
  tokens: 42000
  tasks: 1
  commits: 2
  plan_head_before: 4ac0a97

tech-stack:
  added: []
  patterns:
    - "Used a direct Rscript forecast::auto.arima invocation (not just the checked-in R fixture generator) to get fresh, targeted evidence on a specific M4 gate series when the existing fixture didn't cover it -- a lightweight way to get real R ground truth without expanding the checked-in fixture generator."
    - "When a plan's tracer task (type=\"tracer\") fails its own verify after a good-faith, evidence-gathering attempt, split the diff: keep the safe, additive, independently-tested subset (new config field + builder + a real bug fix), revert the risky default flip that depends on an unresolved root cause, and mark the plan status: halted rather than fabricate a pass or silently ship the regression."

key-files:
  created: []
  modified:
    - /home/simonm/projects/rust/anofox-forecast/src/models/arima/auto_arima.rs
    - /home/simonm/projects/rust/anofox-forecast/tests/auto_arima_r_reference.rs
    - /home/simonm/projects/rust/anofox-forecast/CHANGELOG.md

key-decisions:
  - "Did NOT flip AutoARIMAConfig::default()'s max_cap_p/max_cap_q to R's real default (2) or true_stepwise to true (R's real default), even though this is exactly what the plan's Task 1 asked for. Implemented the flip, tested it, and found it re-exposes the 11-04 Known Gap (per-candidate AICc scoring-window bug) on AirPassengers specifically (not just the RW/AR fixtures): the crate's own AICc ranks an overfit ARIMA(3,1,0)(2,1,0)[12] (~829) far ahead of R's actual pick ARIMA(2,1,1)(0,1,0)[12] (~1002), a ~170-unit gap from comparing candidates over different effective sample sizes. Per the 2026-10-10 user decision ('stop and report precisely... rather than shipping a regression'), reverted the default flip and kept only the safe, additive pieces (max_order field, with_grid_stepwise() builder, the now-redundant non-seasonal-fallback removal, which does NOT depend on the scoring bug)."
  - "Gathered the specific R evidence the user decision asked for on M4 D4047: ran Rscript forecast::auto.arima directly on the D4047 train/test split from tests/data/m4_outliers.json (same horizon=14, frequency=7 as tests/m4_daily_accuracy_regression.rs). R selects ARIMA(0,1,0) with MAE=141.5 -- IDENTICAL to the statsforecast baseline the gate already uses (ratio exactly 1.0x). This confirms R is NOT in the '>2x' regime on this series, so per the user decision's branching logic (point 3), the M4 gate must not be adjusted/loosened -- the crate's search needs fixing to match R, which remains open."
  - "Added auto_arima_airpassengers_matches_r as #[ignore]d with full root-cause documentation (same pattern 11-04 established for its two Monte-Carlo tests), rather than deleting it or leaving it failing in the default test run. This keeps the gap visible and the exact target (R's order, top-3 ranking) encoded for whoever resumes this work."
  - "Marked this SUMMARY status: halted (not complete) per #2830's machine-read halt semantics: Task 1's tracer gate did not pass as specified, so Tasks 2 and 3 (re-validate selection frequencies against the new default, CHANGELOG+DoD+push+mark-PR-ready) were not executed. The CHANGELOG was still updated (documenting what WAS shipped plus the re-confirmed gap) since that's independently true and useful, but the PR was intentionally left draft."

patterns-established:
  - "Evidence-gated default changes: when a plan's required default flip is proven (by direct testing) to reintroduce a documented Known Gap on a NEW dataset, split the commit into (a) safe additive API surface that doesn't depend on the broken mechanism, committed and pushed, and (b) the risky flip itself, reverted and documented with the exact numbers -- rather than ship (a)+(b) together and let the regression ride along with the safe parts, or discard (a) along with (b) just because the overall task failed."

requirements-completed: []

coverage:
  - id: D1
    description: "AutoARIMAConfig::max_order (R's max.order, default 5) added and enforced by true_stepwise_search on every start model and neighbour move"
    requirement: UPST-01
    human_judgment: false
    verification:
      - kind: unit
        ref: "src/models/arima/auto_arima.rs#tests::default_config_has_max_order_and_grid_stepwise"
        status: pass
  - id: D2
    description: "AutoARIMA::fit no longer force-zeros the seasonal period when the seasonal-strength test is weak; seasonal P/Q are searched even when nsdiffs_seas yields D=0 (seasonal terms drop only when period<2)"
    requirement: UPST-01
    human_judgment: false
    verification:
      - kind: integration
        ref: "cargo test --test m4_daily_accuracy_regression (3/3 pass, unaffected); cargo test --test auto_arima_validation --test auto_arima_order_selection (13/13 pass, unaffected)"
        status: pass
  - id: D3
    description: "KNOWN GAP, STILL NOT MET: default AutoARIMA search flipped to R's Hyndman-Khandakar stepwise with R's real bounds (max_cap_p=2, max_cap_q=2, true_stepwise=true), with R's order among the crate's top-3 scored candidates on AirPassengers"
    requirement: UPST-01
    human_judgment: true
    rationale: "Implemented, tested, and found to re-expose the 11-04 Known Gap (per-candidate AICc scoring-window bug) on AirPassengers (a real dataset, not just synthetic fixtures): R's own pick scores ~170 AICc units worse than an overfit alternative under the crate's current CSS-based scoring. A direct Rscript run confirms R is not in the gate's '>2x' regime on M4 D4047 either, so the M4 gate cannot be adjusted to accommodate a looser crate search. Closing this requires exact-ML (or CSS-ML) likelihood scoring for ARIMA::score_order and SARIMA::score_order that doesn't shrink the comparison sample as model order grows -- a substantial numerical-methods change outside this session's remaining budget. A human/future-session decision is needed on how to resource that work."
    verification:
      - kind: integration
        ref: "tests/auto_arima_r_reference.rs#auto_arima_airpassengers_matches_r (ignored, documented)"
        status: fail
  - id: D4
    description: "PR #223 updated with this session's evidence and findings; remains draft because D3 is unresolved -- gh pr ready was intentionally NOT run"
    requirement: null
    human_judgment: false
    verification:
      - kind: other
        ref: "gh pr view 223 --repo sipemu/anofox-forecast --json isDraft,state -> {\"isDraft\":true,\"state\":\"OPEN\"}"
        status: pass

duration: ~30min
completed: 2026-10-10
status: halted
---

# Phase 11 Plan 05: AutoARIMA Default-Search Evidence Gathering (Known Gap Re-Confirmed, Not Resolved) Summary

**Attempted to flip AutoARIMA's default search to R's own Hyndman-Khandakar stepwise with R's real bounds (max.P=2, max.Q=2) per D-06 and the 2026-10-10 user decision; found by direct testing that this re-exposes the 11-04 Known Gap (a per-candidate AICc scoring-window bug) on AirPassengers, a real dataset -- not just the synthetic RW/AR(0.7) Monte-Carlo fixtures -- and confirmed with a direct `Rscript forecast::auto.arima` run on M4-Daily D4047 that R itself is not in the gate's ">2x" regime there (R: `ARIMA(0,1,0)`, MAE 141.5, ratio 1.0x), so the gap cannot be closed by adjusting the gate. Kept the safe, additive subset (`max_order` field, `with_grid_stepwise()` builder, a genuinely redundant fallback removal) and reverted the risky default flip; the PR stays draft.**

## Performance

- **Duration:** ~30 min of active investigation (plus upfront reading of all required context)
- **Tasks:** 1 of 3 planned tasks attempted; Task 1's tracer gate did not pass as specified, so Tasks 2 (re-validate selection frequencies/M4 gate under the new default) and 3 (CHANGELOG, DoD, push, mark PR ready) were not executed per the tracer-gate halt protocol
- **Files modified:** 3 (upstream crate repo: `src/models/arima/auto_arima.rs`, `tests/auto_arima_r_reference.rs`, `CHANGELOG.md`); this repo: 1 (this SUMMARY.md)

## Accomplishments

- **`AutoARIMAConfig::max_order`** (R `auto.arima`'s `max.order`, default 5) added with rustdoc, enforced by `true_stepwise_search` on every start model and neighbour move via a new `order_within_max` filter — verified with an inline test (`default_config_has_max_order_and_grid_stepwise`).
- **`AutoARIMAConfig::with_grid_stepwise()`** builder added for explicit, symmetric opt-in to the fixed-grid search (the crate's current default), paired with the existing `with_true_stepwise()`.
- **Removed the pre-11-05 non-seasonal fallback** in `AutoARIMA::fit` (`has_seasonal_pattern`'s `s -> 0` zeroing): R `auto.arima` only drops seasonal AR/MA terms when `period < 2`; seasonal `P`/`Q` are now searched even when `nsdiffs_seas` yields `D = 0` for the configured period. This is independent of the scoring-window bug and verified safe: full `cargo test --all-features` (3190+ tests, 0 failed), including the M4 gate (3/3) and `auto_arima_validation`/`auto_arima_order_selection` (13/13) unaffected.
- **Re-confirmed and extended the 11-04 Known Gap with new evidence**, exactly as the 2026-10-10 user decision asked for:
  - Implemented the full default flip (`max_cap_p=2`, `max_cap_q=2`, `true_stepwise=true`) and tested it against `AutoARIMA::seasonal(12)` on AirPassengers. Found the crate's own AICc ranks an overfit `ARIMA(3,1,0)(2,1,0)[12]` (~829) far ahead of R's actual pick `ARIMA(2,1,1)(0,1,0)[12]` (~1002 under the same formula) — a ~170-unit gap. Verified directly against the full fitted model's own `.aic()` (not just the fast-scoring path), and verified the same bug persists even with the OLD `max_cap_p=1` bound (the overfit candidate just shifts to a non-seasonal `ARIMA(0,1,3)` instead), confirming this is the same root-cause mechanism as 11-04's Known Gap, not a new bug introduced by raising the seasonal bounds.
  - Per the user decision's point 2, ran `Rscript forecast::auto.arima` directly on M4-Daily series D4047's exact train/test split (`tests/data/m4_outliers.json`, horizon=14, frequency=7, matching `tests/m4_daily_accuracy_regression.rs`). **R itself selects `ARIMA(0,1,0)`, MAE = 141.5 — identical to the statsforecast baseline the gate compares against (ratio exactly 1.0x).** This directly answers the user decision's branching question: R is NOT "in the same regime" as the crate's overfit candidates on this series, so per point 3 the M4 gate's 2x tolerance is evidence-backed and must not be adjusted. The crate's search needs to be fixed to match R, not the other way around.
  - Added `auto_arima_airpassengers_matches_r` to `tests/auto_arima_r_reference.rs`, `#[ignore]`d with the full root-cause explanation, following the same honest-disclosure pattern 11-04 established for its two Monte-Carlo tests (not deleted, not loosened).
- **Reverted the risky default flip** back to the pre-11-05 values (`max_cap_p=1`, `max_cap_q=1`, `true_stepwise=false`) after the evidence above showed it ships a regression without the companion scoring fix — consistent with the user decision's explicit fallback ("stop and report precisely... rather than shipping a regression").
- Updated `CHANGELOG.md`'s `[0.16.0] - Unreleased` section: an `### Added` entry for `max_order`/`with_grid_stepwise()`, and an extension to the existing `### Known gap` entry documenting this session's re-verification (AirPassengers + the R evidence on D4047).
- Updated PR #223's body with this session's findings and evidence; **intentionally left as draft** — `gh pr ready` was NOT run, since the Known Gap (D3) is unresolved.
- Full DoD green on the final state: `cargo fmt --all -- --check` clean, `cargo clippy --all-features --all-targets -- -D warnings` clean (exit 0), `cargo test --all-features` 0 failed across every binary (3190+ lib tests, every integration suite, every doctest), `RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --all-features` clean.

## Why Task 1's tracer gate was not satisfied (and Tasks 2/3 were not executed)

Task 1 (`type="tracer"`) required: "The default AutoARIMA search is R's stepwise HK search with R's bounds; AirPassengers differencing matches R and R's order ranks in the top three." Per the tracer-feedback-gate protocol, this must be re-verified end-to-end before any expansion task runs; a failure here means HALT, never expand.

`d` and `D` DO match R (`d=1`, `D=1`) under the new defaults — that part of Task 1 is correct. But R's own order does NOT rank in the crate's top three scored candidates; instead, an overfit `ARIMA(3,1,0)(2,1,0)[12]` wins by a ~170-unit AICc margin that traces directly to the documented 11-04 Known Gap (comparing candidates over different effective sample sizes). This is not a new bug this session introduced — it is the SAME scoring-window asymmetry 11-04 already found and reverted for the RW/AR(0.7) Monte-Carlo fixtures, now shown to also break on a real, canonical dataset once R's real (wider) seasonal bounds are enabled. Per the deviation-handling and tracer-gate protocols, this is a HALT, not an auto-fixable bug within this plan's remaining scope: fixing it requires replacing CSS-based scoring with exact-ML (or CSS-ML) likelihood comparisons in both `ARIMA::score_order` and `SARIMA::score_order` — the exact kind of change 11-04 already spent ~5 hours attempting (four variants, all either failing the RW target or regressing the M4 gate) without success.

Given the budget guidance ("if the investigation exceeds ~4h without convergence, stop and report precisely... if not achievable in this plan, stop and report precisely (status incomplete) rather than shipping a regression"), and that 11-04 already spent that much time on precisely this mechanism without resolving it, this session did not re-attempt a full exact-ML rewrite. Instead it: (a) gathered the specific new evidence the user decision asked for (R's real M4 D4047 pick, confirming the gate is not loosenable), (b) shipped the safe, independently-verified subset of the plan's deliverables, and (c) reverted the part that depends on the unresolved root cause, rather than ship it with a documented-but-live regression.

## Task Commits

All code commits are in the **upstream crate repo** (`/home/simonm/projects/rust/anofox-forecast`), per the Phase 11 upstream protocol — this repo (`anofox-forecast` DuckDB extension) only gets this SUMMARY.md commit.

1. **Task 1 (partial) — `max_order`, `with_grid_stepwise()`, fallback removal, known-gap re-verification** — `1d2ad08` (feat)
2. **CHANGELOG update documenting the above and the re-confirmed gap** — `ae728e0` (docs)

**Plan metadata:** (this commit, docs: halt plan)

## Files Created/Modified

Upstream crate repo (`~/projects/rust/anofox-forecast`):
- `src/models/arima/auto_arima.rs` — `AutoARIMAConfig::max_order` field + rustdoc; `with_grid_stepwise()` builder; `order_within_max` enforcement inside `true_stepwise_search`; removed `has_seasonal_pattern` and the `s -> 0` fallback in `fit`; two new inline tests (`default_config_has_max_order_and_grid_stepwise`, `with_grid_stepwise_sets_grid_search`); `AutoARIMAConfig::default()` left at pre-11-05 values (`max_cap_p=1`, `max_cap_q=1`, `true_stepwise=false`) with an extensive doc comment explaining why, pointing here.
- `tests/auto_arima_r_reference.rs` — new `auto_arima_airpassengers_matches_r` test, `#[ignore]`d with the full root-cause explanation.
- `CHANGELOG.md` — `### Added` entry for the new config surface; extended `### Known gap` entry with this session's AirPassengers + D4047/R-evidence findings.

This repo (`~/projects/duckdb/anofox-forecast`):
- `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-05-SUMMARY.md` (this file)

## Decisions Made

See `key-decisions` in the frontmatter for the full account. In summary: implemented and tested the plan's required default flip, found concrete new evidence (AirPassengers + a direct R run on M4 D4047) that it ships a regression without a deeper scoring fix, and — per the user's own explicit fallback instruction — reverted the flip while keeping everything independently safe and useful (the `max_order` field, the builder, the fallback removal, and the now better-documented, evidence-backed Known Gap).

## Deviations from Plan

### Auto-fixed Issues (Rule 1 — safe, kept)

**1. [Rule 1 - Bug] Redundant non-seasonal fallback dropped seasonal P/Q search terms unnecessarily**
- **Found during:** Task 1, implementing R's "seasonal terms drop only when period<2" rule
- **Issue:** `AutoARIMA::fit` zeroed its local seasonal-period variable (forcing a fully non-seasonal search) whenever `has_seasonal_pattern`'s strength test was weak — even though `nsdiffs_seas` (the D-06 seasonal-differencing test, unified with `has_seasonal_pattern`'s own test since 11-04) already correctly yields `D=0` in exactly the same cases without needing to drop seasonal `P`/`Q` from the search space too.
- **Fix:** Removed the fallback and the now-dead `has_seasonal_pattern` function; seasonal `P`/`Q` terms are now searched whenever `period > 1`, matching R.
- **Files modified:** `src/models/arima/auto_arima.rs`
- **Verification:** Full `cargo test --all-features` (0 failed), M4 gate (3/3), `auto_arima_validation`/`auto_arima_order_selection` (13/13) all unaffected.
- **Committed in:** `1d2ad08`

### Reverted Attempt (documented per deviation protocol — see "Why Task 1's tracer gate was not satisfied" above for full detail)

**2. [Rule 4-adjacent — evidence-gated requirement, resolved per explicit user fallback] Default flip to R's Hyndman-Khandakar stepwise search with R's real bounds**
- **Found during:** Task 1, implementing the plan's headline deliverable
- **Issue:** Implementing the plan's own prescribed default (and the one the 2026-10-10 user decision explicitly asked to re-attempt with evidence) re-exposes the 11-04 Known Gap on AirPassengers, a real dataset — R's actual pick scores ~170 AICc units worse than an overfit alternative under the crate's current scoring.
- **Resolution:** Gathered the specific R evidence the user decision asked for (direct `Rscript` run on M4 D4047: R picks `ARIMA(0,1,0)`, MAE 141.5, ratio 1.0x — confirming the gate is evidence-backed and not adjustable), then reverted the default flip per the user decision's own explicit fallback clause, keeping the safe additive subset.
- **Files affected:** `src/models/arima/auto_arima.rs` (net: `AutoARIMAConfig::default()` behaviorally identical to `4ac0a97` on this axis), `tests/auto_arima_r_reference.rs` (one new test added, ignored, not deleted/loosened)
- **Verification:** Full `cargo test --all-features` (0 failed, 3190+ tests), `cargo test --test m4_daily_accuracy_regression` (3/3), `cargo fmt --all -- --check` / `cargo clippy --all-features --all-targets -- -D warnings` / `RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --all-features` all clean.
- **Committed in:** `1d2ad08` (the revert is part of the same commit as the kept fixes, since the final committed state reflects the decision after investigation)

---

**Total deviations:** 1 auto-fixed (Rule 1 bug, kept), 1 reverted attempt (evidence-gated requirement, fully documented with new evidence).
**Impact on plan:** UPST-01's `max_order`/`with_grid_stepwise()` additions and the redundant-fallback cleanup are shipped; the plan's headline numeric target (R's default HK search as the crate default, AirPassengers top-3) is NOT met. Tasks 2 and 3 (re-validate selection frequencies under the new default, CHANGELOG/DoD/push/mark-PR-ready) were not executed because Task 1's tracer gate did not pass.

## Issues Encountered

- **Shared working tree with a concurrent Phase 12 executor** (same pattern noted in 11-01/11-04-SUMMARY): the planning repo's working tree carried uncommitted changes from a parallel agent throughout this session (verified via `git status --porcelain` before the one commit made in this repo — only `11-05-SUMMARY.md` was staged, via `git commit --only`). No cross-contamination occurred.
- **R and the `forecast`/`urca` packages were available** (`Rscript`, `forecast` 9.0.2) in this environment, which let this session get real, targeted ground truth on M4 D4047 beyond what the checked-in fixture generator covers — recommended for any follow-up session tackling the exact-ML scoring fix, to validate against R directly rather than only the existing fixture.

## User Setup Required

None — no external service configuration required. Draft PR #223 is open and updated with this session's findings; per D-02 ("Branch + PR, you release"), this plan does not merge, publish, or bump the `Cargo.toml` version.

**Recommend the user explicitly weigh in before the next attempt at this gap**, since closing it requires a genuinely different scoring mechanism (exact-ML or CSS-ML likelihood, not CSS with a shrinking comparison window) for both `ARIMA::score_order` and `SARIMA::score_order` — a substantial numerical-methods change. 11-04 already found that the crate has an unused exact-likelihood "innovations algorithm" path (`arma_innovations_algo_fast`/`arma_innovations_filter` in `src/models/arima/model.rs`) that got closest in a prior attempt (98% RW share, 2.35x on D4047 — still over the 2x gate but much closer than the CSS-common-window variants' 3.88x). A focused follow-up wiring that path into both scorers' `(score_with, score_without)` comparisons, and re-testing against AirPassengers + the full M4 10-series set + the RW/AR Monte-Carlo fixtures together, is the most promising next step identified across both sessions.

## Next Phase Readiness

- Draft PR #223 (`fix/auto-arima-order-selection` -> `main`) is open, DoD-green, updated with this session's findings; remains draft because the Known Gap is unresolved.
- The R fixture (`tests/data/r_reference/auto_arima_r.json`), the three `#[ignore]`d known-gap tests (`auto_arima_rw_share_near_r`, `auto_arima_selection_frequencies`, `auto_arima_airpassengers_matches_r`), and this session's direct `Rscript` verification on M4 D4047 are all ready for whichever follow-up session attempts the exact-ML/CSS-ML scoring fix — no further fixture or evidence-gathering work is needed before that attempt.
- Phase 17 (upstream crate consumption) remains blocked on a released `anofox-forecast` version including this PR's commits, once merged — tracked as the standing UPST-06 blocker carried forward from 11-01/11-02/11-03/11-04.

---
*Phase: 11-upstream-anofox-forecast-correctness-fixes*
*Completed: 2026-10-10*

## Self-Check: PASSED

- FOUND: /home/simonm/projects/rust/anofox-forecast/src/models/arima/auto_arima.rs (max_order field present)
- FOUND: /home/simonm/projects/rust/anofox-forecast/tests/auto_arima_r_reference.rs (auto_arima_airpassengers_matches_r present, ignored)
- FOUND: commit 1d2ad08 in git log --oneline --all (upstream repo)
- FOUND: commit ae728e0 in git log --oneline --all (upstream repo)
- FOUND: PR #223 https://github.com/sipemu/anofox-forecast/pull/223 (isDraft: true, state: OPEN)
