---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 03
subsystem: infra
tags: [rust, theta, forecast, thetaf, statsforecast, upstream-crate, github-pr]

requires:
  - phase: 11-01
    provides: "11-upstream-base.env (BASE_REF=1ae5cfc13ef98afdbb17e27def95a632cbb74fe7, PR_BASE=main, pristine baseline with 0 pre-existing test failures)"
provides:
  - "Upstream branch fix/theta-alpha-intervals, pushed, with PR #222 open against sipemu/anofox-forecast main"
  - "Theta::new()/with_theta()/seasonal()/seasonal_with_decomposition() now estimate alpha and the initial SES level by SES SSE minimisation (thetaf's ets(y,\"ANN\",opt.crit=\"mse\") convention); drift and interval formulas match forecast::thetaf"
  - "Checked-in R fixture generator (validation/reference/r/theta.R) and fixture (tests/data/r_reference/theta_thetaf.json) proving the crate's Theta STM against R forecast 9.0.2's thetaf on rw/trend/ar/air series"
  - "Theta::with_theta_value signature changed: (theta, alpha, level, b, n) -- n is now required for the drift's finite-sample term"
affects: [17-upstream-crate-consumption, diagnostics-sql-parity]

actuals:
  tokens: 20620
  tasks: 2
  commits: 2
  plan_head_before: 69a2ff60b567ce62bb7a220605bca5a257032608

tech-stack:
  added: []
  patterns:
    - "Checked-in R fixture generator under validation/reference/r/ (same pattern 11-02 established: --file= script-dir resolution, provenance block with regenerate command + tool_versions, jsonlite null=\"null\" to avoid NULL->{} encoding of optional fields)"
    - "Local, more-precise alternative to a crate-wide approximation when a feature's own parity bar demands it (Theta's normal_quantile via statrs::distribution::Normal, instead of the crate-wide utils::stats::quantile_normal Abramowitz-Stegun approximation which is only ~1e-4 accurate) -- kept scoped to the one file in files_modified rather than touching the shared utility"

key-files:
  created:
    - /home/simonm/projects/rust/anofox-forecast/validation/reference/r/theta.R
    - /home/simonm/projects/rust/anofox-forecast/tests/data/r_reference/theta_thetaf.json
    - /home/simonm/projects/rust/anofox-forecast/tests/theta_thetaf_reference.rs
  modified:
    - /home/simonm/projects/rust/anofox-forecast/src/models/theta/model.rs
    - /home/simonm/projects/rust/anofox-forecast/src/models/theta/auto.rs
    - /home/simonm/projects/rust/anofox-forecast/CHANGELOG.md

key-decisions:
  - "Confirmed Q1 from research (grep auto.rs:187-189): AutoTheta's STM candidate is built from Theta::new()/Theta::seasonal_with_decomposition(); OTM from the separate OptimizedTheta. The default-alpha change propagates to STM as intended; OTM stays distinct (proven by auto_theta_candidates_distinct)."
  - "Theta::with_theta_value(theta, alpha, level, b) -> (theta, alpha, level, b, n): the thetaf drift formula's finite-sample term (1-(1-alpha)^n)/alpha genuinely depends on n (the training length), which the old warm-start constructor had no slot for (it defaulted n=0, silently zeroing the correction term). Discovered via theta_extract_params_then_warm_start's regression after the drift-formula fix -- not a pre-existing bug, a new requirement this plan's own formula fix introduced. Added n as a required parameter and to fitted_params()'s output map so round-tripping works without a separate lookup."
  - "Replaced Theta's interval z-value source (crate-wide utils::stats::quantile_normal, an Abramowitz-Stegun approximation accurate to only ~1e-4) with a local normal_quantile() using statrs::distribution::Normal::inverse_cdf (already imported in this file for the seasonal ACF test). Required to hit the plan's own 1e-8-relative thetaf-parity bar for prediction intervals; kept local to theta/model.rs rather than touching the shared utility, since utils/stats.rs is outside this plan's files_modified and P9's scope guard."
  - "Reverted doc-comment updates to src/models/theta/mod.rs and src/models/theta/optimized.rs (both said 'STM fixed alpha=0.1', now stale) after discovering they sit outside this plan's files_modified list. Left as a deferred doc-accuracy item below rather than widening scope past the plan's declared file set / P9's scope guard."
  - "Branched fix/theta-alpha-intervals from 1ae5cfc per 11-01's D-01a (NOT from fix/adf-kpss-correctness, which was the repo's checked-out branch at session start) -- confirmed via git switch -c ... 1ae5cfc + merge-base --is-ancestor before any work."

