---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 02
subsystem: infra
tags: [rust, statistics, adf, kpss, mackinnon, statsmodels, urca, tseries, upstream-crate, github-pr]

requires:
  - phase: 11-01
    provides: "11-upstream-base.env (BASE_REF=1ae5cfc13ef98afdbb17e27def95a632cbb74fe7, PR_BASE=main, pristine baseline with 0 pre-existing test failures)"
provides:
  - "Upstream branch fix/adf-kpss-correctness, pushed, with PR #221 open against sipemu/anofox-forecast main"
  - "adf_test_with_options / AdfOptions / AdfRegression / AdfLagSelection / mackinnon_p_value / mackinnon_critical_values / public kpss_p_value — new public API surface in anofox-forecast's validation module"
  - "Checked-in R + Python fixture generators (validation/reference/{r,python}/) and fixtures (tests/data/r_reference/) proving the crate's ADF/KPSS implementation against urca/statsmodels/tseries"
affects: [17-upstream-crate-consumption, diagnostics-sql-parity]

actuals:
  tokens: 138205
  tasks: 3
  commits: 4
  plan_head_before: fbe1baf5e0c16fc2d3563848bc45d04b426c96cc

tech-stack:
  added: []
  patterns:
    - "Checked-in two-language fixture generators (R via urca/tseries, Python via statsmodels) sharing one JSON fixture file per concern, each with a provenance block and a verified byte-identical regeneration command"
    - "Cholesky-on-normal-equations OLS core (no new dependency) reused for a from-scratch augmented regression, matching an existing in-crate pattern (src/models/arima/model.rs::solve_symmetric_positive)"
    - "Column-order-sensitive regression replication: trend-first column order during statsmodels' autolag selection phase, level-first for the final refit — required to make column 0 always be the coefficient under test regardless of regression type"

key-files:
  created:
    - /home/simonm/projects/rust/anofox-forecast/validation/reference/r/adf_kpss.R
    - /home/simonm/projects/rust/anofox-forecast/validation/reference/python/adf_statsmodels.py
    - /home/simonm/projects/rust/anofox-forecast/tests/data/r_reference/adf_kpss_r.json
    - /home/simonm/projects/rust/anofox-forecast/tests/data/r_reference/adf_statsmodels.json
    - /home/simonm/projects/rust/anofox-forecast/tests/adf_kpss_reference.rs
  modified:
    - /home/simonm/projects/rust/anofox-forecast/src/validation/stationarity.rs
    - /home/simonm/projects/rust/anofox-forecast/src/validation/mod.rs
    - /home/simonm/projects/rust/anofox-forecast/CHANGELOG.md
    - /home/simonm/projects/rust/anofox-forecast/THIRD_PARTY_NOTICES.md

key-decisions:
  - "Implemented AIC/BIC/TStat lag selection in the Task 1 tracer commit instead of deferring to Task 2 as the plan's task split anticipated — the regression core and the selection loop were inseparable to build correctly in isolation; Task 2 then only needed to replace the MacKinnon p-value/critical-value tables (which were already numerically correct placeholders, transcribed from statsmodels' adfvalues.py during research) and add the fixture-proof tests"
  - "Exposed kpss_p_value as pub (was private) for API parity with mackinnon_p_value and to let tests/adf_kpss_reference.rs::kpss_p_value_bounded_monotone assert the exact interpolation/clamp behavior directly, rather than only indirectly through kpss_test on synthetic series"
  - "mackinnon_tables_match_statsmodels tests only the three cutoff scalars (tau_max/tau_min/tau_star) per regression via the public mackinnon_p_value API, since the coefficient arrays themselves are private consts not reachable from an integration test; the dense mackinnon_p_value_grid/mackinnon_critical_values_grid tests (-7.0..3.0 step 0.1, nobs in {20,50,100,250,500,10000}) are what actually proves every coefficient numerically to 1e-10"
  - "Base-ref: branched from 1ae5cfc13ef98afdbb17e27def95a632cbb74fe7 per 11-01's D-01a decision; PR targets main per the shared protocol's PR_BASE"

patterns-established:
  - "adf_test_with_options(series, &AdfOptions) as the options-taking entry point, with adf_test as a thin backward-compatible delegation — a pattern future Phase 11 model-area PRs (AutoARIMA, ETS, Theta) can mirror if they need an options struct without breaking their own simple public functions"

