---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 06
subsystem: infra
tags: [rust, ets, exponential-smoothing, likelihood, upstream-crate, r-reference]

requires:
  - phase: 11-01
    provides: "BASE_REF=1ae5cfc, PR_BASE=main, 11-baseline-lint.patch, pristine 0-failure baseline"
provides:
  - "Branch fix/ets-likelihood-intervals created from BASE_REF, baseline lint commit applied (4f98c1f)"
  - "ets.rs: multiplicative-error likelihood now accumulates Sigma ln|y-hat_t| (R/statsforecast convention) over ALL observations, not Sigma ln|y_t| with a first-season skip for seasonal models"
  - "ets.rs: corrected Hyndman innovations-state-space recursion for every (trend x multiplicative-seasonal) combination under BOTH error types, replacing the classical 'y/s desaesonalized' Holt-Winters update that does not match R for seasonal models"
  - "ETS::with_params_and_states(...) -> Self: fixed params + fixed initial states, no optimisation -- for scoring a model at externally supplied (R) parameters"
  - "R reference fixture validation/reference/r/ets.R + tests/data/r_reference/ets_r.json: 18 AirPassengers + 6 non-seasonal + 2 AutoETS + 5 ses/holt/hw fits (also consumed by plans 11-07/11-08)"
  - "tests/ets_r_reference.rs: fixture loader, R-state-to-crate-buffer mapping helper, ets_loglik_at_r_params_mam (passing)"
affects: [11-07, 11-08, 17-upstream-crate-consumption]

actuals:
  tokens: 69800
  tasks: 1
  commits: 2
  plan_head_before: 1ae5cfc13ef98afdbb17e27def95a632cbb74fe7

tech-stack:
  added: []
  patterns:
    - "Validate a corrected recursion formula against R numerically (manual Python re-implementation of the proposed update equations against the exact fixture values) BEFORE committing the Rust change, rather than trusting a textbook derivation in isolation -- caught an error in a first-pass manual derivation (wrong denominator in the seasonal-additive-error update) before it shipped."
    - "Floor a log-objective's argument (sum_sq_errors.max(1e-300)) instead of rejecting a perfect fit outright -- a perfect fit on a constant series is the global optimum of a likelihood objective, not a degenerate candidate to reject."

key-files:
  created:
    - /home/simonm/projects/rust/anofox-forecast/validation/reference/r/ets.R
    - /home/simonm/projects/rust/anofox-forecast/tests/data/r_reference/ets_r.json
    - /home/simonm/projects/rust/anofox-forecast/tests/ets_r_reference.rs
  modified:
    - /home/simonm/projects/rust/anofox-forecast/src/models/exponential/ets.rs

key-decisions:
  - "Scope-limited the recursion fix to exactly the arms Task 1's own test exercises plus their direct structural siblings: TrendType::{None,Additive,AdditiveDamped} x SeasonalType::Multiplicative, for BOTH error types (is_mult_error branch added). Did NOT touch SeasonalType::{None,Additive} arms' recursion (only their likelihood accumulator, via the shared macro fix) -- those combinations are unvalidated against R and are Task 2's job (ets_loglik_at_r_params_all_models covers all 18+6)."
  - "Validated the new (M,A,M) recursion (l_t=(l+b)(1+alpha*e), b_t=b+beta*(l+b)*e, s_t=s_{t-m}(1+gamma*e)) against R's exact fitted values for AirPassengers to full float precision (6 decimal places, effectively machine epsilon) via a standalone Python re-implementation reading the R fixture directly, BEFORE writing the Rust change -- this caught that R's 'beta'/'gamma' for multiplicative-error models are NOT reparametrized (unlike the non-seasonal additive-trend case, where R's reported 'beta' is beta* = beta_SSOE/alpha, a distinction that cost significant analysis time to resolve)."
  - "Also fixed the SAME three arms' additive-error (is_mult_error=false) seasonal update to use the pre-update level (or level+trend) in the denominator, not the just-reassigned one -- this was a latent bug independent of error type (confirmed algebraically against the Hyndman table), left in place previously because it happened to not affect the level update's correctness, only the seasonal state's."
  - "Floored sum_sq_errors to 1e-300 before taking its log in both calculate_likelihood_with_init_buf and fit(), rather than rejecting (sum_sq_errors<=0.0 => f64::MAX) as an early draft did -- the reject-on-zero guard broke models::mstl_forecaster::tests::constant_series_produces_constant_forecast (an ETS sub-fit on a perfectly-constant series has sum_sq_errors==0 exactly, which is the GLOBAL OPTIMUM of the likelihood, not a degenerate candidate). Caught by running cargo test --all-features before committing, per the plan's P8 DoD gate."
  - "Marked this SUMMARY status: halted, not complete, per the #2830 convention 11-05 also used: Task 1 (tracer) is done and its own verify block passes exactly as specified, but Tasks 2 and 3 were not started. No code is uncommitted or broken -- the branch is clean and all of cargo test --all-features / fmt / clippy pass -- this is an intentional stopping point at a sound commit boundary, not a failure state."

