---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 07
subsystem: infra
tags: [rust, ets, prediction-intervals, exponential-smoothing, upstream-crate, r-reference, monte-carlo]

requires:
  - phase: 11-06
    provides: "Branch fix/ets-likelihood-intervals, draft PR #224, corrected ETS likelihood/recursion, with_params_and_states, R reference fixture tests/data/r_reference/ets_r.json (18 AirPassengers + 6 non-seasonal + 2 AutoETS + 5 ses/holt/hw fits)"
provides:
  - "ets.rs: ETS::ets_cj -- Hyndman/Koehler/Ord/Snyder (2008) table 6.1 closed-form forecast-error-variance coefficient c_j = w'F^(j-1)g for every (trend in {N,A,Ad}, season in {N,A}) combination, confirmed against R's own forecast:::class1 source (via Rscript) including an R-implementation-specific seasonal phase quirk (gamma enters one step earlier when trend is absent)"
  - "ets.rs: ETS::ets_forecast_variance (class-1 analytic v_h) and ETS::ets_class2_variance (class-2 heteroscedastic theta_h recursion, matches R's forecast:::class2 exactly) -- both exact to 1e-6 relative of R at R's own parameters"
  - "ets.rs: ETS::ets_simulated_bounds -- 5000-seeded-path Monte-Carlo simulation for class 3 (MNM/MAM/MAdM) and additive-error+multiplicative-season models (ANM/AAM/AAdM) R also simulates, propagating the model's own one-step update recursion with a fixed-seed StdRng, R type-7 empirical quantiles via ETS::quantile_type7 -- within 5% relative of R's half-widths, identical across repeated calls"
  - "ets.rs: pub(crate) ETS::ets_interval_bounds -- single dispatch entry matching R forecast:::forecast.ets's class1/class2/simulation split exactly on (error, season); deletes the pre-11-07 flat sigma*sqrt(ceil(h/m)) formula entirely; consumed by predict_internal_with_intervals and reachable by 11-08's exponential-smoothing family"
  - "ets.rs: new ETS::interval_sigma2 field -- R's degrees-of-freedom-corrected sigma2 (sum(e^2)/(n-length(par))), computed on the relative-error basis for multiplicative-error models (same basis as loglik/AIC's sum_sq_errors), separate from the existing uncorrected residual_variance (still used by AIC/AICc/BIC/loglik, unchanged)"
  - "src/utils/stats.rs: quantile_normal fixed from the Abramowitz & Stegun 26.2.23 approximation (~4.5e-4 max absolute error) to Peter Acklam's rational approximation (~1.15e-9 relative error) -- required to hit 1e-6-relative R parity; incidentally improves every other interval-producing model in the crate"
  - "tests/ets_intervals_reference.rs: ets_class1_intervals_match_r, ets_class2_intervals_match_r, ets_class3_and_restricted_intervals_near_r, ets_interval_coverage_reduced, ets_interval_coverage (#[ignore]) -- all passing"
  - "CHANGELOG.md: [0.16.0] Fixed entry for the full ETS interval overhaul"
  - "Branch fix/ets-likelihood-intervals pushed to origin (commits c6e19d9, 64286b1); draft PR #224 unchanged (still not ready -- 11-08 lands next on the same branch)"
affects: [11-08, 17-upstream-crate-consumption]

actuals:
  tokens: 30877
  tasks: 2
  commits: 2
  plan_head_before: 4d898263810dcd2046978a626219f0d9ddcf9df9

