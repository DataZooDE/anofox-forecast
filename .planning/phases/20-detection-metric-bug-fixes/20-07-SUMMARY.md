---
phase: 20-detection-metric-bug-fixes
plan: 07
subsystem: detection
tags: [bug-fix, changepoints, bocpd, mstl, rust-ffi, "#287", BUG-05]

requires:
  - phase: 20-detection-metric-bug-fixes plan 06
    provides: "ts_changepoints_params.hpp shared parser, ChangepointOptions/ChangepointOptionsFFI, anofox_ts_detect_changepoints_bocpd_opts, #287 md5 default-output pins"
provides:
  - "deseasonalize_for_changepoints (Rust core): mstl_fit seasonal removal + always-on detrend_linear (D1-b)"
  - "ChangepointOptions.deseasonalize/seasonal_period, ChangepointOptionsFFI.deseasonalize/seasonal_period, ts_changepoints_params.hpp deseasonalize/seasonal_period keys -- shared by both SQL consumers"
  - "issue287_A/B/C R forecast::mstl reference cases (r_decomposition/cases.json) pinning the seasonal-removal half of the transform"
  - "#287 deseasonalize acceptance/parity/error pins in detection_decomposition.test"
  - "docs/api/06-changepoint-detection.md ts_detect_changepoints_agg section + full params table; SKILL.md; runnable example Section 7; CHANGELOG"
  - "Phase 20 whole-phase Definition of Done: full suite, audit, fmt, clippy, inventory gate, check-docs, example run, CI"
affects: []

actuals:
  tokens: 19645
  tasks: 3
  commits: 3
  plan_head_before: 4a9694d58406c70bfeaf905dfeacc0c8e0c1acee

tech-stack:
  added: []
  patterns:
    - "A SQL-reachable transform that reuses two already-shared Rust primitives (mstl_fit, detrend_linear) instead of hand-rolling seasonal/trend removal, per RESEARCH's Don't Hand-Roll rule"
    - "Opt-in behavior change on an options struct: the new deseasonalize/seasonal_period fields default to false/None so every pre-existing call site (FFI struct literals, Rust test literals) needed only `..Default::default()`, not a rewrite"
    - "Params MAP strictness split: deseasonalize/seasonal_period validated strictly in C++ (bool/non-negative-integer parse, throws naming the key on anything else); range/usability semantics (period too short, auto-detection) decided in Rust"

key-files:
  created:
    - .planning/phases/20-detection-metric-bug-fixes/20-07-SUMMARY.md
  modified:
    - crates/anofox-fcst-core/src/changepoint.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-core/tests/changepoint_reference.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - src/include/anofox_fcst_ffi.h
    - src/include/ts_changepoints_params.hpp
    - src/table_functions/ts_changepoints.cpp
    - src/aggregate_functions/ts_changepoints_agg.cpp
    - validation/reference/r/gen_decomposition_reference.R
    - validation/reference/fixtures/r_decomposition/cases.json
    - test/sql/correctness/detection_decomposition.test
    - CHANGELOG.md
    - docs/api/06-changepoint-detection.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - examples/changepoint_detection/synthetic_changepoint_examples.sql
    - scripts/doc_snippets/synopsis.txt
    - .planning/phases/20-detection-metric-bug-fixes/deferred-items.md

key-decisions:
  - "deseasonalize_for_changepoints ALWAYS runs detrend_linear, even when no seasonal period is usable (too-short series, or no detectable period) -- D1-b applies unconditionally, not only when a seasonal component was actually removed"
  - "seasonal_period's FFI/C++ sentinel is 0 = auto-detect (matching the existing hazard_lambda<=0-as-unset convention elsewhere in this FFI boundary); a Rust-level explicit value < 2 (0 or 1) also falls through to auto-detection rather than being treated as a user-supplied period"
  - "deseasonalize/seasonal_period are strict at the C++ parse layer (deseasonalize: true/false/1/0 case-insensitive else throws; seasonal_period: non-negative integer else throws) -- unlike hazard_lambda's pre-existing lenient fallback, a typo here must not silently disable or mis-configure the feature"
  - "Task 2's R-reference pin did not need a RED/GREEN split: the transform was already fully implemented and unit-tested in Task 1, so the fixture-based test is a pin against an independent reference, not new-feature TDD (it passed on first run)"
  - "MainDistributionPipeline not dispatched for this plan's CI step, same as 20-03/20-04/20-05: it has no push trigger and a workflow_dispatch run executes extension-upload.sh against the bucket, which this plan's instructions explicitly prohibit without user go-ahead. Only the 'Static audits' push-triggered workflow was watched to completion (success)."