requirements-completed: [DIAG-01, DIAG-02]

coverage:
  - id: D1
    description: "ADF regression is genuinely augmented (lagged differences enter the regression, not just trim the sample); statistic matches urca::ur.df and statsmodels.adfuller to 1e-8 for none/constant/constant+trend regressions at fixed lags 0/1/4 and for AIC/BIC/t-stat autolag selection"
    requirement: DIAG-01
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#adf_fixed_lag_matches_urca"
        status: pass
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#adf_fixed_lag_matches_statsmodels"
        status: pass
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#adf_autolag_matches_statsmodels"
        status: pass
  - id: D2
    description: "ADF p-values and critical values come from MacKinnon (1994, 2010) response surfaces (ports of statsmodels.tsa.adfvalues.mackinnonp/mackinnoncrit, N=1), not the old 9-step table and hard-coded constants; coefficients proven against statsmodels to 1e-10/1e-12 over a dense grid"
    requirement: DIAG-01
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#mackinnon_p_value_grid"
        status: pass
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#mackinnon_critical_values_grid"
        status: pass
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#mackinnon_tables_match_statsmodels"
        status: pass
  - id: D3
    description: "Unit-root size is nominal and IMA(1,1) rejection drops from the audited 63.5% to the reference-implementation level, measured on a 2000-rep Monte-Carlo"
    requirement: DIAG-01
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#adf_size_monte_carlo (ignored; run: cargo test --test adf_kpss_reference -- --ignored adf_size_monte_carlo --nocapture) — printed: random-walk size @5% = 0.0695 (139/2000), IMA(1,1) theta=-0.5 rejection @5% = 0.0870 (174/2000)"
        status: pass
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#adf_size_reduced (300-rep seeded, runs in normal cargo test)"
        status: pass
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#adf_mc_fixture_matches_statsmodels (40 RW + 40 IMA fixture series; crate matches statsmodels exactly at 1/40 + 4/40 rejections)"
        status: pass
  - id: D4
    description: "KPSS p-value bounded to [0.01, 0.10] exactly like tseries::kpss.test (linear interpolation within the 4-point table, clamped flat outside it; the 0.574/2.5% point is no longer skipped)"
    requirement: DIAG-02
    human_judgment: false
    verification:
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#kpss_matches_tseries"
        status: pass
      - kind: integration
        ref: "tests/adf_kpss_reference.rs#kpss_p_value_bounded_monotone"
        status: pass
  - id: D5
    description: "Branch fix/adf-kpss-correctness pushed; PR #221 open against sipemu/anofox-forecast main; full local DoD green (cargo fmt --check, clippy -D warnings, cargo test --all-features with 0 failures across every binary, cargo doc -D warnings); CHANGELOG has a [0.16.0] - Unreleased entry with before/after numbers; THIRD_PARTY_NOTICES.md records the MacKinnon/statsmodels provenance"
    requirement: null
    human_judgment: false
    verification:
      - kind: other
        ref: "git ls-remote --heads origin fix/adf-kpss-correctness (one line); gh pr list --repo sipemu/anofox-forecast --head fix/adf-kpss-correctness --state open -> PR #221 https://github.com/sipemu/anofox-forecast/pull/221"
        status: pass
      - kind: other
        ref: "cargo fmt --all -- --check; cargo clippy --all-features --all-targets -- -D warnings; cargo test --all-features (every `test result:` line shows 0 failed); RUSTDOCFLAGS=\"-D warnings\" cargo doc --no-deps --all-features"
        status: pass

duration: ~55min
completed: 2026-10-09
status: complete
---

# Phase 11 Plan 02: Upstream ADF/KPSS Correctness Fix Summary

**Replaced `anofox-forecast`'s non-augmented ADF regression and 9-step p-value table with a genuinely augmented regression plus MacKinnon (1994, 2010) response surfaces, and bounded the KPSS p-value to `[0.01, 0.10]` like `tseries` — IMA(1,1) unit-root rejection at 5% drops from 63.5% to 8.70% on a 2000-rep Monte-Carlo, delivered as upstream PR #221.**

## Performance