tech-stack:
  added: []
  patterns:
    - "Run the actual R implementation (Rscript -e 'print(forecast:::class1)' etc.) rather than re-deriving a textbook formula by hand and trusting it -- a hand-derived closed form for ETS(A,N,A)'s seasonal c_j phase matched R for h=1..11 but silently diverged (~1.8e-5 relative, invisible-looking but assertion-breaking at 1e-6 tolerance) at h=12 specifically because R's actual G matrix injects gamma at a fixed row index (G[3,1]) regardless of whether a trend row is present -- which shifts the periodic phase back by one step (j=m-1 instead of j=m) only for the no-trend seasonal-additive arms. Confirmed definitively by running forecast:::class1 directly with controlled par/m and reading its numeric cj output, not by re-reading documentation or re-deriving algebra."
    - "When a closed-form variance recursion is exact to 1e-6 but still fails by a tiny, physically-implausible margin (gamma^2 ~1e-8 can't explain a ~2e-4 gap), treat it as a phase/indexing bug rather than fixture rounding noise -- verify against the model's own mean/sigma2 first (both matched to ~1e-5 relative, ruling out point-forecast or variance-magnitude errors) before concluding the *shape* of the formula (which j the periodic term fires at) is wrong."
    - "A function reused across two call sites with different numeric bases (additive-error raw residuals vs multiplicative-error relative residuals) must source its numeric basis from the SAME already-correctly-computed intermediate (sum_sq_errors, which the loglik/AIC code already gets right per error type) rather than re-deriving from a DIFFERENT field (residual_variance) that happens to share a similar name but a different, error-type-invariant (and therefore wrong for mult-error) basis -- caught immediately by ets_class2_intervals_match_r failing >100% relative on MNN, not by eyeballing the code."

key-files:
  created:
    - /home/simonm/projects/rust/anofox-forecast/tests/ets_intervals_reference.rs
  modified:
    - /home/simonm/projects/rust/anofox-forecast/src/models/exponential/ets.rs
    - /home/simonm/projects/rust/anofox-forecast/src/utils/stats.rs
    - /home/simonm/projects/rust/anofox-forecast/CHANGELOG.md

key-decisions:
  - "Implemented the class-1/2 c_j coefficient as a closed-form function (Hyndman/Koehler/Ord/Snyder 2008 table 6.1), not an explicit F/g/w matrix-power loop -- R's own forecast:::class1 builds explicit matrices, but the closed form is algebraically equivalent, avoids a matrix-algebra dependency, and is O(horizon) instead of O(horizon * state_dim^2). Verified exact to 1e-6 relative against R at R's own parameters for every (trend, season) combination in scope, including the R-implementation-specific seasonal phase quirk."
  - "Did NOT replicate R's exact class3 (MNM/MAM/MAdM) Kronecker-product analytic recursion (found by inspecting forecast:::class3 via Rscript -- it IS analytic in R, not simulation) -- the plan's own must_haves explicitly direct 5000-path simulation with a 5%-relative tolerance for this group (not the 1e-6 used for class 1/2), matching R's own fallback behaviour for the additive-error+multiplicative-season models that R itself simulates via pegelsfcast.C. Implementing the Kronecker-product recursion exactly would have been substantially more complex for a group the plan already scopes to 5% tolerance."
  - "ETS::interval_sigma2 is a new, separate field from the existing ETS::residual_variance (per the plan's explicit instruction) rather than a modification to residual_variance's existing definition, since residual_variance still backs AIC/AICc/BIC/loglik output and changing its basis would be an unplanned behavioural change outside this plan's scope."
  - "Fixed quantile_normal's precision (Abramowitz & Stegun -> Acklam) as a Rule-1 deviation rather than loosening the class-1/2 test tolerance below 1e-6 -- the plan's must_haves explicitly require 1e-6-relative parity, and the coarse approximation's ~7e-6 relative error in 80%/95% z-scores was the actual blocker (confirmed empirically: the exact same formula passed once this was fixed, with zero change to the interval math itself). The old approximation had no test asserting its own precision, so no coverage was lost; the existing quantile_normal_known_values test (epsilon=0.01) still passes with the new, strictly more accurate values."

patterns-established:
  - "For any R-forecast-parity work in this crate going forward: when a hand-derived formula is close-but-not-exact to R at a specific boundary, run the actual R function via Rscript with controlled inputs and read its numeric output directly -- faster and more reliable than re-deriving algebra from a textbook description, and it surfaces R-implementation-specific quirks (like the fixed-row gamma injection) that no textbook derivation would predict."

requirements-completed: [UPST-03]