requirements-completed: [BUG-05]

coverage:
  - id: D1
    description: "deseasonalize_for_changepoints: STL seasonal removal (mstl_fit at the given/auto-detected period) followed by an always-on linear detrend (detrend_linear), per D1-b"
    requirement: BUG-05
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/changepoint.rs#test_deseasonalize_false_reproduces_20_06_bit_for_bit, test_deseasonalize_true_threshold_flags_only_the_step_not_the_seasonal_cycle, test_deseasonalize_explicit_period_matches_auto_detection, test_deseasonalize_too_short_for_any_period_still_detrends_without_error"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/changepoint_reference.rs#deseasonalize_for_changepoints_matches_r_mstl_seasonal_for_issue287"
        status: pass
    human_judgment: false
  - id: D2
    description: "ChangepointOptions/ChangepointOptionsFFI/ts_changepoints_params.hpp gain deseasonalize (strict bool, default false) and seasonal_period (strict non-negative integer, default 0=auto); both ts_detect_changepoints_by and ts_detect_changepoints_agg share the one Rust decision"
    requirement: BUG-05
    verification:
      - kind: integration
        ref: "test/sql/correctness/detection_decomposition.test (#287 deseasonalize block: acceptance, parity, deseasonalize-alone, explicit-period-equals-auto, default-identity, error blocks -- 139 assertions total, up from 113)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The #287 acceptance (MAP{'deseasonalize': 'true', 'threshold': '0.5'} -> A none, B 2024-02-20, C none) holds end to end on both surfaces; default outputs stay byte-identical (20-03 md5 pins unchanged)"
    requirement: BUG-05
    verification:
      - kind: integration
        ref: "test/sql/correctness/detection_decomposition.test; live CLI probe against the tracer verify query -> A,none,none;B,2024-02-20,2024-02-20;C,none,none;"
        status: pass
    human_judgment: false
  - id: D4
    description: "Docs (06-changepoint-detection.md, params table + new ts_detect_changepoints_agg section + runnable example), SKILL.md, and synthetic_changepoint_examples.sql Section 7 document hazard_lambda/threshold/deseasonalize/seasonal_period; every snippet runs"
    requirement: BUG-05
    verification:
      - kind: other
        ref: "make check-docs -> 347 PASS, 0 FAIL, 8 EXPECTED-ERROR, 155 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE"
        status: pass
      - kind: other
        ref: "env -u AI_AGENT ./build/release/duckdb -unsigned -bail < examples/changepoint_detection/synthetic_changepoint_examples.sql -> exit 0"
        status: pass
    human_judgment: false
  - id: D5
    description: "Whole-Phase-20 Definition of Done: full native suite, make audit, cargo fmt --check, cargo test, clippy, correctness inventory, check-docs, example run, CI (push-triggered workflow)"
    verification:
      - kind: other
        ref: "HOME=$(mktemp -d) ./build/release/test/unittest \"$PWD/test/*\" -> All tests passed (6191 assertions in 95 test cases)"
        status: pass
      - kind: other
        ref: "make audit -> 34 hits, 34 allow-listed, 0 violations, 0 stale entries"
        status: pass
      - kind: other
        ref: "cargo fmt --all -- --check -> clean; cargo test -p anofox-fcst-core -> all pass; cargo clippy -p anofox-fcst-core -p anofox-fcst-ffi --all-targets -- -D warnings -> clean"
        status: pass
    human_judgment: true
    rationale: "python3 scripts/check_correctness_inventory.py exits 1 on 3 pre-existing MISSING names unrelated to this plan's diff (documented in deferred-items.md) and the branch's MainDistributionPipeline (wasm-runtime-test, v1.4.5/v1.5.6/v2.0.0 builds) was not dispatched (no push trigger; workflow_dispatch would hit the bucket) -- a human should confirm these pre-existing/deferred gaps are acceptable to close out the phase."

