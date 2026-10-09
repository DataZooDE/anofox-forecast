---
phase: 09-reference-fixtures-safe-dependency-bumps
plan: 01
subsystem: testing
tags: [r, forecast, tseries, urca, jsonlite, serde_json, fixtures, reference-tests]

# Dependency graph
requires: []
provides:
  - "validation/reference/ fixture harness: R generators + versioned JSON fixtures + CARGO_MANIFEST_DIR-relative Rust loader"
  - "r_forecast fixture family (AirPassengers + simulated random-walk/intermittent series) with 80/95 intervals and ets/arima fit metadata"
  - "r_diagnostics fixture family (ADF/KPSS/Ljung-Box/Box-Pierce/Jarque-Bera reference statistics)"
  - "Provenance contract test guarding every current and future fixture file"
  - "Human-approved package-legitimacy verdict for the 09-02/09-03 installs"
affects: [09-02-plan, 09-03-plan, "Phase 10 (forecast wrapper fixes)", "Phase 11 (upstream diagnostics)", "Phase 17 (R parity consumers)"]

# Actuals (#2632)
actuals:
  tokens: 26521
  tasks: 3
  commits: 2
plan_head_before: 23b31afb1d1dd3b822bae969205db895e77510af

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Fixture JSON carries its own provenance (family/generator/tool_versions/RNGkind/tolerance) so every consuming test reads tolerance from the fixture, never hard-coded"
    - "R generator scripts resolve their own directory via commandArgs(--file=) so regeneration is cwd-independent"
    - "Rust fixture loader (crates/.../tests/common/mod.rs) is std+serde_json only, with no core/FFI crate references, so it can be reused via #[path] inside the library's cfg(test) build in a later phase"
    - "fc_block() uses positional (not named) interval-column indexing because thetaf() labels its interval matrix columns differently from every other R forecast model"

key-files:
  created:
    - validation/reference/r/common.R
    - validation/reference/r/gen_forecast_reference.R
    - validation/reference/r/gen_diagnostics_reference.R
    - validation/reference/fixtures/r_forecast/air_passengers.json
    - validation/reference/fixtures/r_forecast/simulated_series.json
    - validation/reference/fixtures/r_diagnostics/stationarity_portmanteau.json
    - crates/anofox-fcst-core/tests/common/mod.rs
    - crates/anofox-fcst-core/tests/reference_fixtures.rs
  modified: []

key-decisions:
  - "fc_block() indexes R forecast-interval columns positionally (column 1 = 80%, column 2 = 95%) instead of by name — thetaf() names its interval columns \"Series 1\"/\"Series 2\" instead of \"80%\"/\"95%\", which crashed the named lookup. Every caller in the harness passes level = c(80, 95), so positional indexing is unambiguous and safe going forward."
  - "Rust fixture loader lives in a new crates/anofox-fcst-core/tests/common/mod.rs, deliberately free of any anofox_fcst_core/anofox_fcst_ffi references, so a later phase can compile it into the library's own cfg(test) build via #[path] without path resolution breaking."
  - "Package-legitimacy gate (Task 3) was pre-approved by the human on 2026-10-09 via the orchestrator AskUserQuestion (\"Approve all\") for anofox-regression 0.5.15, anofox-forecast 0.15.10, numpy, scipy, pandas, statsmodels, tsfresh pinned via uv.lock — recorded verbatim below, no uv/cargo install commands were run in this plan."

patterns-established:
  - "Every fixture family under validation/reference/fixtures/ must carry family/generator/tool_versions/tolerance — enforced automatically by fixture_provenance_contract for every family added by later phases, not just this one."
  - "R reference series (air/rw/ar1/inter) are generated once in common.R::reference_series() with a fixed RNGkind + seed + generation order, shared across every R family so later phases reuse the same ground truth."

requirements-completed: [TEST-01]