patterns-established:
  - "When a plan's <interfaces> section states a buffer-index mapping derived at planning time, re-verify it numerically against the actual generated fixture before trusting it in a test -- the mapping in this plan's <interfaces> (buffer[k] = par s_{m-1-k}) was correct, but a first manual read of the printed JSON mis-indexed the par array and produced a false contradiction that cost time to resolve by re-deriving from the fixture directly."

requirements-completed: []

coverage:
  - id: D1
    description: "Branch fix/ets-likelihood-intervals created from BASE_REF (1ae5cfc); baseline lint patch applied as its own commit; branch left clean except the pre-existing untracked .planning/state.json"
    human_judgment: false
    verification:
      - kind: other
        ref: "git branch --show-current == fix/ets-likelihood-intervals; git merge-base --is-ancestor 1ae5cfc HEAD; git status --porcelain shows only '?? .planning/state.json'"
        status: pass
  - id: D2
    description: "ets_likelihood_loop! macro (both arms) accumulates ln|one-step forecast| for multiplicative error, not ln|y|"
    requirement: UPST-02
    human_judgment: false
    verification:
      - kind: unit
        ref: "grep -c '_fc.abs().ln()' src/models/exponential/ets.rs -> 2"
        status: pass
  - id: D3
    description: "calculate_likelihood_with_init_buf and fit() score every observation from t=0 (no first-period skip for seasonal models); objective/log_likelihood/aic/aicc/bic follow R/statsforecast's convention"
    requirement: UPST-02
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/ets_r_reference.rs#ets_loglik_at_r_params_mam -- loglik/aic/aicc/bic within 1e-6 relative of R forecast::ets(MAM) on AirPassengers at R's own fitted parameters"
        status: pass
  - id: D4
    description: "ETS::with_params_and_states(spec, period, alpha, beta, gamma, phi, level, trend, seasonals) -> Self added; no optimisation, values used as given"
    requirement: UPST-02
    human_judgment: false
    verification:
      - kind: unit
        ref: "grep -c 'pub fn with_params_and_states' src/models/exponential/ets.rs -> 1; exercised by ets_loglik_at_r_params_mam"
        status: pass
  - id: D5
    description: "Fitted values and final level/trend/seasonal states for ETS(M,A,M) on AirPassengers at R's own parameters match R to 1e-8 relative"
    requirement: UPST-02
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/ets_r_reference.rs#ets_loglik_at_r_params_mam -- fitted[0..143], final level/trend/12 seasonals all within 1e-8 relative"
        status: pass
  - id: D6
    description: "R reference fixture (validation/reference/r/ets.R, tests/data/r_reference/ets_r.json) covering 18 AirPassengers + 6 non-seasonal + 2 AutoETS + 5 ses/holt/hw fits, for this plan and 11-07/11-08"
    human_judgment: false
    verification:
      - kind: other
        ref: "Rscript validation/reference/r/ets.R regenerated the fixture cleanly; air_auto method/aicc (ETS(M,Ad,M), 1400.638) matches the plan's <must_haves> target (R: 1400.64)"
        status: pass
  - id: D7
    description: "Full regression safety: cargo test --all-features (0 failures across all 38 test binaries, matching the 11-01 pristine baseline), cargo fmt --all -- --check, cargo clippy --all-features --all-targets -- -D warnings all clean"
    human_judgment: false
    verification:
      - kind: other
        ref: "cargo test --all-features -> 'test result: ok' with 0 failed on every binary; cargo fmt --all -- --check exits 0; cargo clippy ... -D warnings exits 0 with no warnings"
        status: pass
  - id: D8
    description: "Task 2 (usual parameter constraints across every optimizer branch; ets_loglik_at_r_params_all_models/ets_usual_constraints_hold/ets_optimised_fit_quality/ets_mam_parameters_near_r) -- NOT STARTED"
    human_judgment: true
    verification: []
    rationale: "Halted for context-budget reasons after Task 1's tracer verify passed; no attempt was made, nothing to automate-verify"
  - id: D9
    description: "Task 3 (AutoETS R-restricted candidate pool, ETSSpec::is_r_restricted/MAA+MAdA validity, CHANGELOG, DoD, push, draft PR) -- NOT STARTED"
    human_judgment: true
    verification: []
    rationale: "Halted for context-budget reasons; branch has not been pushed and no PR exists"

