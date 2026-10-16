---
phase: 15-detection-decomposition-on-fdars-0-47
plan: 03
subsystem: forecasting-core
tags: [bocpd, pelt, changepoint, bayesian, rust, ffi, duckdb-extension, tdd]

requires:
  - phase: 15-detection-decomposition-on-fdars-0-47 (plan 01)
    provides: "fdars-core 0.47 bump, Phase 15 fixture harness + test/sql/ts_detection_reference.test, docs-snippet harness fork"
  - phase: 15-detection-decomposition-on-fdars-0-47 (plan 02)
    provides: "scipy find_peaks port (unrelated module, but shares the same test/sql file and snippet harness)"
provides:
  - "changepoint::{robust_noise_scale, BocpdTrace, bocpd_trace} -- Adams & MacKay (2007) BOCPD: normalised Student-t predictive, sample-mean Normal-Gamma update, truncate-with-renormalise at run length 500, MAP-reset flag rule"
  - "detect_changepoints_bocpd is now a thin wrapper over bocpd_trace (unchanged FFI signature)"
  - "changepoint::detect_changepoints (PELT): sigma-normalised L1 (median)/L2/Normal costs, BIC-style penalty defaults, exact-safe pruning with ChangepointResult.n_cost_evaluations, penalty/input validation"
  - "changepoints fixture family: validation/reference/python/gen_changepoint_reference.py (independent numpy BOCPD + exact optimal-partitioning DP), bocpd_cases.json (10 cases), pelt_cases.json (49 cases incl. 30 random_small)"
  - "crates/anofox-fcst-core/tests/changepoint_reference.rs: bocpd_matches_numpy_reference, bocpd_behaviour_anchors, pelt_matches_numpy_exact_dp, pelt_is_scale_and_shift_invariant"
  - "test/sql/ts_detection_reference.test 15-03 BOCPD section (12 assertions, 53 total)"
  - "measured BOCPD false-alarm rate (20/11/5 flags at hazard 250/500/1000; 94/100 3-sigma detection within 3 samples) documented in CHANGELOG, docs, skill, examples README, validation README"
affects: [15-04, 15-05]

actuals:
  tokens: 104427
  tasks: 3
  commits: 4
  plan_head_before: d9221e2d364003f69ab1f6d240ea009490be01be

tech-stack:
  added: []
  patterns:
    - "Rust TDD RED via scaffolding-only compile fix (new struct field + inert helper functions, no behavioural change) so new tests compile and fail on assertions, not on missing-API compile errors -- same pattern 15-02 used via its tracer task, applied here within a single TDD task since PELT had no preceding tracer substep"
    - "Exact-safe PELT pruning bug class: a `live` candidate set that is not re-filtered for per-t admissibility (t - tau >= min_size) silently corrupts every later f[t] once any candidate is queried before it is old enough -- caught by comparing the pruned DP against a brute-force no-pruning DP sharing the same cost functions and bisecting to the first diverging t, not by eyeballing the pruning inequality"
    - "Independent numpy reference generators (BOCPD, exact-DP) written directly from the cited equations (Adams & MacKay 2007; optimal-partitioning recursion), never by translating the Rust implementation -- confirmed this caught a real Rust bug (the admissibility bug above), since the python reference never had it"

key-files:
  created:
    - validation/reference/python/gen_changepoint_reference.py
    - validation/reference/fixtures/changepoints/bocpd_cases.json
    - validation/reference/fixtures/changepoints/pelt_cases.json
    - crates/anofox-fcst-core/tests/changepoint_reference.rs
    - .planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-03.tsv
  modified:
    - crates/anofox-fcst-core/src/changepoint.rs
    - crates/anofox-fcst-core/src/lib.rs
    - test/sql/ts_detection_reference.test
    - validation/reference/README.md
    - CHANGELOG.md
    - docs/api/06-changepoint-detection.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - examples/changepoint_detection/README.md