coverage:
  - id: D1
    description: "Class-1 (additive error, trend N/A/Ad, season N/A) ETS prediction intervals use the Hyndman et al. (2008) analytic variance via the closed-form c_j coefficient, matching R forecast.ets to 1e-6 relative at R's own parameters for all six models (ANN/AAN/AAdN on pos, ANA/AAA/AAdA on air), h=1..24, both 80% and 95% levels"
    requirement: UPST-03
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/ets_intervals_reference.rs#ets_class1_intervals_match_r -- pass"
        status: pass
  - id: D2
    description: "Class-2 (multiplicative error, trend N/A/Ad, season N/A) ETS prediction intervals use R's heteroscedastic theta_h recursion, matching R to 1e-6 relative for all six models (MNN/MAN/MAdN on pos, MNA/MAA/MAdA on air)"
    requirement: UPST-03
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/ets_intervals_reference.rs#ets_class2_intervals_match_r -- pass"
        status: pass
  - id: D3
    description: "Class-3 (MNM/MAM/MAdM) and additive-error+multiplicative-season models (ANM/AAM/AAdM) use 5000-seeded-path Monte-Carlo simulation; 95% half-widths within 5% relative of R for h=1..24; two consecutive predict_with_intervals calls return identical bounds"
    requirement: UPST-03
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/ets_intervals_reference.rs#ets_class3_and_restricted_intervals_near_r -- pass"
        status: pass
  - id: D4
    description: "Monte-Carlo coverage of the resulting 95% intervals is near nominal: reduced test (300 local-level ANN-DGP reps, n=200, h=1..12) mean coverage 0.9500 (target [0.92, 0.975]); full #[ignore] test (2000 reps each of ANN/AAdN/MAM DGPs) mean coverage 0.9457/0.9331/0.9359 (target [0.93, 0.97] each)"
    requirement: UPST-03
    human_judgment: false
    verification:
      - kind: unit
        ref: "tests/ets_intervals_reference.rs#ets_interval_coverage_reduced (non-ignored, always runs) -- pass; #ets_interval_coverage (#[ignore], run manually this session with --nocapture, coverages recorded above) -- pass"
        status: pass
  - id: D5
    description: "Interval machinery is reachable by sibling modules for 11-08 (pub(crate) fn ets_interval_bounds); branch pushed with CHANGELOG entries documenting the before/after half-widths and coverage numbers"
    human_judgment: false
    verification:
      - kind: other
        ref: "grep -c 'pub(crate) fn ets_interval_bounds' src/models/exponential/ets.rs -> 1; git -C /home/simonm/projects/rust/anofox-forecast rev-parse HEAD == origin/fix/ets-likelihood-intervals; CHANGELOG.md [0.16.0] section mentions ETS prediction intervals"
        status: pass
  - id: D6
    description: "Full upstream DoD green before push: cargo fmt --all -- --check, cargo clippy --all-features --all-targets -- -D warnings, cargo test --all-features (0 failures), RUSTDOCFLAGS=\"-D warnings\" cargo doc --no-deps --all-features"
    human_judgment: false
    verification:
      - kind: other
        ref: "all four commands exit 0 (confirmed individually and the full test suite via a background run, exit code 0)"
        status: pass

duration: ~3h
completed: 2026-10-10
status: complete
---

# Phase 11 Plan 07: ETS Prediction Intervals (Part 2/3 of fix/ets-likelihood-intervals) Summary

**ETS prediction intervals now follow R `forecast.ets`'s exact class1/class2/class3 dispatch (Hyndman et al. 2008 analytic variances for additive- and multiplicative-error non-multiplicative-season models, 5000-path Monte-Carlo simulation for every multiplicative-season model), replacing the old flat `sigma*sqrt(ceil(h/m))` width; class-1/2 bounds match R to 1e-6 relative at R's own parameters, class-3 half-widths within 5% relative, Monte-Carlo coverage of the resulting 95% intervals lands at 0.93-0.95 across four different DGPs; branch pushed, draft PR #224 unchanged.**

## Performance

- **Duration:** ~3h
- **Started:** 2026-10-10
- **Completed:** 2026-10-10
- **Tasks:** 2/2 completed
- **Files modified:** 3 modified (ets.rs, stats.rs, CHANGELOG.md), 1 created (tests/ets_intervals_reference.rs) in the upstream crate repo; this SUMMARY in the planning repo

## Accomplishments