duration: 42min
completed: 2026-10-23
status: complete
---

# Phase 20 Plan 07: Opt-in deseasonalize (D1-b) for changepoints + Phase 20 gate Summary

BUG-05 (#287) closes out Phase 20: `ts_detect_changepoints_by`/`ts_detect_changepoints_agg` gain an opt-in `deseasonalize` key that removes the STL seasonal component (at the given `seasonal_period`, or auto-detected) AND always removes a straight-line trend before BOCPD (D1-b, user decision) -- level shifts survive as steps, regular seasonal cycles stop producing changepoints. The transform (`deseasonalize_for_changepoints`) reuses the two already-shared Rust primitives `mstl_fit` and `detrend_linear` rather than hand-rolling anything new. Defaults are unchanged (the 20-03 md5 pins still pass); the #287 panel combined with `threshold: 0.5` (from 20-06) flags nothing on A/C and exactly B's `2024-02-20` shift, on both surfaces. Docs, skill, a runnable example, and CHANGELOG now describe all four changepoint params keys (`hazard_lambda`, `threshold`, `deseasonalize`, `seasonal_period`), and the whole-phase Definition of Done was run: full native suite (6191 assertions), `make audit`, `cargo fmt --check`, `cargo test`, `cargo clippy -D warnings`, `make check-docs`, and the example script, all green, with three pre-existing, unrelated gate gaps documented rather than fixed (correctness-inventory MISSING names; MainDistributionPipeline not dispatched).

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1 (tracer) | f5f9f29 | feat(20-07): deseasonalize/seasonal_period changepoint transform (D1-b, #287, BUG-05) |
| 2 (tdd) | e6db3a7 | test(20-07): R mstl reference + SQL acceptance/error/parity pins for deseasonalize (#287, BUG-05) |
| 3 (auto) | d081a4b | docs(20-07): document hazard_lambda/threshold/deseasonalize + phase gate (#287, BUG-05) |

`git rev-list --count 4a9694d..d081a4b` = 3. All three carry the `Co-Authored-By: Claude Sonnet 5` trailer.

## What changed

### Task 1 (tracer): the D1-b transform end to end

- **`crates/anofox-fcst-core/src/changepoint.rs`:** new `pub fn deseasonalize_for_changepoints(values, seasonal_period: Option<usize>) -> Result<Vec<f64>>`. Period: the given value when `>= 2`; otherwise the auto-detected primary period from `detect_periods(.., PeriodMethod::Fft, None, None)`, rounded (`< 2`, including "no seasonality" at `primary_period == 0.0`, means no seasonal removal). Seasonal removal reuses `mstl_fit(values, &[p])` -- if `mstl_fit`'s own usability filter (`n > 2*p`) rejects the period, seasonal removal is skipped. `detrend_linear` always runs on whatever is left (deseasonalised, or the raw series). `ChangepointOptions` gained `deseasonalize: bool` (default `false`) and `seasonal_period: Option<usize>` (default `None`); `detect_changepoints_bocpd_with_options` runs the transform first when `deseasonalize` is true, otherwise the 20-06 path is unchanged.
- **FFI:** `ChangepointOptionsFFI` gained `deseasonalize: bool`/`seasonal_period: i64` (`0` = auto, negative -> `InvalidParameter` naming `seasonal_period`); `anofox_ts_detect_changepoints_bocpd_opts` maps them into the Rust options. Header regenerated via `make header` and committed.
- **`ts_changepoints_params.hpp`:** `deseasonalize`/`seasonal_period` fields added to `ChangepointParamSet`; strict parsing (bool `true/false/1/0` case-insensitive, non-negative integer) throws `InvalidInputException` naming the key on anything else. Both `ts_changepoints.cpp` and `ts_changepoints_agg.cpp` now populate `opts.deseasonalize`/`opts.seasonal_period` from the parsed param set.
- Four new Rust unit tests (deseasonalize-false no-op, threshold+deseasonalize isolates the real shift on the #287 panel's series B, explicit `seasonal_period=7` equals auto-detection, too-short-for-any-period still detrends without error) plus fixing the four pre-existing `ChangepointOptions { .. }` literals (3 in `changepoint.rs`, 1 in `changepoint_reference.rs`) that needed `..Default::default()` after the struct grew two fields.

### Task 2 (TDD-shaped, single pass): R reference + SQL pins

- `gen_decomposition_reference.R` gained `issue287_series(base, step_at_50)` (the exact #287 SQL formula) and three new `mstl_cases` entries (`issue287_A/B/C`, period 7); regenerating `cases.json` was purely additive -- every pre-existing case byte-identical in the diff.
- `changepoint_reference.rs` gained `deseasonalize_for_changepoints_matches_r_mstl_seasonal_for_issue287`: for each of the three cases, `values - R's own mstl seasonal (p7)`, then `detrend_linear`, must equal `deseasonalize_for_changepoints(values, Some(7))` within the fixture's `component_abs` tolerance -- an independent reference for the seasonal-removal half of the transform (the numpy BOCPD reference from 20-06 already pins the decision-rule half).
- `detection_decomposition.test` gained a "#287 deseasonalize (BUG-05, D1-b)" block (118 new lines, 26 new assertions, 139 total up from 113): the acceptance string on both surfaces, table/aggregate parity, `deseasonalize` alone (MAP run-length rule) leaving extra weekly-spaced flags on B, explicit `seasonal_period='7'` equalling auto, a default-identity re-check, and `deseasonalize`/`seasonal_period` statement errors naming the key on both surfaces.
- No RED/GREEN split was needed: the transform was already fully implemented in Task 1, so this task adds an independent reference pin, not new feature code (it passed on the first run).

### Task 3 (auto): docs, skill, example, CHANGELOG, phase gate

- `docs/api/06-changepoint-detection.md`: `ts_detect_changepoints_by`'s params table now covers `hazard_lambda`/`threshold`/`deseasonalize`/`seasonal_period` with full semantics; a new `ts_detect_changepoints_agg` section (signature, params, returns, example); a new "Ignoring Seasonal Cycles" section with a runnable `deseasonalize` + `threshold` example and the "deseasonalize alone can still flag weekly-spaced points" caveat.
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md`: the changepoint params table gains the same four keys; a new example; a note that `ts_detect_changepoints_agg` shares the same params MAP.
- `examples/changepoint_detection/synthetic_changepoint_examples.sql`: new Section 7 running the #287 panel with/without `deseasonalize`/`threshold` on both surfaces, with inline comments stating the expected flags per step.
- `CHANGELOG.md` `## [Unreleased]`: a new `### Added` entry for the `threshold`/`deseasonalize`/`seasonal_period` keys, and a new `### Fixed` entry for `ts_detect_changepoints_agg` previously ignoring its entire params MAP (#287, BUG-06, closed out by 20-06 but not yet in CHANGELOG) plus the `ts_detect_periods_by` period-7 regression pin.
- `scripts/doc_snippets/synopsis.txt`: added the new `ts_detect_changepoints_agg(...)` signature key (Rule 3 -- the signature block is narrative, not runnable SQL, and was failing `make check-docs` as a FAIL until keyed as SYNOPSIS).
- Phase gate run and recorded (see Verification below); `.planning/phases/20-detection-metric-bug-fixes/deferred-items.md` gained a "From 20-07" entry for the correctness-inventory gap.

## Verification (observed)

- **Task 1 tracer** (CLI): the plan's exact tracer query on the #287 panel -> `A,none,none;B,2024-02-20,2024-02-20;C,none,none;` -- exact match. `cargo test -p anofox-fcst-core --lib changepoint`: 30 passed, 0 failed. `detection_decomposition.test`: 113 assertions, all pass (md5 default pins unchanged). `cargo fmt --all -- --check` and `make audit` (34 hits, 0 violations): clean.
- **Task 2**: `cargo test -p anofox-fcst-core --test changepoint_reference`: 6 passed, 0 failed. `detection_decomposition.test`: 139 assertions, all pass (up from 113).
- **Task 3 / whole-phase gate**:
  - `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"`: **All tests passed (6191 assertions in 95 test cases)** (up from 6165 at the start of this plan).
  - `make audit`: 34 hits, 34 allow-listed, 0 violations, 0 stale entries.
  - `cargo fmt --all -- --check`: clean.
  - `cargo test -p anofox-fcst-core`: all lib/integration/doctests pass.
  - `cargo clippy -p anofox-fcst-core -p anofox-fcst-ffi --all-targets -- -D warnings`: clean, 0 warnings.
  - `make check-docs`: `347 PASS, 0 FAIL, 8 EXPECTED-ERROR, 155 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE`.
  - `python3 scripts/check_correctness_inventory.py`: exits 1 -- `MISSING _ts_forecast_scalar`, `MISSING ts_features_config_template`, `MISSING ts_features_list`. The last two are the pre-existing Phase 19 BUG-02 entries already logged by 20-03. `_ts_forecast_scalar` is new to the report but confirmed pre-existing (the CHANGELOG backtick mention it keys off was already present, unchanged, at this plan's starting commit `4a9694d`, added by 20-02, and only ever appears inside a `union_source.test` comment, never as an actual correctness-test call). Every function THIS plan added to the CHANGELOG is covered. Logged in `deferred-items.md`.
  - `env -u AI_AGENT ./build/release/duckdb -unsigned -bail < examples/changepoint_detection/synthetic_changepoint_examples.sql`: exit 0, all 7 sections printed cleanly.
  - CI: pushed `4a9694d..d081a4b` to `milestone/v0.11.0-issue-sweep`. The "Static audits" workflow (the only one triggered by a branch push) ran as **40030333430** and completed `success`. **MainDistributionPipeline (wasm-runtime-test, v2.0.0/v1.5.6/v1.4.5 builds) was not dispatched** -- same as 20-03/20-04/20-05: it has no `push` trigger on a branch, and a `workflow_dispatch` run executes `extension-upload.sh` against the bucket (with `latest`/versioned false), which this plan's execution instructions explicitly prohibit without the user's go-ahead. `git rev-parse HEAD` equals `git rev-parse origin/milestone/v0.11.0-issue-sweep` (both `d081a4b`).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed a compile break in `changepoint_reference.rs` caused by this plan's own struct extension**
- **Found during:** Task 1, immediately after extending `ChangepointOptions` with two new fields
- **Issue:** The pre-existing `ChangepointOptions { hazard_lambda, threshold: Some(0.5) }` literal in `changepoint_reference.rs` (from 20-06) stopped compiling once `ChangepointOptions` grew `deseasonalize`/`seasonal_period` (missing-field error, caught by `cargo clippy --all-targets`)
- **Fix:** Added `..Default::default()` to the literal
- **Files modified:** `crates/anofox-fcst-core/tests/changepoint_reference.rs`
- **Verification:** `cargo clippy -p anofox-fcst-core -p anofox-fcst-ffi --all-targets -- -D warnings` clean; `cargo test -p anofox-fcst-core --test changepoint_reference` passes
- **Committed in:** f5f9f29 (Task 1 commit)

**2. [Rule 1 - Bug, test-authoring] The plan's literal "pure sine + step" unit-test fixture was pathological for STL and had to be replaced**
- **Found during:** Task 1, writing the four new Rust unit tests the plan specified
- **Issue:** The plan's Task 1 wording calls for a test series that is "a pure sine (period 7, n=84) plus a +15 step at index 50". Building that exact series (no trend, no secondary harmonics) and running it through `deseasonalize_for_changepoints` + threshold 0.5 flagged index 78, not 50 -- STL's loess-based seasonal fit does not cleanly separate a perfectly periodic, noise-free sinusoid from a step on a series this short, leaking seasonal residual into the "detrended" output. The already-measured, already-verified `issue287_series_b()` helper (same file, from 20-06 -- the exact #287 formula with its small trend + two extra harmonic terms) does NOT exhibit this pathology and reproduces the plan's own measured table (`A none; B 2024-02-20; C none`) exactly.
- **Fix:** Used `issue287_series_b()` instead of inventing a new "pure sine" helper, for both of the two tests that needed a seasonal-with-step series
- **Files modified:** `crates/anofox-fcst-core/src/changepoint.rs`
- **Verification:** Both new tests pass; independently cross-checked against a throwaway debug binary showing the "pure sine" case's divergent (and divergent-with-reality) behavior before the fix
- **Committed in:** f5f9f29 (Task 1 commit)

**3. [Rule 3 - Blocking] `make check-docs` FAIL on the new `ts_detect_changepoints_agg` signature snippet**
- **Found during:** Task 3, running `make check-docs` after adding the new docs section
- **Issue:** The new `ts_detect_changepoints_agg(date_col, value_col, params) -- used with GROUP BY` signature block (narrative, not runnable SQL -- `date_col`/`value_col` are not bound to any table) executed as literal SQL and failed with a binder error
- **Fix:** Added the block's first line as a new key to `scripts/doc_snippets/synopsis.txt`, matching the existing `ts_detect_changepoints_by` signature key's treatment
- **Files modified:** `scripts/doc_snippets/synopsis.txt`
- **Verification:** `make check-docs` -> `347 PASS, 0 FAIL, 8 EXPECTED-ERROR, 155 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE`
- **Committed in:** d081a4b (Task 3 commit)

**4. [Rule 1 - Bug, docs example] The docs/example's first draft of a `deseasonalize` demo series hit the same STL pathology as deviation #2**
- **Found during:** Task 3, verifying the new docs example and example-script Section 7 against the built extension before committing
- **Issue:** A "pure sine + step, no trend" demo series in the first draft of `docs/api/06-changepoint-detection.md`'s new section flagged `2024-03-19`, not the real shift `2024-02-20`, for the same reason as deviation #2
- **Fix:** Replaced it with the #287-style formula (small linear trend + two extra harmonic terms) in both the docs example and `synthetic_changepoint_examples.sql`'s Section 7, verified against the built extension to flag exactly `2024-02-20`
- **Files modified:** `docs/api/06-changepoint-detection.md`, `examples/changepoint_detection/synthetic_changepoint_examples.sql`
- **Verification:** Both snippets re-run against the built extension; `make check-docs` and the full example script exit 0 with the documented output
- **Committed in:** d081a4b (Task 3 commit)

---

**Total deviations:** 4 auto-fixed (2 Rule 1 -- bug/correctness in test and docs authoring, 2 Rule 3 -- blocking compile/check-docs failures)
**Impact on plan:** All four were necessary to keep the plan's own deliverables (tests, docs, example) correct and green; none changed the shipped Rust/C++ implementation itself. No scope creep.

### Out-of-scope discoveries

- `python3 scripts/check_correctness_inventory.py` now reports a third pre-existing MISSING name (`_ts_forecast_scalar`), confirmed to predate this plan (CHANGELOG mention added by 20-02; never an actual correctness-test call). Logged in `deferred-items.md`, not fixed (out of this plan's file scope).

## Known Stubs

None.

## Requirement status

BUG-05 (#287: opt-in `deseasonalize`/`seasonal_period`, D1-b transform, defaults unchanged) is complete per this plan's verification. This closes Requirements table coverage for BUG-03 through BUG-07 except BUG-03 (tracked separately, already closed by earlier Phase 20 plans per REQUIREMENTS.md).

## Self-Check: PASSED

- FOUND: commits f5f9f29, e6db3a7, d081a4b (`git log --oneline`)
- FOUND: `crates/anofox-fcst-core/src/changepoint.rs` contains `pub fn deseasonalize_for_changepoints` (1 match)
- FOUND: `src/include/anofox_fcst_ffi.h` / `src/include/ts_changepoints_params.hpp` both contain `deseasonalize`
- FOUND: `detection_decomposition.test` -- 139 assertions pass; full native suite -- 6191 assertions in 95 test cases pass
- FOUND: `make check-docs` -- 0 FAIL; example script exits 0
- FOUND: pushed commit `d081a4b` == `origin/milestone/v0.11.0-issue-sweep` HEAD