duration: ~2h40min
completed: 2026-10-10
status: halted
---

# Phase 11 Plan 06: ETS Likelihood Correctness (Part 1/3 of fix/ets-likelihood-intervals) Summary

**Task 1 (tracer) complete and verified: the ETS multiplicative-error likelihood now matches R forecast::ets's loglik/aic/aicc/bic/fitted/final-states to 1e-6/1e-8 relative at R's own parameters for ETS(M,A,M) on AirPassengers, with a new R reference fixture covering 18+6+2+5 models for this plan and its two siblings -- but Tasks 2 (optimizer constraints, all-18+6-model parity) and 3 (AutoETS R-restricted pool, CHANGELOG, push, draft PR) were not started; the branch is unpushed.**

## Performance

- **Duration:** ~2h40min
- **Started:** 2026-10-10 (session start)
- **Completed (Task 1 only):** 2026-10-10
- **Tasks:** 1/3 completed
- **Files modified:** 4 (1 modified, 3 created) in the upstream crate repo; 1 created in this planning repo (this SUMMARY)

## Accomplishments

- Set up the shared upstream branch `fix/ets-likelihood-intervals` from `BASE_REF=1ae5cfc` per the phase's upstream protocol, with the baseline clippy/fmt patch applied as its own first commit (`4f98c1f`)
- Root-caused and fixed the ETS multiplicative-error likelihood bug (`ln|y|` instead of `ln|ŷ|`, and seasonal models skipping the first `period` observations) across both the optimizer's objective function and `fit()`'s stored `log_likelihood`/`aic`/`aicc`/`bic`
- Discovered and fixed a SECOND, deeper bug while chasing the first: the seasonal-multiplicative recursion (used by ANM/MNM/AAM/MAM/AAdM/MAdM) used a classical "deseasonalized" Holt-Winters update that does not algebraically reduce to R's actual (Hyndman innovations state-space) recursion for either error type -- validated and replaced with the correct form, confirmed to full float precision against R's own AirPassengers MAM fit
- Added `ETS::with_params_and_states(...)`, a new constructor that scores a model at caller-supplied parameters and initial states with zero optimisation, used by the new R-parity test and intended for 11-07/11-08 too
- Generated a comprehensive R reference fixture (`validation/reference/r/ets.R` -> `tests/data/r_reference/ets_r.json`): 18 AirPassengers (E x T x S) fits, 6 non-seasonal fits on a strictly-positive series, 2 AutoETS winners, and the 5-model `ses`/`holt`/`hw` family -- the latter two blocks are for plans 11-07/11-08, not consumed by this plan's own test yet
- Full regression safety confirmed: `cargo test --all-features` passes with 0 failures across all 38 test binaries (matching the 11-01 pristine baseline exactly), `cargo fmt --all -- --check` and `cargo clippy --all-features --all-targets -- -D warnings` both clean

## Task Commits

1. **Branch setup (P3/P4 of the upstream protocol, not a plan task)** - `4f98c1f` (style) -- baseline clippy/fmt fix, applied per 11-01's `11-baseline-lint.patch`
2. **Task 1: Tracer — branch, R ets fixture, Σ ln|ŷ| over all observations with R's likelihood convention; ETS(M,A,M) likelihood at R's parameters equals R exactly** - `2561223` (feat)

Tasks 2 and 3 were **not executed** (see Deviations/Halt below).

**Plan metadata:** this commit (docs: halt plan at Task 1, planning-repo only)

## Files Created/Modified (upstream crate repo: `/home/simonm/projects/rust/anofox-forecast`)

- `src/models/exponential/ets.rs` (modified) -- likelihood macro fix, `start_idx=0`, R-convention loglik/aic/aicc/bic in both `calculate_likelihood_with_init_buf` and `fit()`, corrected multiplicative-seasonal recursion (3 arms x 2 error types, both the likelihood-scoring copy and the `fit()` copy), `with_params_and_states`, perfect-fit floor guard
- `validation/reference/r/ets.R` (new) -- self-contained R generator (forecast 9.0.2), regenerate with `Rscript validation/reference/r/ets.R`
- `tests/data/r_reference/ets_r.json` (new) -- the generated fixture (provenance block included; regeneration is byte-identical given the fixed seed)
- `tests/ets_r_reference.rs` (new) -- fixture loader, R-state-to-crate-buffer mapping helper (`r_state_to_crate`), `ets_loglik_at_r_params_mam` (passing)