- **Task 1 (tracer):** Added `ETS::ets_cj` (Hyndman/Koehler/Ord/Snyder 2008 table 6.1 closed-form forecast-error-variance coefficient) and `ETS::ets_forecast_variance` (class-1 analytic variance), plus the `pub(crate) ETS::ets_interval_bounds` dispatch entry (class 1 wired; class 2/3 placeholder). Added `ETS::interval_sigma2` (R's degrees-of-freedom-corrected sigma2). Discovered and fixed a real precision bug in `quantile_normal` (Abramowitz & Stegun approximation, ~4.5e-4 max absolute error -- insufficient for 1e-6-relative parity) by replacing it with Peter Acklam's rational approximation. Discovered and fixed a genuine formula bug via direct inspection of R's own `forecast:::class1` source: R injects the seasonal gamma term at a fixed matrix row regardless of whether a trend row is present, which shifts the periodic bump back by one step (j=m-1 instead of j=m) specifically for no-trend seasonal-additive models. All six class-1 models (ANN/AAN/AAdN/ANA/AAA/AAdA) now match R to 1e-6 relative at R's own parameters.
- **Task 2:** Added `ETS::ets_class2_variance` (R's `forecast:::class2` heteroscedastic `theta_h` recursion, reusing the class-1 `c_j` coefficients exactly as R's own implementation does) and `ETS::ets_simulated_bounds` (5000-seeded-path Monte-Carlo simulation for class 3 and additive-error+multiplicative-season models, propagating the fitted final state forward via the model's own one-step update recursion with R type-7 empirical quantiles). Completed the `ets_interval_bounds` dispatch to match R's `forecast.ets` class1/class2/simulation split exactly on `(error, season)`, deleting the old flat-width fallback entirely. Found and fixed a Rule-1 bug: `interval_sigma2` (added in Task 1) used the raw-residual basis for all error types, but multiplicative-error models need the relative-error basis (the same one the loglik/AIC already use) -- caught immediately by `ets_class2_intervals_match_r` failing >100% relative on MNN. Added all four `<behavior>` tests plus the CHANGELOG entry, ran the ignored Monte-Carlo coverage test once and recorded the coverages, ran the full P8 DoD, and pushed.

## Task Commits

1. **Task 1: Tracer -- class-1 analytic variance through the generic state-space recursion** -- `c6e19d9` (feat) -- `ets_cj`/`ets_forecast_variance`/`ets_interval_bounds` (class-1 wired), `interval_sigma2` field, `quantile_normal` precision fix, `tests/ets_intervals_reference.rs::ets_class1_intervals_match_r`.
2. **Task 2: Class-2 analytic, class-3 and A-error/M-season simulation, Monte-Carlo coverage, CHANGELOG, push** -- `64286b1` (feat) -- `ets_class2_variance`, `ets_simulated_bounds`, `quantile_type7`, full dispatch, `interval_sigma2` relative-basis fix, all remaining `<behavior>` tests, CHANGELOG, DoD, push.

**Plan metadata:** this commit (docs: complete plan, planning-repo only, `git commit --only`)

## Files Created/Modified (upstream crate repo: `/home/simonm/projects/rust/anofox-forecast`)

- `src/models/exponential/ets.rs` (modified) -- `ETS::ets_cj`, `ETS::ets_forecast_variance`, `ETS::ets_class2_variance`, `ETS::ets_simulated_bounds`, `ETS::quantile_type7`, `pub(crate) ETS::ets_interval_bounds`, new `ETS::interval_sigma2` field, rewired `predict_internal_with_intervals`, updated `predict_with_intervals` rustdoc documenting the per-class method
- `src/utils/stats.rs` (modified) -- `quantile_normal` replaced with Peter Acklam's rational approximation; updated doc comment
- `tests/ets_intervals_reference.rs` (created) -- `ets_class1_intervals_match_r`, `ets_class2_intervals_match_r`, `ets_class3_and_restricted_intervals_near_r`, `ets_interval_coverage_reduced`, `ets_interval_coverage` (#[ignore]); fixture-loading/R-state-conversion helpers duplicated from `ets_r_reference.rs` by design (no shared test module, per plan `<interfaces>`); Monte-Carlo DGP generators (`simulate_ann`, `simulate_aadn`, `simulate_mam`)
- `CHANGELOG.md` (modified) -- `[0.16.0]` Fixed entry for the full ETS interval overhaul (class1/2/3 dispatch, before/after AirPassengers MAM half-widths, coverage numbers, `interval_sigma2`, `quantile_normal` precision)

No files in this planning repo were modified except this SUMMARY.

## Decisions Made

See `key-decisions` in frontmatter above for full reasoning. In summary:
1. Implemented the class-1/2 `c_j` coefficient as a closed-form function per Hyndman's own published table, not R's explicit F/g/w matrices -- verified exact to 1e-6 relative including an R-implementation-specific seasonal phase quirk confirmed by running `forecast:::class1` directly.
2. Deliberately did NOT port R's exact class-3 Kronecker-product analytic recursion (confirmed via `forecast:::class3` inspection that R itself uses an analytic formula there, not simulation) -- the plan's own must_haves explicitly direct simulation with 5% tolerance for this group, matching R's own fallback behaviour for the sibling additive-error+multiplicative-season group.
3. `interval_sigma2` is a new field, not a change to the existing `residual_variance` (which still backs AIC/AICc/BIC/loglik), per the plan's explicit instruction.
4. Fixed `quantile_normal`'s precision as a Rule-1 deviation (the actual blocker for 1e-6-relative parity) rather than loosening the test tolerance.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `quantile_normal` used an imprecise Abramowitz & Stegun approximation**
- **Found during:** Task 1, debugging a 7.4e-6 relative mismatch on `ets_class1_intervals_match_r`'s ANN case
- **Issue:** The existing `quantile_normal` (formula 26.2.23) has ~4.5e-4 max absolute error in `p`, translating to enough `z`-score imprecision to fail the plan's 1e-6-relative R parity requirement -- not specific to this plan's new code, a pre-existing limitation exposed by a stricter test than any prior caller needed.
- **Fix:** Replaced with Peter Acklam's rational approximation (~1.15e-9 relative error, no new dependency).
- **Files modified:** `src/utils/stats.rs`
- **Verification:** `ets_class1_intervals_match_r` passes for ANN; `quantile_normal_known_values`/`quantile_normal_boundary_values` (existing tests) still pass; full `cargo test --all-features` green (no regressions from the precision change)
- **Committed in:** `c6e19d9` (Task 1 commit)

**2. [Rule 1 - Bug] Seasonal `c_j` phase was off by one step for no-trend seasonal-additive models**
- **Found during:** Task 1, `ets_class1_intervals_match_r` failing on ANA specifically at h=12 (not earlier), by a ~1.8e-5 relative margin too small to be explained by the (tiny) gamma value itself
- **Issue:** The initial closed-form assumed the seasonal gamma term re-enters the forecast-error variance periodically at `j ≡ 0 (mod period)` for every (trend, season) combination. Running R's actual `forecast:::class1` directly via `Rscript` (with controlled `par`/`m` and reading the numeric `cj` output) showed the periodic bump actually fires at `j = m-1, 2m-1, ...` when trend is absent, vs `j = m, 2m, ...` when trend is present -- because R's `G` matrix injects gamma at a fixed row (`G[3,1]`) regardless of whether a trend row exists, which is the *second* seasonal state when there is no trend row to occupy position 2.
- **Fix:** `ets_cj`'s seasonal-term condition now branches on whether trend is present, matching R's phase exactly.
- **Files modified:** `src/models/exponential/ets.rs`
- **Verification:** `ets_class1_intervals_match_r` passes for all six class-1 models, h=1..24
- **Committed in:** `c6e19d9` (Task 1 commit)

**3. [Rule 1 - Bug] `interval_sigma2` used the raw-residual basis for all error types**
- **Found during:** Task 2, `ets_class2_intervals_match_r` failing by >100% relative on MNN immediately
- **Issue:** `interval_sigma2` (added in Task 1) was computed from `residual_variance`'s raw `(y-fc)` basis, but R's `sigma2` for multiplicative-error models is the variance of the *relative* error `(y-fc)/fc` -- the same basis the loglik/AIC calculation already correctly uses via the already-accumulated `sum_sq_errors` variable.
- **Fix:** `interval_sigma2` now divides `sum_sq_errors` directly (already correctly relative-vs-absolute per error type) by the degrees-of-freedom, instead of re-deriving from `residual_variance`.
- **Files modified:** `src/models/exponential/ets.rs`
- **Verification:** `ets_class2_intervals_match_r` passes for all six class-2 models, h=1..24
- **Committed in:** `64286b1` (Task 2 commit)

**4. [Rule 3 - Blocking] clippy `excessive_precision` on an Acklam coefficient literal**
- **Found during:** Task 2, P8 DoD clippy pass
- **Issue:** `1.383577518672690e+02` has a trailing zero past `f64`'s representable precision.
- **Fix:** Truncated to `1.38357751867269e+02` (same represented value, clippy-clean).
- **Files modified:** `src/utils/stats.rs`
- **Verification:** `cargo clippy --all-features --all-targets -- -D warnings` exits 0
- **Committed in:** `64286b1` (Task 2 commit)

**5. [Rule 3 - Blocking] clippy `too_many_arguments` on two new test DGP helpers**
- **Found during:** Task 2, P8 DoD clippy pass
- **Issue:** `simulate_aadn` (8 args) and `simulate_mam` (10 args) exceed clippy's default 7-argument threshold.
- **Fix:** `#[allow(clippy::too_many_arguments)]` on both (test-only helpers with no natural struct boundary; not production API).
- **Files modified:** `tests/ets_intervals_reference.rs`
- **Verification:** `cargo clippy --all-features --all-targets -- -D warnings` exits 0
- **Committed in:** `64286b1` (Task 2 commit)

---

**Total deviations:** 5 auto-fixed (3 correctness bugs, 2 blocking clippy fixes).
**Impact on plan:** All five were necessary to make this plan's own acceptance tests pass (the 1e-6-relative R parity and the clean DoD the plan requires). No scope creep: every fix is confined to the interval machinery this plan introduces or the one pre-existing shared utility (`quantile_normal`) its precision requirement exposed.

## Known Stubs

None.

## Threat Flags

None -- no new network/auth/file-access surface; all changes are within the existing ETS prediction-interval code path. The new Monte-Carlo simulation uses a fixed, documented seed (`ETS::ETS_SIMULATION_SEED`) and is deterministic, not a source of nondeterministic behaviour.

## User Setup Required

None. No external service configuration needed.

## Next Phase Readiness

- **Ready for 11-08**: `pub(crate) fn ets_interval_bounds` is the single dispatch entry the exponential-smoothing family (SES/Holt/HW/SeasonalES) plan can reuse or pattern-match against; the shared R fixture (`tests/data/r_reference/ets_r.json`, including the `es_family` block) is unchanged.
- **Branch state:** `fix/ets-likelihood-intervals` pushed to origin (`c6e19d9`, `64286b1`); draft PR [#224](https://github.com/sipemu/anofox-forecast/pull/224) still open against `main`, still not marked ready -- 11-08 lands next on the same branch before it is marked ready for review.
- **No blockers.** This plan's own scope (D-08: ETS prediction intervals, class1/class2 exact parity, class3/restricted simulation within 5%, Monte-Carlo coverage) is complete and fully verified against the R fixture, with the full upstream DoD (fmt, clippy, test --all-features, doc) green.

---
*Phase: 11-upstream-anofox-forecast-correctness-fixes*
*Completed: 2026-10-10*

## Self-Check: PASSED

- FOUND: /home/simonm/projects/rust/anofox-forecast/src/models/exponential/ets.rs
- FOUND: /home/simonm/projects/rust/anofox-forecast/src/utils/stats.rs
- FOUND: /home/simonm/projects/rust/anofox-forecast/tests/ets_intervals_reference.rs
- FOUND: /home/simonm/projects/rust/anofox-forecast/CHANGELOG.md
- FOUND: commit c6e19d9 in upstream repo (git log --oneline fix/ets-likelihood-intervals)
- FOUND: commit 64286b1 in upstream repo
- FOUND: draft PR #224 at https://github.com/sipemu/anofox-forecast/pull/224 (isDraft: true, state: OPEN, matches pre-plan state)
- FOUND: origin/fix/ets-likelihood-intervals == local HEAD (64286b1)