- **Duration:** ~55 min
- **Started:** ~2026-10-09T21:08Z (session start, per the init context)
- **Completed:** 2026-10-09T21:05Z (last upstream commit 45344ce at 2026-10-09T23:02:26+02:00, PR opened moments later)
- **Tasks:** 3
- **Files modified:** 9 (upstream crate repo: 5 created, 4 modified; this repo: 1 created — this SUMMARY)

## Accomplishments

- Built a from-scratch augmented ADF regression core in `anofox-forecast` (Cholesky-on-normal-equations OLS, no new dependency) that genuinely includes lagged differences `Δy_{t-1}..Δy_{t-lag}` in the regression — the old `compute_adf_statistic` regressed `Δy` on `y_{t-1}` alone and only used the "selected" lag to trim the sample.
- Added `AdfRegression` (NoConstant/Constant/ConstantTrend), `AdfLagSelection` (Aic/Bic/TStat/Fixed), `AdfOptions`, and `adf_test_with_options`, replicating `statsmodels.tsa.stattools.adfuller`'s exact column-ordering and sample-size conventions (trend-first during autolag selection on a common sample, level-first on the final refit's own sample so column 0 is always the `y_{t-1}` coefficient regardless of regression type).
- Ported `statsmodels.tsa.adfvalues.mackinnonp`/`mackinnoncrit` (N=1) as `mackinnon_p_value`/`mackinnon_critical_values`, with every coefficient dumped programmatically from the installed `statsmodels` 0.14.6 into a checked-in fixture (never hand-copied), proven to the Rust constants to `1e-10`/`1e-12` over a dense grid.
- Bounded `kpss_p_value` to `[0.01, 0.10]` via linear interpolation within `tseries::kpss.test`'s own 4-point table (the old implementation extrapolated linearly outside it and skipped the table's 2.5% point entirely); exposed it as a public function for parity with the ADF side.
- Wrote two checked-in fixture generators — `validation/reference/r/adf_kpss.R` (`urca::ur.df`, `tseries::kpss.test`) and `validation/reference/python/adf_statsmodels.py` (`statsmodels.tsa.stattools.adfuller`, `mackinnonp`/`mackinnoncrit`) — both verified byte-identical on regeneration, feeding `tests/adf_kpss_reference.rs`'s 11 passing tests plus a `#[ignore]`d 2000-rep Monte-Carlo.
- Measured the before/after numbers directly: IMA(1,1) θ=−0.5 rejection at 5% **63.5% → 8.70%** (174/2000); random-walk size at 5% **6.95%** (139/2000, within the nominal [3.5%, 7.5%] band); the crate's rejection decisions on the 40+40 fixture series match `statsmodels`' autolag-AIC decisions exactly (1/40 + 4/40 both sides).
- Pushed branch `fix/adf-kpss-correctness` and opened **PR #221** (https://github.com/sipemu/anofox-forecast/pull/221) against `sipemu/anofox-forecast:main`, with full local DoD green.

## Task Commits

All commits are in the **upstream crate repo** (`/home/simonm/projects/rust/anofox-forecast`), per the Phase 11 upstream protocol — this repo (`anofox-forecast` DuckDB extension) only gets this SUMMARY.md commit.

1. **P4 baseline lint (shared protocol step, before Task 1):** `fbe1baf` (style) — applied `11-baseline-lint.patch` from 11-01, no behaviour change
2. **Task 1: Tracer — branch, R fixture generator, augmented regression, fixed-lag parity with urca** — `a5c7a85` (feat)
3. **Task 2: MacKinnon p-values/critical values, AIC/BIC/t-stat lag selection, Monte-Carlo size** — `ed8dec5` (feat)
4. **Task 3: KPSS p-value bounded like tseries, docs, CHANGELOG, DoD, push, PR** — `45344ce` (fix)

**Plan metadata:** (this commit, in this repo — docs: complete plan)

## Files Created/Modified