patterns-established: []

requirements-completed: [UPST-04]

coverage:
  - id: D1
    description: "Theta::new() (STM) estimates alpha and the initial level jointly by minimising in-sample SES SSE, matching forecast::thetaf's ets(y,\"ANN\",opt.crit=\"mse\"); alpha within 0.01 of R and crate SSE at its optimum <= R's SSE on the rw fixture"
    requirement: UPST-04
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/theta_thetaf_reference.rs#theta_stm_matches_thetaf_rw"
        status: pass
  - id: D2
    description: "Point forecasts follow thetaf's drift formula (1-(1-alpha)^n)/alpha and prediction intervals follow thetaf's se_h = sqrt(sigma2*(1+alpha^2*(h-1))); reproduces R's point/lower/upper to 1e-8 relative at R's own fitted parameters, for rw/trend/ar/air"
    requirement: UPST-04
    human_judgment: false
    verification:
      - kind: unit
        ref: "src/models/theta/model.rs#thetaf_formula_at_r_parameters"
        status: pass
  - id: D3
    description: "End-to-end fit+formula on all four fixture series (rw/trend/ar non-seasonal, air seasonal multiplicative): alpha within 0.01, point within 0.5% (1% air), bounds within 1% (3% air) of thetaf; random-walk bias removed (h=1 point -21.78656053 vs R -21.78656052)"
    requirement: UPST-04
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/theta_thetaf_reference.rs#theta_stm_matches_thetaf"
        status: pass
  - id: D4
    description: "In-sample fitted values match thetaf's fitted(theta_model) within 1% relative after the first observation"
    requirement: UPST-04
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/theta_thetaf_reference.rs#theta_fitted_convention_matches_thetaf"
        status: pass
  - id: D5
    description: "AutoTheta still evaluates distinct STM/OTM candidates (different model_scores on AirPassengers) after the default-alpha change; Theta::with_theta_value keeps its given fixed alpha through fit()"
    requirement: UPST-04
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/theta_thetaf_reference.rs#auto_theta_candidates_distinct"
        status: pass
  - id: D6
    description: "Branch fix/theta-alpha-intervals pushed; PR #222 open against sipemu/anofox-forecast main; full local DoD green (fmt, clippy -D warnings, cargo test --all-features 0 failed across every binary, cargo doc -D warnings); CHANGELOG has a [0.16.0] - Unreleased entry with before/after numbers"
    requirement: null
    human_judgment: false
    verification:
      - kind: other
        ref: "git ls-remote --heads origin fix/theta-alpha-intervals (one line); gh pr list --repo sipemu/anofox-forecast --head fix/theta-alpha-intervals --state open -> PR #222 https://github.com/sipemu/anofox-forecast/pull/222"
        status: pass
      - kind: other
        ref: "cargo fmt --all -- --check; cargo clippy --all-features --all-targets -- -D warnings; cargo test --all-features (every test binary: 0 failed, 3188 lib tests + all integration suites); RUSTDOCFLAGS=\"-D warnings\" cargo doc --no-deps --all-features"
        status: pass

duration: ~70min
completed: 2026-10-09
status: complete
---

# Phase 11 Plan 03: Theta STM Estimates Alpha by SES Optimisation; thetaf Drift and Intervals Summary