No files in this planning repo were modified except this SUMMARY and (pending) STATE.md.

## Decisions Made

See `key-decisions` in frontmatter above for the full reasoning. In summary:
1. Scoped the recursion fix narrowly to the arms Task 1's own test needs plus their direct siblings (all seasonal-multiplicative combinations, both error types), leaving seasonal-additive and non-seasonal arms' recursion untouched for Task 2 to validate/fix if needed.
2. Validated the corrected (M,A,M) recursion numerically against the R fixture via a standalone Python script BEFORE writing the Rust change, which caught an indexing error in my own first manual derivation.
3. Also corrected a latent additive-error bug in the same three arms (seasonal update used the just-reassigned level instead of the pre-update one), since it was visible in the same code and cost no extra scope to fix alongside the multiplicative-error fix.
4. Floored the new `ln(sum_sq_errors)` objective instead of rejecting a perfect fit outright, after `cargo test --all-features` caught a regression this introduced in `mstl_forecaster`'s constant-series test.
5. Halted after Task 1 rather than continuing into Tasks 2/3 under a severely depleted context budget, to avoid either rushing unverified optimizer/AutoETS changes or leaving the branch in a broken/uncommitted state.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Seasonal multiplicative-error recursion did not match R's actual update equations**
- **Found during:** Task 1, step 4 (writing `ets_loglik_at_r_params_mam`) -- the test initially failed with `rel_diff=0.0103` on `log_likelihood`, not the expected near-zero
- **Issue:** The existing `(TrendType::{None,Additive,AdditiveDamped}, SeasonalType::Multiplicative)` update blocks (in both `calculate_likelihood_with_init_buf` and `fit()`) used a classical "deseasonalized" Holt-Winters update (`y_des = y/s`, seasonal update using the just-computed new level) that is NOT the Hyndman innovations state-space recursion R's `forecast::ets()` actually computes -- it happened to produce the exact right LEVEL update by algebraic coincidence, but the wrong TREND and SEASONAL updates, with error compounding across observations.
- **Fix:** Replaced with `l_t=(l+phi*b)(1+alpha*e)`, `b_t=phi*b+beta*(l+phi*b)*e`, `s_t=s_{t-m}(1+gamma*e)` (e=(y-fc)/fc) for `is_mult_error=true`; corrected the pre-existing additive-error path's seasonal update to use the pre-update `level`/`level+trend`, not the post-update one. Validated to full float precision (6+ decimal places, effectively machine epsilon on all 6 values checked) against R's AirPassengers MAM fit via a standalone Python re-implementation before committing the Rust change.
- **Files modified:** `src/models/exponential/ets.rs` (both the likelihood-scoring copy and the `fit()` copy of 3 match arms)
- **Verification:** `ets_loglik_at_r_params_mam` passes (loglik/aic/aicc/bic within 1e-6 relative, fitted/final-states within 1e-8 relative of R)
- **Commit:** `2561223`

**2. [Rule 1 - Bug] `ln(sum_sq_errors)` objective rejected a perfect fit as degenerate**
- **Found during:** Task 1, final `cargo test --all-features` run (per P8 DoD) before committing
- **Issue:** An early draft guarded `sum_sq_errors <= 0.0 => return f64::MAX` to avoid `ln(0) = -inf`; this made EVERY ETS candidate on a perfectly-constant series (`sum_sq_errors == 0` exactly) look invalid, breaking `models::mstl_forecaster::tests::constant_series_produces_constant_forecast` ("No valid ETS model could be fitted").
- **Fix:** Floor `sum_sq_errors` to `1e-300` before taking its log, in both `calculate_likelihood_with_init_buf` and `fit()`, instead of rejecting -- a perfect fit is the global optimum of a likelihood objective, not a degenerate candidate.
- **Files modified:** `src/models/exponential/ets.rs`
- **Verification:** `cargo test --all-features` -> 0 failures across all 38 test binaries (re-ran in full after the fix)
- **Commit:** `2561223`

---

**Total deviations:** 2 auto-fixed (2 bugs, both Rule 1).
**Impact on plan:** Both were necessary to make Task 1's own verify block pass as written and to avoid a regression; no scope creep beyond what Task 1's action step 2 ("fix the likelihood") already called for.

## Halt Rationale (Tasks 2 and 3 not started)