key-decisions:
  - "Task 1 (tracer) rewrote only BOCPD, deliberately leaving PELT's cost functions/dead-pruning code byte-for-byte unchanged from pre-plan state, so Task 2's RED phase measured genuine pre-fix PELT behaviour rather than a partially-mixed intermediate state."
  - "gross_step_n24 (the issue-71 [100]*12+[10]*12 fixture case) uses the DEFAULT hazard_lambda=250, not the original Rust unit test's hazard_lambda=10 -- confirmed by sweeping hazard values until the new normalised prob[12] matched the 15-RESEARCH.md anchor 0.87988 exactly at hazard=250."
  - "ChangepointResult.n_cost_evaluations was added to the struct as RED-phase scaffolding (Task 2's own 'test' commit) populated honestly by the still-buggy pre-fix candidate loop, specifically so the pruning-effectiveness test measured the real pre-fix O(n^2) behaviour rather than a stubbed zero -- confirmed by that test genuinely failing on the threshold assertion in the RED commit."
  - "unpruned_exact_dp (the Rust test-only ground-truth DP for the 3000-point pruning test) uses O(1) prefix-sum L2 costs instead of calling the crate's own generic cost_l2, purely to keep the test's own wall-clock time reasonable (O(n^2) instead of O(n^3)) -- this does not change which tau values are scanned (still every admissible one, no pruning), only how fast each one's cost is computed."
  - "A genuine implementation bug (not a plan deviation) was found and fixed during GREEN: the first pruning implementation queried/pruned every tau in the `live` set without checking per-t admissibility (t - tau >= min_size), letting a too-recent candidate corrupt the DP -- on mean3_n150 with CostFunction::Normal this produced 145 spurious changepoints and cost -337.13 instead of the correct 2/[50,100]/172.14. Found by comparing against a brute-force no-pruning DP sharing the same cost functions and bisecting to the first diverging t (t=3, m=2, live=[0,2]: tau=2 was queried despite 3-2=1 < 2). Fixed by skipping inadmissible tau in both the min-evaluation loop and the retain/prune check."

requirements-completed: [DET-02, DET-03]

coverage:
  - id: D1
    description: "BOCPD is Adams & MacKay (2007) done properly: normalised Student-t predictive (full Gamma kernel, log domain), sample-mean Normal-Gamma update, truncation-with-renormalise at run length 500; matches an independent numpy reference to 1e-10 on every per-step probability and exactly on every flag across 10 fixture cases incl. a 1500-point truncation case and a constant series"
    requirement: "DET-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/changepoint_reference.rs#bocpd_matches_numpy_reference, #bocpd_behaviour_anchors"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test (15-03 BOCPD section, 12 assertions)"
        status: pass
    human_judgment: false
  - id: D2
    description: "A changepoint is flagged on a MAP run-length reset (step3_n200 -> [100], previously []); stationary noise stays quiet ([]); the issue-71 step still flags [12] via the new rule; step3 * 100 + 7 flags the same index (scale/shift invariant)"
    requirement: "DET-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/changepoint.rs#test_detect_changepoints_bocpd_step3_flags_100, #test_detect_changepoints_bocpd_step_change_issue_71, #test_bocpd_trace_scale_and_shift_invariant"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test (step3/noise/scaled/issue-71 assertions)"
        status: pass
    human_judgment: false
  - id: D3
    description: "BOCPD's measured false-alarm rate is documented, not tuned away: 100 N(0,1) series (seeds 16000..16099) flag 20/11/5 points in total at hazard 250/500/1000; 3-sigma steps (seeds 17000..17099) are found within 3 samples in 94/100 cases at every one of those hazards -- re-measured against the fixed Rust implementation and recorded in CHANGELOG, docs/api, the detection skill, and the examples README"
    requirement: "DET-02"
    verification:
      - kind: other
        ref: "measured via a throwaway Rust test against bocpd_trace (not committed); numbers recorded in CHANGELOG.md, docs/api/06-changepoint-detection.md, plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md, examples/changepoint_detection/README.md, validation/reference/README.md"
        status: pass
    human_judgment: false
  - id: D4
    description: "PELT normalises every cost by robust_noise_scale: detect_changepoints(x) == detect_changepoints(100*x) == detect_changepoints(x+1e6) for L1/L2/Normal; L1 uses the segment median (2*sqrt(2)*sum|z-median|), not the mean; matches an independent numpy exact-DP reference exactly on changepoints across 49 fixture cases incl. 30 random small series"
    requirement: "DET-03"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/changepoint.rs#test_cost_l1_uses_median_not_mean, #test_detect_changepoints_scale_invariant_for_every_cost"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/changepoint_reference.rs#pelt_matches_numpy_exact_dp, #pelt_is_scale_and_shift_invariant"
        status: pass
    human_judgment: false
  - id: D5
    description: "PELT pruning actually prunes and stays exact: on a 3000-point, 10-segment series it evaluates well under 20% of the n(n+1)/2 segment costs an unpruned exact DP needs, and the pruned result equals that unpruned DP exactly; default penalty is BIC-style (2 ln n for L1/L2, 3 ln n for Normal)"
    requirement: "DET-03"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/changepoint.rs#test_pelt_pruning_effective_and_exact_on_long_series"
        status: pass
    human_judgment: false
  - id: D6
    description: "Edge cases: BOCPD n<3 -> InsufficientData, constant series flags nothing, non-finite input -> InvalidInput; PELT n < 2*min_size -> no changepoints with the whole-series cost (not a hardcoded 0.0), constant series -> no changepoints for every cost, non-finite/negative penalty -> InvalidParameter, non-finite input -> InvalidInput"
    requirement: "DET-02, DET-03"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/changepoint.rs#test_detect_changepoints_bocpd_insufficient_data, #test_detect_changepoints_bocpd_non_finite_input_is_invalid_input, #test_constant_series_zero_changepoints_for_every_cost, #test_detect_changepoints_short_series_cost_is_whole_series_cost, #test_detect_changepoints_rejects_non_finite_or_negative_penalty, #test_detect_changepoints_rejects_non_finite_input"
        status: pass
    human_judgment: false
  - id: D7
    description: "test/sql/ts_detection_reference.test gains a non-gated 15-03 BOCPD section; make test_release reports more assertions than after plan 15-02 (1976 -> 1988); cargo fmt/clippy/test --workspace clean; FFI header byte-identical; TDD RED/GREEN gate commits present for Task 2"
    verification:
      - kind: other
        ref: "make test_release (1988 assertions); cargo fmt --all -- --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace (398+ tests, 0 failed); git diff --quiet -- src/include/anofox_fcst_ffi.h; git log --grep='^test(15-03)' and --grep='^feat(15-03)'"
        status: pass
    human_judgment: false
  - id: D8
    description: "Changepoint docs/skill/example README describe the BOCPD algorithm (standardisation, flag rule, probability definition, measured false-alarm table) and PELT's Rust-only status; CHANGELOG before/after tables; snippet harness 0 FAIL"
    verification:
      - kind: other
        ref: ".planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-03.tsv (0 FAIL rows, exit 0)"
        status: pass
    human_judgment: false