**Standard Theta Model (STM) now estimates alpha and the initial SES level by joint SSE minimisation exactly like R `forecast::thetaf`, and its drift and prediction-interval formulas match thetaf's own conventions — removing the random-walk forecast bias (rw h=1 point now -21.78656053 vs thetaf's -21.78656052) and the ~2x-too-wide h>=6 intervals, delivered as upstream PR #222.**

## Performance

- **Duration:** ~70 min
- **Tasks:** 2
- **Files modified:** 7 (upstream crate repo: 3 created, 4 modified — 1 of those 4 was the shared P4 baseline-lint patch; this repo: 1 created — this SUMMARY.md)

## Accomplishments

- Replaced the 1-D alpha-only optimiser with `optimize_alpha_level(series)`, which jointly minimises `SSE = sum((y_t - l_{t-1})^2)` over alpha AND a free initial level `l0` via multi-start Nelder-Mead (alpha0 in {0.1, 0.5, 0.9}) — exactly R `forecast::thetaf`'s `ets(y, "ANN", opt.crit = "mse")` semantics, where every observation contributes (including the first), unlike the old series[0]-pinned recursion.
- `Theta::new()`/`with_theta()`/`seasonal()`/`seasonal_with_decomposition()` now default to `optimize=true, alpha=None` (was fixed `alpha=0.1, optimize=false`). `with_alpha()` (explicit user-fixed alpha) and `with_theta_value` (warm-start) stay fixed, per the plan's requirement.
- Fixed the drift formula: `(1 - 1/theta) * b * ((h-1) + (1-(1-alpha)^n)/alpha)` (n = training length), replacing the old `1/alpha` asymptotic approximation that ignored the finite-sample correction.
- Fixed the prediction-interval formula: `se_h = sqrt(sigma2 * (1 + alpha^2*(h-1)))`, replacing an AR(1)-style geometric-sum factor that was ~2x too wide at h>=6 per the upstream validation audit. `sigma2 = SSE/(n-2)` is now computed on the deseasonalized series at the SES optimum (thetaf's `ets$sigma2` convention), not from post-hoc reseasonalized residuals.
- Confirmed Q1 from research: AutoTheta's STM candidate is built from `Theta::new()`/`Theta::seasonal_with_decomposition()` (`auto.rs:187-189`); OTM from the separate `OptimizedTheta`. The default-alpha change propagates to STM as intended; OTM stays distinct — proven by a new test.
- Wrote a checked-in R fixture generator (`validation/reference/r/theta.R`, R `forecast` 9.0.2) covering rw/trend/ar/air (seasonal) series, feeding 5 new tests (1 inline in `model.rs`, 4 integration in `tests/theta_thetaf_reference.rs`) — all passing, including an exact-formula test matching thetaf's point/lower/upper to **1e-8 relative** at R's own fitted parameters.
- Measured before/after numbers directly (see Decisions / CHANGELOG): rw alpha moves from the old fixed `0.1` to `0.9999` (R: `0.999899989605684`); rw h=1 point `-21.78656053` vs R `-21.78656052`; air h=12 95% half-width `62.26268` vs R `62.25325`.
- Pushed branch `fix/theta-alpha-intervals` and opened **PR #222** (https://github.com/sipemu/anofox-forecast/pull/222) against `sipemu/anofox-forecast:main`, with full local DoD green.

## Task Commits

All task commits are in the **upstream crate repo** (`/home/simonm/projects/rust/anofox-forecast`), per the Phase 11 upstream protocol — this repo (`anofox-forecast` DuckDB extension) only gets this SUMMARY.md commit.

1. **P4 baseline lint (shared protocol step, before Task 1):** `8f5e0e5` (style) — applied `11-baseline-lint.patch` from 11-01, no behaviour change
2. **Task 1: Tracer — branch, thetaf fixture, STM estimates (alpha, l0) by SES SSE; random-walk forecast proven against thetaf** — `b6fd938` (feat)
3. **Task 2: thetaf drift factor and intervals, seasonal convention, all-series fixture tests, docs, CHANGELOG, DoD, push and open PR** — `42f4ee2` (fix)

**Plan metadata:** (this commit, docs: complete plan)

## Files Created/Modified

Upstream crate repo (`~/projects/rust/anofox-forecast`):
- `validation/reference/r/theta.R` (new) — thetaf fixture generator: rw (random walk, n=200), trend (n=120), ar (AR(0.7), n=300), air (AirPassengers, seasonal multiplicative); `forecast:::theta_model`/`forecast:::forecast.theta_model` at h=24, level=c(80,95)
- `tests/data/r_reference/theta_thetaf.json` (new) — fixture, regeneration-verified byte-identical
- `tests/theta_thetaf_reference.rs` (new) — 4 passing integration tests: `theta_stm_matches_thetaf_rw`, `theta_stm_matches_thetaf`, `theta_fitted_convention_matches_thetaf`, `auto_theta_candidates_distinct`
- `src/models/theta/model.rs` — `optimize_alpha_level` (replaces `optimize_alpha`), `calculate_sse` gains a free `l0` arg, `normal_quantile` (statrs-backed exact normal inverse-CDF, replaces the crate-wide approximation locally in Theta), drift and interval formulas fixed, `with_theta_value` gains required `n: usize`, `fitted_params()` gains `"n"`, inline test `thetaf_formula_at_r_parameters`, updated `theta_stm_uses_fixed_alpha` -> `theta_stm_estimates_alpha_by_ses_optimization` (per P7, old test asserted the now-incorrect fixed-0.1 behaviour)
- `src/models/theta/auto.rs` — doc comments (lines 17, 53) updated from "fixed alpha=0.1" to "alpha estimated by SES optimisation (thetaf)"
- `CHANGELOG.md` — new `[0.16.0] - Unreleased` section (`### Fixed`) with before/after numbers, including a note that sibling Phase 11 PRs will add their own blocks under the same heading

This repo (`~/projects/duckdb/anofox-forecast`):
- `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-03-SUMMARY.md` (this file)

## Decisions Made

- Confirmed Q1 (research open question) via `grep -n 'Theta::new\|Theta::seasonal' auto.rs`: AutoTheta's STM candidate comes from `Theta::new()`/`Theta::seasonal_with_decomposition()` (lines 187-189); OTM from `OptimizedTheta::new()`/`seasonal_with_decomposition()` (lines 202/207) — a fully independent struct that already optimises both alpha and theta. The STM default-alpha fix therefore propagates correctly to AutoTheta's STM candidate without touching OTM.
- **`with_theta_value` signature change** (`(theta, alpha, level, b)` -> `(theta, alpha, level, b, n)`): discovered via a regression in the pre-existing `theta_extract_params_then_warm_start` test after the drift-formula fix — the new drift term `(1-(1-alpha)^n)/alpha` genuinely needs the training sample size `n`, which the old warm-start constructor silently defaulted to `0` (zeroing the correction entirely). This is not a pre-existing bug; it's a new requirement this plan's own formula fix introduces, so the constructor needed to grow a parameter rather than the fix being deferred. Only 3 call sites exist, all within `model.rs`'s own tests (confirmed via repo-wide grep); no external callers in this crate. `fitted_params()` now also reports `"n"` so round-tripping doesn't need a separate lookup.
- **Local `normal_quantile` instead of touching the shared `utils::stats::quantile_normal`:** the plan's own acceptance bar ("reproduces R's point/lower/upper to 1e-8 relative") is unreachable through the crate-wide Abramowitz-Stegun approximation (its own test asserts only `epsilon = 0.01`). Fixing the shared utility would improve every model that uses it but would also widen this plan's diff outside `files_modified` and risk unrelated behavior changes across Croston/SES/Holt/etc. Added a small `normal_quantile()` in `theta/model.rs` backed by `statrs::distribution::Normal::inverse_cdf` (already imported in this file) instead — correct, in-scope, and the three `quantile_normal` call sites in Theta (`predict_with_intervals`, `predict_with_exog_intervals`, `fitted_values_with_intervals`) now use it.
- **Reverted doc-comment updates to `mod.rs`/`optimized.rs`:** both files still say "STM ... fixed alpha=0.1" (now stale wording, not incorrect-breaking) in their module/struct doc comments. I initially updated both for consistency with the `auto.rs` doc fixes, then reverted when I noticed neither file is in this plan's declared `files_modified` and P9's scope guard (`git diff --name-only BASE_REF..HEAD` must be a subset of `files_modified`) is a literal protocol gate for this delivery, not just a style preference. Logged below as a deferred doc-accuracy item rather than silently widening scope.
- Branched `fix/theta-alpha-intervals` from `1ae5cfc` (BASE_REF per 11-01's D-01a) — explicitly NOT from `fix/adf-kpss-correctness`, which was the repo's checked-out branch at session start (clean, pushed, per 11-02). Verified via `git switch -c fix/theta-alpha-intervals 1ae5cfc...` + `git merge-base --is-ancestor BASE_REF HEAD` before any work, per P3.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] jsonlite encodes R `NULL` list values as `{}`, not `null`, breaking the fixture's `seas_component` field for non-seasonal series**
- **Found during:** Task 1, first `cargo test` run against the freshly generated fixture
- **Issue:** `tests/theta_thetaf_reference.rs`'s `Option<Vec<f64>>` field failed to deserialize `rw`'s `seas_component` with `invalid type: map, expected a sequence` — `jsonlite::write_json`'s default `null` handling encodes an R `NULL` inside a `list()` as the JSON object `{}`, not the JSON `null` token.
- **Fix:** Added `null = "null"` to `write_fixture()`'s `jsonlite::write_json(...)` call in `validation/reference/r/theta.R`.
- **Files modified:** `validation/reference/r/theta.R`, `tests/data/r_reference/theta_thetaf.json` (regenerated)
- **Verification:** Fixture confirmed `"seas_component": null` for rw/trend/ar afterward; `cargo test --test theta_thetaf_reference theta_stm_matches_thetaf_rw` passed.
- **Committed in:** `b6fd938` (Task 1 commit)

**2. [Rule 1 - Bug] `with_theta_value`'s silently-zeroed `n` broke the pre-existing warm-start round-trip test after the drift-formula fix**
- **Found during:** Task 2, `cargo test --lib models::theta` after landing the drift/interval formula changes
- **Issue:** `theta_extract_params_then_warm_start` failed (`24.849... != 24.699...`) — the new drift formula's `(1-(1-alpha)^n)/alpha` term evaluates to `0` when `n=0` (the old `with_theta_value`'s hard-coded value), so a warm-started model built from `fitted_params()` could no longer reproduce the originating model's forecasts.
- **Fix:** Added a required `n: usize` parameter to `with_theta_value`, stored it in the struct's existing `n` field (previously hard-coded to `0` in this constructor only), and added `"n"` to `fitted_params()`'s output map. Updated the 3 in-file call sites (`theta_warm_start_predict_without_fit`, `theta_extract_params_then_warm_start`, `theta_warm_start_fit_refines`).
- **Files modified:** `src/models/theta/model.rs`
- **Verification:** `cargo test --lib models::theta` — 90 passed, 0 failed, after the fix.
- **Committed in:** `42f4ee2` (Task 2 commit)