This plan's three tasks are sequential and each depends on the previous one's commit landing cleanly (Task 2's tests assume Task 1's likelihood/start_idx/recursion fixes are in place; Task 3's CHANGELOG/push/PR assumes Task 2's optimizer-constraint work is done). Task 1 required substantially more investigation than its action steps implied -- the plan's own action step 2 only mentioned the likelihood/`start_idx` fix, but making `ets_loglik_at_r_params_mam` pass (an explicit, unambiguous acceptance criterion) required discovering and fixing the seasonal recursion bug described above, which was not anticipated in the plan text. By the time Task 1's verify block passed cleanly (`cargo test --test ets_r_reference ets_loglik_at_r_params_mam` exits 0; `cargo test --lib models::exponential::ets` exits 0; full `cargo test --all-features` + fmt + clippy all clean) and was committed, the context budget available for this execution session was exhausted.

Rather than:
- (a) attempt Tasks 2/3 under a severely depleted budget and risk an unverified, partially-committed, or silently-wrong optimizer/AutoETS change landing on a shared branch, or
- (b) fabricate completion of Tasks 2/3 in this SUMMARY,

this plan is reported **halted** at a clean, fully-verified commit boundary: Task 1's commit (`2561223`) is real, its tests pass, the full suite is green, and the upstream branch is in a known-good state for a continuation session to resume from.

**What a continuation session needs to do:**
1. Confirm `git -C /home/simonm/projects/rust/anofox-forecast branch --show-current` == `fix/ets-likelihood-intervals`, HEAD == `2561223` (or later).
2. Execute Task 2 (`map_usual_params` helper used in every `optimize_params` branch; `ets_loglik_at_r_params_all_models`, `ets_usual_constraints_hold`, `ets_optimised_fit_quality`, `ets_mam_parameters_near_r` in `tests/ets_r_reference.rs`) -- NOTE: given what Task 1 uncovered, re-verify the seasonal-additive (`SeasonalType::Additive`, both error types) and non-seasonal (`SeasonalType::None`, mult-error trend) recursion arms against the fixture's 18+6 models too, since Task 1 only validated/fixed the seasonal-multiplicative arms -- `ets_loglik_at_r_params_all_models`'s exact-likelihood assertion will surface any remaining recursion mismatches in those arms directly.
3. Execute Task 3 (AutoETS R-restricted pool, `ETSSpec::is_r_restricted`, MAA/MAdA validity, CHANGELOG, full P8 DoD, push, draft PR).
4. Update this SUMMARY's frontmatter `status: halted` -> `status: complete`, fill in the remaining `coverage` entries' `status`/`human_judgment`, and complete the `## Task Commits` / `## Files Created/Modified` sections for Tasks 2-3.

## User Setup Required

None. No external service configuration needed to resume.

## Next Phase Readiness

- **Not ready to proceed to 11-07/11-08** in the sense of "branch is PR-ready" -- it is not (no push, no PR). However, the R fixture (`tests/data/r_reference/ets_r.json`) both plans will consume already exists and is complete (the `es_family` block for 11-08 and the `air`/`pos` AutoETS blocks for 11-07's interval work are already in the fixture, generated once in this session).
- **Blocker for 11-07/11-08:** both are sequenced after this plan's Task 3 (CHANGELOG + push + draft PR) per the plan's own `depends_on`/wave structure implied by "Plans 11-07 (ETS intervals) and 11-08 ... complete the branch" -- they should not start until this plan's Tasks 2-3 land, since 11-07/11-08 build on the optimizer-constraint and AutoETS-pool changes Task 2/3 will make.
- **Recommendation for the orchestrator:** re-dispatch this plan (11-06) for a continuation session focused solely on Tasks 2 and 3, with the context above as a starting point -- do not re-run Task 1 (it is committed and verified).

---
*Phase: 11-upstream-anofox-forecast-correctness-fixes*
*Completed: 2026-10-10 (Task 1 of 3 only; plan halted)*

## Self-Check: PASSED

- FOUND: /home/simonm/projects/rust/anofox-forecast/src/models/exponential/ets.rs
- FOUND: /home/simonm/projects/rust/anofox-forecast/validation/reference/r/ets.R
- FOUND: /home/simonm/projects/rust/anofox-forecast/tests/data/r_reference/ets_r.json
- FOUND: /home/simonm/projects/rust/anofox-forecast/tests/ets_r_reference.rs
- FOUND: commit 4f98c1f in upstream repo (git log --oneline fix/ets-likelihood-intervals)
- FOUND: commit 2561223 in upstream repo (git log --oneline fix/ets-likelihood-intervals)