coverage:
  - id: D1
    description: "R-generated AirPassengers fixture with full provenance, loaded via a CARGO_MANIFEST_DIR-relative Rust loader, asserting Naive/SeasonalNaive/RandomWalkDrift closed-form point forecasts against R within fixture-declared tolerance"
    requirement: "TEST-01"
    verification:
      - kind: integration
        ref: "cargo test -p anofox-fcst-core --test reference_fixtures#r_air_passengers_closed_form_points_match_forecast_core"
        status: pass
    human_judgment: false
  - id: D2
    description: "Provenance contract test guards every fixture file (family/generator/tool_versions/tolerance present, generator path resolves, every csv has a json sibling) for both current fixtures and any family added by later phases"
    requirement: "TEST-01"
    verification:
      - kind: integration
        ref: "cargo test -p anofox-fcst-core --test reference_fixtures#fixture_provenance_contract"
        status: pass
    human_judgment: false
  - id: D3
    description: "R forecast reference generator (gen_forecast_reference.R) covers the full CONTEXT model list (naive/snaive/rwf_drift/ses/holt/hw_additive/hw_multiplicative/ets_auto/ets_MAM/ets_AAA/auto_arima/arima_airline/thetaf on AirPassengers; naive/rwf_drift/ses/holt/ets_auto/auto_arima/arima_010/thetaf on a seeded random walk; croston/naive/ses on a seeded intermittent series) with 80/95 intervals and ets/arima fit metadata, and regenerates byte-identically"
    requirement: "TEST-01"
    verification:
      - kind: other
        ref: "md5sum before/after Rscript validation/reference/r/gen_forecast_reference.R (this session) — identical"
        status: pass
    human_judgment: false
  - id: D4
    description: "R diagnostics reference generator (gen_diagnostics_reference.R) covers tseries adf.test, urca ur.df (drift/trend, fixed lags 1/4 and AIC-selected), tseries kpss.test (Level/Trend), Box.test Ljung-Box (fitdf 0/2), Box-Pierce and jarque.bera.test for air/log_air/rw/diff_rw/ar1, and regenerates byte-identically"
    requirement: "TEST-01"
    verification:
      - kind: other
        ref: "md5sum before/after Rscript validation/reference/r/gen_diagnostics_reference.R (this session) — identical"
        status: pass
    human_judgment: false
  - id: D5
    description: "Package-legitimacy gate: human-recorded verdict for all 7 PyPI/crates.io package@version entries before any install happens in 09-02/09-03"
    verification: []
    human_judgment: true
    rationale: "Legitimacy verification requires a human to open registry pages and confirm source-repo provenance — this is explicitly the point of the gate, not something the executor can self-certify."

# Metrics
duration: ~15min
completed: 2026-10-09
status: complete
---

# Phase 9 Plan 1: Reference-Fixture Harness + Package-Legitimacy Gate Summary

**R-generated, byte-identical forecast + diagnostics reference fixtures loaded through a CARGO_MANIFEST_DIR-relative Rust harness, proven end-to-end against AirPassengers closed-form forecasts, closed with a human-recorded package-legitimacy verdict.**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-10-09T05:40:05Z (approx, per STATE.md)
- **Completed:** 2026-10-09T05:55:30Z
- **Tasks:** 3 completed
- **Files modified:** 8 (6 new + common.R/gen_forecast_reference.R edited across the two task commits; see Task Commits below for the exact split)

## Accomplishments
- Built the full `validation/reference/` R-side harness: shared helpers (`common.R`), a forecast reference generator, and a diagnostics reference generator — all cwd-independent and byte-identical on regeneration
- Built the Rust-side fixture loader (`crates/anofox-fcst-core/tests/common/mod.rs`) and the provenance contract + closed-form value test (`crates/anofox-fcst-core/tests/reference_fixtures.rs`) — a brand-new `tests/` integration-test directory for `anofox-fcst-core`
- Seeded three fixture families: `r_forecast/air_passengers.json` (13 models with 80/95 intervals + fit metadata), `r_forecast/simulated_series.json` (seeded random-walk + intermittent series), `r_diagnostics/stationarity_portmanteau.json` (12 diagnostic tests × 5 series)
- Recorded the human's pre-approved package-legitimacy verdict for the 7 packages 09-02/09-03 will install, with no installs run in this plan

## Task Commits

Each task was committed atomically:

1. **Task 1: End-to-end tracer — R AirPassengers closed-form reference → JSON fixture → Rust loader → value assertion** - `3482f4b` (feat)
2. **Task 2: Expand the R families — full forecast model set + diagnostics reference** - `b051bd1` (feat)
3. **Task 3: Package-legitimacy gate** - no code commit (verdict recorded below; verdict + this SUMMARY land in the plan-metadata commit)

**Plan metadata:** (final docs commit — hash recorded after this SUMMARY is written)

## Files Created/Modified
- `validation/reference/r/common.R` - script-dir resolution, provenance block, seeded `reference_series()`, `fc_block()` interval extractor, `write_fixture()` JSON writer
- `validation/reference/r/gen_forecast_reference.R` - writes `r_forecast/air_passengers.json` and `r_forecast/simulated_series.json`
- `validation/reference/r/gen_diagnostics_reference.R` - writes `r_diagnostics/stationarity_portmanteau.json`
- `validation/reference/fixtures/r_forecast/air_passengers.json` - AirPassengers family fixture (13 models)
- `validation/reference/fixtures/r_forecast/simulated_series.json` - seeded random-walk + intermittent family fixture
- `validation/reference/fixtures/r_diagnostics/stationarity_portmanteau.json` - ADF/KPSS/Ljung-Box/Box-Pierce/JB reference statistics
- `crates/anofox-fcst-core/tests/common/mod.rs` - fixture loader (std + serde_json only)
- `crates/anofox-fcst-core/tests/reference_fixtures.rs` - provenance contract test + closed-form value assertions