duration: ~75min
completed: 2026-10-13
status: complete
---

# Phase 15 Plan 3: BOCPD Adams & MacKay + PELT normalisation/pruning Summary

**BOCPD is now a correct Adams & MacKay (2007) implementation (normalised Student-t
predictive, sample-mean Normal-Gamma update, truncate-with-renormalise) that detects a
3-sigma step it previously missed entirely, while PELT (Rust/FFI-only) gained
sigma-normalised costs, a median-based L1 cost, and a real exact-safe prune that fixes a
genuine implementation bug found mid-GREEN (an admissibility check missing from the
pruning loop, which had silently corrupted PELT's DP on any `Normal`-cost series).**

## Performance

- **Duration:** ~75 min
- **Started:** 2026-10-13T20:50:00Z (approx, per prior plan's completion timestamp)
- **Completed:** 2026-10-13T22:05:00Z (approx)
- **Tasks:** 3 (Task 1 tracer, Task 2 TDD RED+GREEN, Task 3 docs/CHANGELOG sweep)
- **Files modified:** 13 (across 4 commits)

## Accomplishments

- **Task 1 (tracer, BOCPD):** `changepoint.rs` gained `robust_noise_scale` (MAD-of-
  first-differences robust scale with a documented 3-step fallback chain, shared by
  PELT in Task 2) and `bocpd_trace`/`BocpdTrace` implementing Adams & MacKay (2007)
  exactly per the plan's `<interfaces>` spec: a fully normalised Student-t predictive
  (`ln_gamma` from `statrs`, log-domain throughout), a Normal-Gamma posterior update
  using each run's SAMPLE mean (not the posterior mean) for the sum-of-squares term,
  and run-length truncation at 500 that merges the tail probability mass into the last
  kept entry and renormalises, rather than dropping it. `detect_changepoints_bocpd`
  became a thin wrapper over `bocpd_trace` with an unchanged FFI signature. A new
  independent numpy BOCPD reference (`gen_changepoint_reference.py`) and 10-case
  `bocpd_cases.json` fixture (step3 at 3 hazard values, noise, the issue-71 step at
  the default hazard, a two-step series, a 1500-point truncation case, constant,
  short n=3, and a `*100+7` scale/shift case) pin `sigma_hat`, `loc`, every `prob`
  value, `map_run_length`, `is_changepoint`, and `changepoints` to the fixture exactly
  (1e-10 on probabilities). `_ts_detect_changepoints_bocpd(step3_n200, 250.0, true)`
  on the built extension went from `[]` (missed the 3-sigma step entirely) to `[100]`.
  New `test/sql/ts_detection_reference.test` 15-03 section (12 assertions).
- **Task 2 (TDD, PELT):** RED phase added `ChangepointResult.n_cost_evaluations` and
  inert `effective_min_size`/`default_penalty` helpers as scaffolding (non-behavioural),
  then 7 new unit tests + 2 new integration tests describing sigma-normalised costs,
  median-based L1, penalty/input validation, and pruning effectiveness/exactness --
  5/7 unit tests and both integration tests failed against the unmodified pre-fix PELT,
  with exact got/want evidence recorded in the RED commit (e.g. `cost_l1([1,2,3,100])`
  got `147` mean-based, wanted `282.842712474619` median-based; `x*100` changepoints
  `[50,100]` became `[8,13,...,142]`, 25 spurious). GREEN rewrote `cost_l1`/`cost_l2`/
  `cost_normal` to operate on `z = values / sigma_hat`, added penalty/input validation,
  a real whole-series cost for too-short series, and a genuine PELT pruning rewrite
  (a `live` candidate set retained/queried across iterations). **A real bug was caught
  mid-GREEN**: the first pruning implementation did not check per-`t` admissibility
  (`t - tau >= min_size`) before querying a `live` candidate, letting a too-recent tau
  compete in the DP minimum -- on `mean3_n150` with `CostFunction::Normal` this produced
  145 spurious changepoints and cost `-337.13` instead of the correct `[50,100]`/
  `172.14`. Found by writing a brute-force no-pruning DP sharing the same cost
  functions and bisecting to the first diverging `t` (found at `t=3`, `live=[0,2]`,
  `m=2`: `tau=2` was queried despite `3-2=1 < 2`). Fixed by skipping inadmissible
  `tau` in both the cost-minimisation loop and the retain/prune check. All RED tests
  now pass, including the 3000-point pruning/exactness test (runs in well under a
  second post-fix; the pre-fix version did not finish in several minutes at n=3000,
  independently demonstrating T-15-11's O(n^2)/O(n^3) worst case). Extended
  `gen_changepoint_reference.py` with an independent numpy exact optimal-partitioning
  DP (`opt_partition`) and wrote `pelt_cases.json` (49 cases: 6 `mean3_n150` cost x
  min_size combinations, var2/noiseless-step/noise/constant/outlier cases, an explicit
  penalty override, a scale/shift-invariance pair, and 30 `random_small` cases with
  varying n/cost/min_size).
- **Task 3:** Measured BOCPD's false-alarm rate against the fixed implementation (100
  independent `N(0,1)` series, seeds `16000..16099`: hazard 250/500/1000 -> 20/11/5
  total false alarms; 100 independent 3-sigma-step series, seeds `17000..17099`:
  detected within 3 samples in 94/100 at every hazard) -- these numbers match
  15-RESEARCH.md's prototype anchors exactly when re-measured against the real Rust
  implementation. CHANGELOG gained BOCPD and PELT before/after tables with real
  "before" values captured live from the pre-fix build in Task 1/2 step 1. New
  "Algorithm" section in `docs/api/06-changepoint-detection.md` (standardisation,
  Normal-Gamma prior, hazard definition, truncation, the MAP-reset flag rule and
  where it lands, the probability definition, the measured hazard table, and PELT's
  Rust-only status). Same summary added to the detection SKILL.md and the
  `examples/changepoint_detection/README.md` "BOCPD Algorithm" section.
  `validation/reference/README.md` gained the `changepoints` family row, tolerance
  rationale, and the measured false-alarm notes. Snippet harness on the three
  changed docs files: `pass-list-15-03.tsv`, 0 FAIL. `make test_release`: 1988
  assertions (up from 1976 after 15-02). FFI header byte-identical
  (`header_changed=0`).

## Task Commits

Each task was committed atomically (Task 2's TDD cycle produced two commits):

1. **Task 1: Tracer -- BOCPD Adams & MacKay done properly** -- `7818450` (feat)
2. **Task 2 RED: failing tests for PELT sigma normalisation/median/pruning** -- `f7e1449` (test)
3. **Task 2 GREEN: PELT normalisation, median L1, exact-safe pruning** -- `583f463` (feat)
4. **Task 3: CHANGELOG, docs/skill/example sweep** -- `75937ba` (docs)

_Tracer feedback gate (after Task 1): `HUMAN_VERIFY_MODE=end-of-phase`, Task 1's
`<verify>` was automated-only, auto mode was NOT active (`workflow.auto_advance:
false`, `_auto_chain_active: false`) -- per the tracer rule chain this re-ran Task 1's
`<verify>` end-to-end (all commands passed) and continued directly to Task 2 without a
checkpoint._

**Commits ledger (#3968):** `plan_head_before: d9221e2d364003f69ab1f6d240ea009490be01be`;
`git rev-list --count d9221e2..HEAD` reports **5**, not 4 -- one concurrent sibling-plan
commit (`49d0732` docs(17): revise 17-03 for D-12 root-cause diagnosis) landed between
this plan's Task 1 and Task 2 commits, from another planning agent working
`.planning/phases/17-*` concurrently on the same branch, exactly as flagged as expected
in this executor's `<sequential_execution>` instructions. `actuals.commits: 4` above
reports this plan's own commits only.

**Plan metadata:** (this commit, docs: complete 15-03 plan)

## Files Created/Modified

**Created:**
- `validation/reference/python/gen_changepoint_reference.py` -- independent numpy
  BOCPD (Adams & MacKay 2007) and exact optimal-partitioning DP fixture generator
- `validation/reference/fixtures/changepoints/bocpd_cases.json` -- 10-case fixture
- `validation/reference/fixtures/changepoints/pelt_cases.json` -- 49-case fixture
- `crates/anofox-fcst-core/tests/changepoint_reference.rs` -- Rust parity tests
  against both fixtures
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-03.tsv`
  -- snippet harness output (0 FAIL)

**Modified:**
- `crates/anofox-fcst-core/src/changepoint.rs` -- `robust_noise_scale`, `BocpdTrace`,
  `bocpd_trace`, rewritten `detect_changepoints_bocpd`; sigma-normalised PELT costs,
  median-based L1, BIC-style penalty defaults, exact-safe pruning with
  `ChangepointResult.n_cost_evaluations`, penalty/input validation; 19 new/changed
  unit tests
- `crates/anofox-fcst-core/src/lib.rs` -- re-exports `bocpd_trace`, `robust_noise_scale`,
  `BocpdTrace`
- `test/sql/ts_detection_reference.test` -- 15-03 BOCPD section (12 assertions, 53
  total, up from 41 after 15-02)
- `validation/reference/README.md` -- `changepoints` family row, tolerance rationale,
  measured false-alarm notes, regeneration command
- `CHANGELOG.md` -- BOCPD and PELT before/after tables appended to the Phase 15 bullet
- `docs/api/06-changepoint-detection.md` -- new "Algorithm" section
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` -- changepoint
  section algorithm/hazard-guidance summary, PELT-no-SQL-surface note
- `examples/changepoint_detection/README.md` -- expanded "BOCPD Algorithm" section
  with measured false-alarm table

## Decisions Made

See `key-decisions` in frontmatter.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] PELT pruning silently corrupted the DP for any candidate queried before it was admissible**
- **Found during:** Task 2 GREEN, while verifying the first pruning implementation
  against `pelt_cases.json`'s `mean3_n150_Normal_m1` case
- **Issue:** The pruning loop iterated the full `live` candidate set at every `t`
  without checking `t - tau >= min_size` before using a candidate in the cost
  minimisation or the retain/prune test. On `mean3_n150` with `CostFunction::Normal`
  (`min_size` effectively 2), this let `tau = t - 1` (a too-short, 1-point segment)
  compete in the DP at `t`, producing 145 spurious changepoints and cost `-337.13`
  instead of the correct `[50, 100]`/`172.14` (confirmed identical to the exhaustive
  no-pruning DP once fixed).
- **Fix:** Added an admissibility check (`t - tau < min_size -> skip`) in both the
  cost-minimisation loop and the retain/prune check -- an inadmissible candidate is
  left untouched in `live` (not evaluated, not pruned) until it becomes admissible.
- **Files modified:** `crates/anofox-fcst-core/src/changepoint.rs`
- **Verification:** `pelt_matches_numpy_exact_dp` and
  `test_pelt_pruning_effective_and_exact_on_long_series` pass; a temporary debug
  module (bisecting the first diverging `t` between the pruned algorithm and a
  brute-force reference) confirmed the fix and was deleted before committing.
- **Committed in:** `583f463` (Task 2 GREEN commit)

**2. [Rule 1 - Bug] `test_cost_normal`'s pre-existing assertion was stale after the `+1e-8` cost change**
- **Found during:** Task 2 GREEN, running the existing unit test suite
- **Issue:** The old `cost_normal` special-cased a zero-variance segment to return
  exactly `0.0`; the new formula (`n*(1 + ln(var_mle + 1e-8))`, required for pruning
  to stay exact-safe even on near-zero-variance segments) returns a negative value
  for a constant segment instead. `test_cost_normal`'s old assertion
  (`constant_cost == 0.0`) was therefore stale, not a regression.
- **Fix:** Updated the assertion to the new closed-form expected value
  (`n * (1 + ln(1e-8))`), matching the documented new formula exactly.
- **Files modified:** `crates/anofox-fcst-core/src/changepoint.rs`
- **Verification:** `test_cost_normal` passes.
- **Committed in:** `583f463` (Task 2 GREEN commit)

---

**Total deviations:** 2 auto-fixed (2x Rule 1 - bug).
**Impact:** The first was a genuine algorithmic correctness bug in this plan's own
new PELT pruning code, caught before it ever reached a commit the plan's acceptance
criteria would have passed on faith (the fixture test would have failed, surfacing it
regardless, but the brute-force bisection made the root cause immediate rather than a
long debugging session). The second was a mechanical test-update required by an
intentional formula change already specified in the plan. Neither changed the plan's
intended scope.

## Known Stubs

None.

## Issues Encountered

None beyond the deviations documented above.

## User Setup Required

None -- no external service configuration required.

## Next Phase Readiness

- Plan 15-04 (MSTL + classical decomposition, DET-04) is unaffected by this plan's
  changes -- `decomposition.rs`/`stats.rs`/`stl.rs` were not touched.
- `changepoint.rs`'s `robust_noise_scale` is a stable, reusable building block if a
  later plan needs a robust noise-scale estimate elsewhere, though none currently plan
  to use it outside this module.
- The Phase 15 snippet harness and `test/sql/ts_detection_reference.test` remain in
  place for 15-04/15-05 to append their own sections.
- No blockers identified for 15-04.

## Self-Check: PASSED

- `crates/anofox-fcst-core/tests/changepoint_reference.rs` exists on disk: FOUND
- `validation/reference/fixtures/changepoints/bocpd_cases.json` exists on disk: FOUND
- `validation/reference/fixtures/changepoints/pelt_cases.json` exists on disk: FOUND
- `validation/reference/python/gen_changepoint_reference.py` exists on disk: FOUND
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-03.tsv` exists on disk: FOUND
- `git log --oneline --all | grep 7818450`: FOUND
- `git log --oneline --all | grep f7e1449`: FOUND
- `git log --oneline --all | grep 583f463`: FOUND
- `git log --oneline --all | grep 75937ba`: FOUND
- Re-ran plan-level `<verification>`: `changepoints` fixtures regenerate byte-identically
  (`git diff --exit-code` empty); `bocpd_matches_numpy_reference`/`bocpd_behaviour_anchors`
  and `pelt_matches_numpy_exact_dp`/`pelt_is_scale_and_shift_invariant` all pass (PASS);
  invariance/pruning-effectiveness/edge-case unit tests pass (22/22 lib tests); `cargo
  fmt --all -- --check` / `cargo clippy --all-targets --all-features -- -D warnings` /
  `cargo test --workspace` all green (0 failures across 398+ tests) (PASS);
  `ts_detection_reference.test` 15-03 BOCPD section passes (53 assertions total) and
  `ts_detection_null_policy.test` still passes (24 assertions); `make test_release`
  reports 1988 assertions (up from 1976 after 15-02); docs/skill/examples README state
  the measured false-alarm rate and PELT's Rust-only status; snippet harness 0 FAIL;
  `synthetic_changepoint_examples.sql` exits 0; FFI header unchanged
  (`header_changed=0`) (PASS).

---
*Phase: 15-detection-decomposition-on-fdars-0-47*
*Completed: 2026-10-13*