**3. [Rule 1 - Bug] Theta's interval z-values via the crate-wide `quantile_normal` approximation were not precise enough for the plan's own 1e-8-relative thetaf-parity bar**
- **Found during:** Task 2, writing `thetaf_formula_at_r_parameters`
- **Issue:** `thetaf_formula_at_r_parameters`'s lower/upper assertions failed by ~8e-6 relative (well above the 1e-8 bar) — `utils::stats::quantile_normal` is an Abramowitz-Stegun rational approximation (its own test only asserts `epsilon = 0.01`), not precise enough for an interval-bound comparison that scales any z-value error by the full standard error.
- **Fix:** Added `normal_quantile()` to `theta/model.rs`, backed by `statrs::distribution::Normal::inverse_cdf` (statrs was already an import in this file for the seasonal ACF test's `z_90` threshold); replaced the 3 `quantile_normal` call sites in Theta's interval methods.
- **Files modified:** `src/models/theta/model.rs`
- **Verification:** `thetaf_formula_at_r_parameters` passes at `<= 1e-8` relative for all four series, both 80% and 95% levels, h=1..24.
- **Committed in:** `42f4ee2` (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (3 bugs — Rule 1, all either fixture-generation plumbing or correctness consequences of this plan's own formula fix).
**Impact on plan:** All three were necessary to produce the fixture-backed, 1e-8-accurate tests the plan's `must_haves.truths` require, and to keep a pre-existing warm-start test passing honestly (updated, not loosened or deleted, per P7). No scope creep — each is scoped to the Theta model files already in `files_modified`.

## Known Stubs / Deferred Doc Accuracy

- `src/models/theta/mod.rs` (module doc, line 8) and `src/models/theta/optimized.rs` (struct doc, lines ~21-22) in the upstream crate still describe STM as having "fixed alpha=0.1" / "fixed parameters" — now stale wording (STM estimates alpha by SES optimisation since this PR), but not incorrect-breaking (nothing in those files' doctests or behavior depends on the wording). Not fixed here because both files sit outside this plan's declared `files_modified` and touching them would violate P9's scope guard before push. Low-risk, two-line fix for a future doc-pass PR; not blocking.
- `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` in **this repo** (the DuckDB extension, not the upstream crate) pins the old fixed-alpha=0.1 coincidental Theta/Croston fitted-value equality (flagged in Phase 10's `deferred-items.md`). That test will need updating once the extension consumes a released `anofox-forecast` >= 0.16.0 — tracked as a Phase 17 item (UPST-06 dependency), not touched by this plan.

## Issues Encountered

None beyond the three auto-fixed deviations above.

## User Setup Required

None — no external service configuration required. PR #222 is open for the user to review, request changes on, or merge on their own schedule (per D-02, "Branch + PR, you release"). This plan does not merge, publish, or bump the `Cargo.toml` version.

## Next Phase Readiness

- PR #222 (`fix/theta-alpha-intervals` -> `main`) is open, DoD-green, with before/after evidence. The user should review and merge it alongside PR #220 (11-01 sync) and PR #221 (11-02 ADF/KPSS) before cutting a `0.16.0` release — all three (plus any further Phase 11 fix PRs) will show CHANGELOG conflicts under the shared `[0.16.0] - Unreleased` heading, which is expected and resolved by keeping all blocks.
- `Theta::with_theta_value`'s signature change `(theta, alpha, level, b)` -> `(theta, alpha, level, b, n)` is a breaking change for any downstream caller — none exist in this crate today, but Phase 17 (which will bump the extension's pin to consume 0.16.0) should grep the extension's `crates/anofox-fcst-core` for any `with_theta_value` usage before upgrading.
- Phase 17 (upstream crate consumption, SQL diagnostics parity) depends on a released `anofox-forecast` version that includes this PR's commits — tracked as the standing UPST-06 blocker carried forward from 11-01/11-02.
- The two deferred doc-accuracy items above (`mod.rs`/`optimized.rs` stale wording) are low-risk and can be folded into whichever future PR next touches those files, or picked up in a dedicated doc-pass PR.

---
*Phase: 11-upstream-anofox-forecast-correctness-fixes*
*Completed: 2026-10-09*