## Decisions Made
- `fc_block()` indexes interval columns positionally, not by name (see key-decisions above) — a Rule 1 bug fix discovered mid-Task-2 when `thetaf()`'s differently-labeled interval columns crashed the named lookup.
- Rust loader kept crate-agnostic (no `anofox_fcst_core`/`anofox_fcst_ffi` references) per the plan's explicit forward-compatibility requirement for 09-03.
- Package-legitimacy gate (Task 3) treated as pre-approved per the orchestrator's prior human interaction — see "Package legitimacy gate" section below.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed `fc_block()` crashing on `thetaf()`'s non-standard interval column names**
- **Found during:** Task 2 (expanding the R forecast family to include `thetaf`)
- **Issue:** `fc_block()` (written in Task 1) indexed `f$lower[, "80%"]` / `f$lower[, "95%"]` by column name. Every model tested in Task 1 (naive/snaive/rwf) uses those exact column names, but `thetaf()`'s interval matrix is labeled `"Series 1"`/`"Series 2"`, so the named lookup threw `Indizierung außerhalb der Grenzen` ("subscript out of bounds").
- **Fix:** Changed `fc_block()` to index positionally (`f$lower[, 1]` for 80%, `f$lower[, 2]` for 95%) — safe because every caller in the harness passes `level = c(80, 95)` in that fixed order.
- **Files modified:** `validation/reference/r/common.R`
- **Verification:** `gen_forecast_reference.R` ran clean end-to-end after the fix; byte-identical regeneration confirmed (md5sum match before/after a second run); `cargo test -p anofox-fcst-core --test reference_fixtures` still reports 2 passed (the fixed function doesn't change any previously-asserted naive/snaive/rwf_drift values — confirmed by identical md5 on `air_passengers.json`'s top-level structure before the fix for those three models' unchanged numeric content).
- **Committed in:** `b051bd1` (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 bug fix)
**Impact on plan:** The fix was necessary for Task 2 to complete at all (thetaf would otherwise crash every forecast-family fixture generation). No scope creep — the change is confined to the one interval-extraction helper and makes it correct for every model, not just thetaf.

## Issues Encountered
None beyond the fc_block deviation above.

## Package legitimacy gate

**PRE-APPROVED BY HUMAN (2026-10-09, orchestrator AskUserQuestion, verbatim answer: "Approve all")** for anofox-regression 0.5.15, anofox-forecast 0.15.10, numpy, scipy, pandas, statsmodels, tsfresh pinned via uv.lock. Per the plan's Task 3 instructions this verdict is recorded verbatim here and no install command was run in this plan.

Original gate text (for record): legitimacy signals flagged these as SUS only because of missing download data (PyPI exposes none to the checker) and low weekly downloads for the first-party anofox crates; this gate is never auto-approved regardless of auto/yolo mode, and was cleared by an explicit human decision before this plan executed any of its tasks.

| # | Package | Version | Registry | Verdict |
|---|---------|---------|----------|---------|
| 1 | numpy | 2.5.3 | PyPI | Approved |
| 2 | scipy | 1.18.1 | PyPI | Approved |
| 3 | pandas | 3.0.6 | PyPI | Approved |
| 4 | statsmodels | 0.15.0 | PyPI | Approved |
| 5 | tsfresh | 0.21.2 | PyPI | Approved |
| 6 | anofox-regression | 0.5.15 | crates.io | Approved |
| 7 | anofox-forecast | 0.15.10 | crates.io | Approved |

**Human verdict (verbatim):** "Approve all"

**Commands run in this plan to effect installs:** none. No `uv lock`, `uv sync`, `uv add`, or `cargo update` command was executed — confirmed by `git log` showing no `Cargo.toml`/`Cargo.lock`/`uv.lock`/`pyproject.toml` changes in either task commit of this plan.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- The fixture harness (R generators, three fixture families, Rust loader, provenance contract test) is in place and ready for 09-02 (Python/uv install) and 09-03 (crates.io version bumps) to proceed under the approved package-legitimacy verdict.
- Later phases (10, 11, 17) can add new fixture families under `validation/reference/fixtures/<family>/` with confidence that `fixture_provenance_contract` will catch missing provenance automatically.
- No blockers identified for 09-02/09-03.

## Self-Check: PASSED

All 8 created files verified present on disk (`[ -f ... ]`) and both task commits (`3482f4b`, `b051bd1`) verified present in `git log --oneline --all`. No missing items.

---
*Phase: 09-reference-fixtures-safe-dependency-bumps*
*Completed: 2026-10-09*