Upstream crate repo (`~/projects/rust/anofox-forecast`):
- `validation/reference/r/adf_kpss.R` (new) — `urca::ur.df` fixed-lag statistics (none/drift/trend × lags 0/1/4) and `tseries::kpss.test` battery spanning all five p-value regions, plus 40+40 Monte-Carlo series (random walk, IMA(1,1) θ=−0.5)
- `validation/reference/python/adf_statsmodels.py` (new) — `adfuller` fixed-lag/autolag AIC/BIC/t-stat, the same 40+40 Monte-Carlo series recomputed on identical draws, dense `mackinnonp`/`mackinnoncrit` grids, and the N=1 coefficient-table dump
- `tests/data/r_reference/adf_kpss_r.json`, `tests/data/r_reference/adf_statsmodels.json` (new) — fixtures, both regeneration-verified byte-identical
- `tests/adf_kpss_reference.rs` (new) — 11 passing tests + 1 `#[ignore]`d Monte-Carlo
- `src/validation/stationarity.rs` — augmented regression core, `AdfOptions`/`AdfRegression`/`AdfLagSelection`, `adf_test_with_options`, `mackinnon_p_value`, `mackinnon_critical_values`, bounded `kpss_p_value` (now public); deleted `select_lag_aic`/`compute_aic`/`compute_rss`/`compute_adf_statistic`/old `adf_p_value`
- `src/validation/mod.rs` — re-exports for the new public items
- `CHANGELOG.md` — new `[0.16.0] - Unreleased` section (Fixed/Added/Changed) with before/after numbers
- `THIRD_PARTY_NOTICES.md` — new entry for the MacKinnon/statsmodels coefficient-table provenance (BSD-3-Clause)

This repo (`~/projects/duckdb/anofox-forecast`):
- `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-02-SUMMARY.md` (this file)

## Decisions Made

- Branched from `1ae5cfc13ef98afdbb17e27def95a632cbb74fe7` per 11-01's pre-answered D-01a; PR targets `main` per the shared `PR_BASE` contract. No "based on 1ae5cfc / not merged into main" note was needed beyond what's already in the PR body's "Base ref note" section — included verbatim per the protocol.
- Implemented AIC/BIC/TStat autolag selection already in the Task 1 tracer commit (ahead of the plan's intended Task 1 → Task 2 split, which anticipated Task 1 completing only the Fixed-lag path). The regression core and the selection loop shared too much machinery to build correctly in two passes; Task 2's actual remaining work was replacing/documenting the MacKinnon tables and adding the fixture-proof tests, which the commit messages describe accurately.
- Exposed `kpss_p_value` as `pub` (previously private) so `kpss_p_value_bounded_monotone` could assert the interpolation/clamp behavior directly rather than only through `kpss_test` on synthetic series, and for naming/API parity with `mackinnon_p_value`. Documented as `### Added` in CHANGELOG.
- `mackinnon_tables_match_statsmodels` tests only the three cutoff scalars (`tau_max`/`tau_min`/`tau_star`) per regression via the public API, since the coefficient arrays are private `const`s not reachable from an integration test crate; the actual numeric proof of every coefficient is the dense `mackinnon_p_value_grid`/`mackinnon_critical_values_grid` tests (grid of 101 statistics × 3 regressions, and 6 nobs × 3 regressions respectively), both passing to `1e-10`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] R fixture's Monte-Carlo block didn't export raw series values**
- **Found during:** Task 2, writing `validation/reference/python/adf_statsmodels.py` (needed to recompute `adfuller` on the identical 40+40 Monte-Carlo draws `urca::ur.df` had already scored)
- **Issue:** `adf_kpss.R`'s `mc_rw`/`mc_ima` lists only recorded the derived `statistic`/`reject_5pct`, not each series' raw values — the Python side had nothing to recompute on.
- **Fix:** Added `random_walk_series`/`ima_theta_neg_0_5_series` to the R fixture's `monte_carlo` block (the raw 40+40 series, same construction, same RNG position — not touching the series/adf_fixed_lag sections' RNG consumption order).
- **Files modified:** `validation/reference/r/adf_kpss.R`, `tests/data/r_reference/adf_kpss_r.json` (regenerated)
- **Verification:** Re-ran `Rscript validation/reference/r/adf_kpss.R` a second time after the fix; `adf_fixed_lag_matches_urca` still passed unchanged (RNG order for that section untouched); fixture confirmed byte-identical on a third regeneration.
- **Committed in:** `ed8dec5` (Task 2 commit)

**2. [Rule 1 - Bug] R fixture's KPSS block didn't export raw series values for the `ar_phi_*` region-coverage series**
- **Found during:** Task 3, writing `kpss_matches_tseries` (the `ar_phi_*` series exist only inside the `kpss` fixture block, not the top-level `series` block, so there was no way to recompute `kpss_test` on them)
- **Issue:** `kpss_block()` only recorded `statistic`/`p_value`/`lag`, not the series' own values.
- **Fix:** Added `values = as.numeric(y)` to `kpss_block()`'s output.
- **Files modified:** `validation/reference/r/adf_kpss.R`, `tests/data/r_reference/adf_kpss_r.json` (regenerated)
- **Verification:** `kpss_matches_tseries` passes for all 16 KPSS fixture entries (6 main-battery + 10 `ar_phi_*`), covering all five `tseries` p-value regions; ADF sections confirmed unaffected.
- **Committed in:** `45344ce` (Task 3 commit)

**3. [Rule 1 - Bug] Python fixture's JSON serialization would have emitted non-standard `Infinity` tokens**
- **Found during:** Task 2, dumping `tau_max_nc[0]` (which is `inf` for the "n" regression) to JSON
- **Issue:** Python's `json.dump` with default `allow_nan=True` writes the bare (non-standard) token `Infinity`, which `serde_json` rejects as invalid JSON.
- **Fix:** Added a `json_safe()` recursive pass converting non-finite floats to the same `"Inf"`/`"-Inf"`/`"NaN"` string sentinels this repo's R generators already use (per `validation/reference/r/common.R`'s documented convention), and set `allow_nan=False` as a safety net.
- **Files modified:** `validation/reference/python/adf_statsmodels.py`
- **Verification:** `tests/adf_kpss_reference.rs`'s `parse_f64` helper accepts the sentinel; `mackinnon_tables_match_statsmodels` exercises the "n" regression's infinite `tau_max` without a parse error.
- **Committed in:** `ed8dec5` (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (3 bugs — Rule 1, all in fixture-generation plumbing, none in the production statistical logic).
**Impact on plan:** All three were necessary to produce the fixture-backed tests the plan's `must_haves.truths` require. No scope creep — each is a narrow fix to a checked-in generator script, not a change to `anofox-forecast`'s shipped behavior.

## Issues Encountered

- **Concurrent Phase 12 execution in the same planning-repo working tree** (same pattern 11-01 documented). `git status --porcelain` in this repo shows unrelated modified files (`CHANGELOG.md`, `docs/api/*.md`, a skill file, etc.) belonging to a different, concurrently-running GSD agent. Per the orchestrator's instructions, this plan's own commit in this repo uses `git commit --only .planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-02-SUMMARY.md` — never staged or touched any other file.

## User Setup Required

None — no external service configuration required. PR #221 is open for the user to review, request changes on, or merge on their own schedule (per D-02, "Branch + PR, you release" — this plan does not merge, publish, or bump the Cargo.toml version).

## Next Phase Readiness

- PR #221 (`fix/adf-kpss-correctness` → `main`) is open, DoD-green, with before/after evidence. The user should review and merge it (along with PR #220 from 11-01, and any sibling Phase 11 fix PRs landing around the same `[0.16.0]` CHANGELOG section) before cutting a `0.16.0` release.
- Phase 17 (upstream crate consumption, SQL diagnostics parity) depends on a released `anofox-forecast` version that includes this PR's commits — tracked as the standing UPST-06 blocker carried forward from 11-01.
- `adf_test_with_options`/`AdfOptions` establish a pattern (options-struct entry point + thin backward-compatible delegation from the original simple function) that sibling Phase 11 plans (AutoARIMA, ETS, Theta, TBATS/MSTL/GARCH) can reuse if their own fixes need a similar options surface without breaking existing callers.

---
*Phase: 11-upstream-anofox-forecast-correctness-fixes*
*Completed: 2026-10-09*

## Self-Check: PASSED

- FOUND: .planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-02-SUMMARY.md
- FOUND upstream commit (fix/adf-kpss-correctness): fbe1baf (P4 baseline lint)
- FOUND upstream commit (fix/adf-kpss-correctness): a5c7a85 (Task 1)
- FOUND upstream commit (fix/adf-kpss-correctness): ed8dec5 (Task 2)
- FOUND upstream commit (fix/adf-kpss-correctness): 45344ce (Task 3)
- FOUND: PR #221 open at https://github.com/sipemu/anofox-forecast/pull/221
