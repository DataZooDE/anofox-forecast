# Phase 17: Upstream Consumption & Diagnostics Parity - Research

**Researched:** 2026-10-12
**Domain:** Rust crate version consumption (anofox-forecast 0.15.10 → 0.16.0), SQL diagnostics correctness (ADF/KPSS four-way classification, Ljung-Box df correction), R-forecast parity test infrastructure
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Bump `anofox-forecast` 0.15.10 → the released `0.16.0` from crates.io: workspace `Cargo.toml` plus `Cargo.lock`. No interim `[patch]`, because the release exists. The existing `[patch.crates-io]` entry (argmin) is unrelated and stays. At phase close there is no anofox-forecast `[patch]`/git/path override.
- **D-02:** AutoARIMA's order-selection fixes (upstream PR #223, still a draft) are NOT in 0.16.0. Phase 17 proceeds with 0.16.0. In the FCST-09 parity tests AutoARIMA is a documented known gap (own tolerance, or order-mismatch note) with a tracked follow-up. UPST-06 closes for 0.16.0; UPST-01 closes when a later release containing #223 is consumed (a small follow-up bump, tracked in STATE.md).
- **D-03:** Executes after Phases 15 and 16 (Cargo.lock overlap, forecast numbers shift).
- **D-04:** `ts_stationarity_by` / `classify_stationarity` use the standard ADF/KPSS four-way mapping (statsmodels convention):
  - ADF rejects H0 and KPSS does not → `stationary`
  - ADF does not reject and KPSS rejects → `non_stationary`
  - neither rejects → `trend_stationary`
  - both reject → `difference_stationary`

  A constant series gets an explicit `constant` verdict (C4). Breaking label change, documented in the CHANGELOG.
- **D-05:** A `regression := 'c' | 'ct'` parameter (default `'c'`, statsmodels/urca naming) on `ts_adf_by`, `ts_kpss_by` and `ts_stationarity_by`, so trend variants and the trend-stationary verdict are reachable. `ts_adf_by` / `ts_kpss_by` return the corrected 0.16.0 statistics and p-values (augmented ADF with MacKinnon p-values; KPSS p bounded [0.01, 0.10]) matching the Phase 11 fixtures.
- **D-06:** `ts_ljung_box_by` takes a named parameter `fitted_params := 0` (default), with df = lags − fitted_params; df ≤ 0 raises an error. The p-value matches R `Box.test(..., type = "Ljung-Box", fitdf = p + q)` (C5).
- **D-07:** `docs/api/10-diagnostics.md` is rewritten to claim only what fixtures verify (C6). Every snippet runs against the built extension via the snippet harness.
- **D-08:** Reference: a checked-in R script (`forecast` installed) runs naive, snaive, rwf(drift), ses, holt, hw, ets, auto.arima, thetaf and croston on AirPassengers plus 3 seeded simulated series (local level, trend, intermittent). Extend the existing `validation/reference/r/gen_forecast_reference.R` and the `fixtures/r_forecast/` files (air_passengers.json, simulated_series.json from Phase 9) rather than starting over. Fixtures hold point forecasts and 95% bounds.
- **D-09:** Tolerances per model, documented in `validation/reference/README.md`: deterministic baselines (Naive/SNaive/RWD/Croston) to about 1e-8; optimised models (SES/Holt/HW/ETS/Theta) to a documented relative tolerance, since optimisers differ. AutoARIMA per D-02.
- **D-10:** A seeded Monte Carlo coverage test for the local-level case (about 2,000 repetitions): ETS(A,N,N)/SES 95% intervals must cover within ±3pp of nominal.
- **D-11:** 3 plans: (1) bump to 0.16.0 + ADF/KPSS from SQL + four-way stationarity mapping + `regression` parameter [UPST-06, DIAG-03]; (2) Ljung-Box `fitted_params` + diagnostics docs [DIAG-04, DIAG-05]; (3) R forecast parity fixtures + Monte Carlo coverage [FCST-09]. New SQL assertions go in a NON-json-gated reference test file. Breaking changes get CHANGELOG before/after rows; docs and skills are updated.

### Claude's Discretion

- Exact tolerance values per model (each justified), the Monte Carlo seed and size, simulated-series parameters, and where the parity test lives (Rust integration test over the core forecast wrapper and/or SQL).

### Deferred Ideas (OUT OF SCOPE)

- A follow-up bump to the anofox-forecast release containing AutoARIMA #223 (closes UPST-01, tightens the AutoARIMA parity tolerance).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| UPST-06 | Upstream changes land as PR(s) in `~/projects/rust/anofox-forecast` with tests + CHANGELOG; the extension consumes them (interim `[patch]`, final released version after user publishes) | `anofox-forecast` 0.16.0 is **already published on crates.io** (verified directly against the registry, see Standard Stack). Its own CHANGELOG confirms PRs #221 (ADF/KPSS), #222 (Theta), #224 (ETS), #225 (GARCH/TBATS/MSTL) are in 0.16.0; #223 (AutoARIMA) is explicitly excluded, matching D-02. Dependency surface (names + version reqs) is byte-identical to 0.15.10, so the bump should be a clean `Cargo.toml`/`Cargo.lock` edit with zero new transitive crates — Common Pitfalls section documents the concrete compile/test breakage the bump causes regardless. |
| DIAG-03 | `classify_stationarity` uses the standard ADF/KPSS four-way mapping, can produce trend-stationary (trend variants), and handles constant series explicitly | Current extension code at `crates/anofox-fcst-core/src/validation.rs:124-131` is read and quoted verbatim below — it is **inverted** relative to D-04 on 3 of 4 branches. The fix is a pure relabelling in the EXTENSION's wrapper (no upstream crate involvement) plus a new constant-series pre-check using the existing `filter::is_constant`. |
| DIAG-04 | Ljung-Box accepts `fitted_params` (df = lags − p − q) from SQL | Upstream crate's `ljung_box(residuals, lags, fitted_params)` **already accepts `fitted_params`** (verified by reading `validation/residual_tests.rs` in the released 0.16.0 source) — this was never an upstream bug. The bug is entirely in the extension: `crates/anofox-fcst-core/src/validation.rs:178-180` hard-codes `0`, and the FFI function `anofox_ts_ljung_box` (lib.rs:7274) has no `fitted_params` parameter at all. Both must be threaded through, plus the SQL macro `ts_ljung_box_by` and scalar `ts_ljung_box`. |
| DIAG-05 | `docs/api/10-diagnostics.md` claims match verified behaviour | Current doc's "Caveats" section (lines 169-212) explicitly documents the *old*, now-fixed limitations (non-augmented ADF, RSS-not-AIC lag selection, 9-point p-value table) with "(fix planned for v0.10.0, Phase 11/17)" markers — these three caveats should be deleted/rewritten, not merely edited, once DIAG-01/02/03/04 land. |
| FCST-09 | AirPassengers + simulated-series fixtures: point/interval parity with R `forecast`; Monte-Carlo coverage ±3pp | Phase 9's `validation/reference/r/gen_forecast_reference.R` + `fixtures/r_forecast/{air_passengers,simulated_series}.json` already cover 10 of the needed model×series combinations (not all 10 models × all series — model applicability is intentionally scoped per series type). The one genuinely missing piece is a **"trend" simulated series** — `reference_series()` currently returns only `air`, `rw` (local level), `ar1`, `inter` (intermittent); D-08's third series ("trend") does not exist yet. |
</phase_requirements>

## Summary

Phase 17 has three independent-but-ordered concerns: (1) consume a crate version bump that is **already released and sitting on crates.io**, (2) fix two extension-side (not upstream) diagnostics bugs that only need the crate's *already-existing* API surface, and (3) extend an *already-built* R-parity fixture harness with one new series and a Monte Carlo coverage test. None of the three requires new upstream work — Phase 11's five PRs (#220 sync, #221 ADF/KPSS, #222 Theta, #224 ETS, #225 GARCH/TBATS/MSTL) are confirmed merged and released as `anofox-forecast` 0.16.0 (2026-10-10); only #223 (AutoARIMA) is excluded, exactly matching the CONTEXT.md's D-02.

The bump itself is a single `Cargo.toml` version-string edit with an identical dependency surface (verified against the crates.io registry: same 19 dependency entries, same version requirements, between 0.15.10 and 0.16.0) — low technical risk for the `cargo tree`/MSRV/OpenSSL-static concerns the CLAUDE.md flags. The *real* risk is downstream: the bump silently changes forecast numbers everywhere (ETS/Theta/GARCH/TBATS/MSTL likelihoods and intervals all change formulas) and breaks two concrete, already-identified test files outright (`test/sql/ts_forecast_ets_model.test`, `test/sql/ts_native_param_validation.test` — both pin the pre-0.16.0 rejection of `ETS(M,A,A)`/`ETS(M,Ad,A)` as "unstable", which 0.16.0 makes a no-op) plus one Rust test documented as designed to break (`crates/anofox-fcst-core/tests/theta_croston_fitted.rs`) and, almost certainly, the SQL diagnostics test's hard-coded ADF critical-value assertions (`test/sql/ts_diagnostics.test:70-87`), since critical values are now finite-sample-corrected rather than fixed asymptotic constants.

The diagnostics fix (DIAG-03/04) is **smaller than the CONTEXT.md's framing suggests for KPSS**: `classify_stationarity`'s four-way mapping bug is a pure relabelling inside the extension (verified: the current code's `(true,false)`/`(false,false)`/`(false,true)` branches are each mapped to the wrong one of the three non-`stationary` labels), and Ljung-Box's `fitted_params` plumbing is pure wiring (the crate function already takes the parameter; nothing to fix upstream). But the `regression := 'c'|'ct'` parameter in D-05 has a real gap: the crate's `AdfOptions.regression` genuinely supports `'c'`/`'ct'` (confirmed by reading `stationarity.rs` source), but **`kpss_test` has no regression/trend variant at all in 0.16.0** — only the Level ('c') KPSS null hypothesis exists in the crate. This is flagged as an open question requiring a scoping decision before planning D-05 fully (see Open Questions).

**Primary recommendation:** Treat this as three small, independently verifiable plans exactly as D-11 specifies — plan 1 (bump + ADF/KPSS/stationarity SQL + `regression` param, scoped to ADF only pending the KPSS-trend decision), plan 2 (Ljung-Box `fitted_params` wiring + docs rewrite), plan 3 (add the missing "trend" fixture series + Monte Carlo coverage test). Do not attempt to "fix" AutoARIMA, TBATS/MSTL coverage gaps, or Holt/HoltWinters fitting-time state-initialization gaps — all three are upstream-documented, out-of-scope limitations with their own tracked follow-ups.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Crate version/dependency management | Rust Core (Cargo.toml) | — | Pure dependency-graph edit; no C++/SQL involvement |
| ADF/KPSS statistic computation | Rust Core (upstream crate, `anofox_forecast::validation`) | — | Already correct in 0.16.0; extension only marshals |
| Four-way stationarity classification | Rust Core (extension's own `anofox-fcst-core::validation`) | — | This is extension-authored logic wrapping the crate's two booleans; the bug and the fix both live here, not upstream |
| `regression`/`fitted_params` SQL parameters | C++ (scalar function arg parsing) → FFI → Rust Core | SQL Macro (named-default plumbing) | Named-default plumbing is a macro-layer concern (`ts_macros.cpp`); the actual enum/usize threading is FFI+Rust |
| R-parity fixture generation | R scripts (`validation/reference/r/`) | — | Ground truth; Rust tests only consume, never compute expected values |
| Monte-Carlo coverage test | Rust Core (integration test) | — | Pure statistical simulation against the crate's own `predict_with_intervals`; no SQL/C++ needed |
| Docs rewrite (`10-diagnostics.md`) | Docs | — | No code; must be re-verified against the built extension per the snippet harness |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `anofox-forecast` | `0.16.0` [VERIFIED: crates.io registry API, `GET /api/v1/crates/anofox-forecast` — `max_version`/`newest_version` both `"0.16.0"`, published `2026-10-10T22:28:38Z`] | Forecasting models, ADF/KPSS/Ljung-Box, ETS/Theta/GARCH/TBATS/MSTL | Already the project's pinned first-party dependency (`sipemu/anofox-forecast`); this phase only bumps the version string |

**Installation (Cargo.toml edit, not a package install):**
```toml
anofox-forecast = { version = "0.16.0", features = ["anomaly", "serde"] }
```

**Version verification:** [VERIFIED: crates.io dependency API] `anofox-forecast` 0.16.0's dependency list is **identical** to 0.15.10's — same 19 entries (`anofox-statistics ^0.4`, `chrono ^0.4`, `getrandom ^0.2`, `lbfgs ^0.3`, `rand ^0.8.6`, `statrs ^0.18`, `thiserror ^2.0`, `trueno ^0.8`, plus the optional `anofox-regression ^0.5.3`/`bincode ^1.3`/`faer ^0.23`/`fdars-core ^0.9`/`rayon ^1.10`/`serde`/`serde_json`, plus dev-deps `approx`/`criterion`/`proptest`), each with the identical version requirement string. This was independently confirmed by building a throwaway scratch crate (`cargo add anofox-forecast@0.16.0 --features anomaly,serde` in a scratch directory, NOT the extension tree) — it resolved and compiled cleanly, pulling in `anofox-statistics 0.4.8`, `anofox-regression 0.5.26` (via its own looser `^0.5.3` req — irrelevant, since the extension pins its own direct `anofox-regression = "=0.5.15"` separately and this optional feature is not enabled), `trueno 0.8.9`. **No new crate names** enter the dependency graph from this bump alone. The executor must still run `cargo tree` in the real workspace to confirm (per the ROADMAP's own DoD item), since `anofox-statistics`/other transitive crates could receive a compatible patch bump even with an unchanged requirement string — low risk, not zero.

**MSRV:** unaffected — `trueno ^0.8` (the AVX-512 code that set the workspace's `rust-version = "1.89"` floor in Phase 9) has the identical requirement in both versions.

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| R `forecast` | 9.0.2 [VERIFIED: `Rscript -e 'packageVersion("forecast")'` run in this environment] | R reference oracle for FCST-09 | Already installed, matches `validation/reference/README.md`'s documented version exactly — no environment drift |
| R `tseries` | 0.10.63 [VERIFIED: same Rscript check] | KPSS/ADF reference, Ljung-Box `Box.test` reference | Already used by `gen_diagnostics_reference.R`; no change needed |
| R `urca` | 1.3.4 [VERIFIED: same Rscript check] | `ur.df` ADF reference with trend variants | Already used by `gen_diagnostics_reference.R` |
| R `jsonlite` | 2.0.0 [VERIFIED: same Rscript check] | Fixture JSON serialization | Already used by `common.R`'s `write_fixture()` |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Bumping to the released 0.16.0 | An interim `[patch.crates-io]` git/path override on the fix branches | Explicitly rejected by D-01 — the release already exists, making a patch override unnecessary complexity |
| Fixing `classify_stationarity`'s mapping bug in the extension | Reporting it as an upstream crate issue | The bug is 100% extension-side (`crates/anofox-fcst-core/src/validation.rs`); upstream `anofox-forecast` has no `classify_stationarity` function at all |
| Implementing KPSS-trend locally in `anofox-fcst-core` | Scoping `regression` to ADF only, documenting KPSS as Level-only regardless of the requested regression | See Open Questions — this is a real scoping decision, not purely Claude's discretion, because it changes what D-05 can deliver |

## Package Legitimacy Audit

No new external packages are installed by this phase — `anofox-forecast` is an existing, already-vetted first-party dependency (`sipemu/anofox-forecast`, the user's own upstream crate per CLAUDE.md), and only its pinned version string changes. The Package Legitimacy Gate protocol targets net-new package introductions; it is not applicable here. For completeness, registry metadata was still checked directly:

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| `anofox-forecast` | crates.io | 10 months (created 2025-12-12) [VERIFIED: crates.io API `crate.created_at`] | 12,184 total [VERIFIED: crates.io API `crate.downloads`] | `github.com/sipemu/anofox-forecast` [VERIFIED: crates.io API `crate.repository`] | OK (pre-approved, first-party) | Approved — version bump only |

**Packages removed due to [SLOP] verdict:** none.
**Packages flagged as suspicious [SUS]:** none.

## Architecture Patterns

### System Architecture Diagram

```
SQL layer                         C++ scalar/table fns                FFI boundary              Rust core                      Upstream crate (0.16.0)
----------                        ----------------------               ------------              ----------                      ------------------------
ts_adf_by(src,grp,ds,v,            TsAdfFunction (diagnostics.cpp)      anofox_ts_adf(            adf() in validation.rs   -->   adf_test() / adf_test_with_options()
  max_lags:=-1,                     reads LIST(DOUBLE) via                values, validity,         wraps validation::adf_test     -- AdfOptions{regression, lag_selection, max_lags}
  regression:='c')  [NEW param]     ListDoubleReader                     length, max_lags,          (currently hardcodes           -- returns StationarityResult
         |                               |                                [regression NEW],         AdfOptions::default())        -- critical_values now MacKinnon-finite-sample
         v                               v                                out_result, out_error)           |                              |
ts_kpss_by(... )                   TsKpssFunction                       anofox_ts_kpss(...)        kpss() wraps                  kpss_test() -- Level ('c') ONLY,
  [regression param: OPEN            (no regression param                 [no regression param       validation::kpss_test         no trend variant exists (see
   QUESTION, see below]               exists on crate side]                exists on crate side]      (lags only)                   Open Questions)
         |                               |                                     |                              |
         v                               v                                     v                              v
ts_stationarity_by(...)            TsStationarityFunction               anofox_ts_stationarity(    stationarity() calls adf()+kpss(),
                                                                          ...)                       classify_stationarity() -- THIS
                                                                                                      is extension-authored, the
                                                                                                      4-way mapping bug lives HERE
         |
         v
ts_ljung_box_by(src,grp,ds,v,      TsLjungBoxFunction                   anofox_ts_ljung_box(       ljung_box() hardcodes   -->   ljung_box(residuals, lags,
  lags:=-1,                          (no fitted_params param              values, validity,          fitted_params=0                fitted_params) -- ALREADY
  fitted_params:=0)  [NEW param]      exists yet)                         length, lags,               (bug is HERE, not            takes fitted_params; nothing
                                                                            [fitted_params NEW],        upstream)                    to fix upstream for DIAG-04
                                                                            out_result, out_error)
```

### Recommended Project Structure

No new files/directories — this phase edits existing files in place:
```
Cargo.toml                                        # version bump (D-01)
crates/anofox-fcst-core/src/validation.rs         # classify_stationarity fix, regression param, fitted_params param (DIAG-03/04)
crates/anofox-fcst-ffi/src/lib.rs                 # anofox_ts_adf/kpss/stationarity/ljung_box signature additions
src/include/anofox_fcst_ffi.h                      # regenerated cbindgen header (or hand-edited to match)
src/scalar_functions/diagnostics.cpp               # new overloads threading regression/fitted_params
src/macros/ts_macros.cpp                           # ts_adf_by/ts_kpss_by/ts_stationarity_by/ts_ljung_box_by named-param additions
docs/api/10-diagnostics.md                         # rewrite (D-07/DIAG-05)
validation/reference/r/common.R                    # add "trend" series to reference_series() (D-08)
validation/reference/r/gen_forecast_reference.R    # add trend-family models block (D-08)
validation/reference/fixtures/r_forecast/*.json    # regenerated fixtures
crates/anofox-fcst-core/tests/*.rs                 # new FCST-09 parity tests, Monte Carlo coverage test, fix theta_croston_fitted.rs
test/sql/ts_diagnostics.test                       # update cv_1pct/cv_5pct assertions (see Common Pitfalls)
test/sql/ts_forecast_ets_model.test                # update MAA/MAdA "unstable" rejection tests (see Common Pitfalls)
test/sql/ts_native_param_validation.test            # same
CHANGELOG.md                                        # before/after rows (D-11)
```

### Pattern 1: Extension-side classify_stationarity fix (DIAG-03)

**What:** The four-way mapping bug is entirely in `crates/anofox-fcst-core/src/validation.rs`; the fix is a pure relabelling of the match arms, no upstream involvement.

**Current code (verbatim, `crates/anofox-fcst-core/src/validation.rs:124-131`):**
```rust
pub fn classify_stationarity(adf_is_stationary: bool, kpss_is_stationary: bool) -> &'static str {
    match (adf_is_stationary, kpss_is_stationary) {
        (true, true) => "stationary",
        (true, false) => "trend_stationary",
        (false, false) => "difference_stationary",
        (false, true) => "non_stationary",
    }
}
```

**D-04's correct mapping, translated into the same `(adf_is_stationary, kpss_is_stationary)` bool pairs** (`adf_is_stationary` = ADF rejects its unit-root null; `kpss_is_stationary` = KPSS does NOT reject its stationarity null — per the existing doc comment on `CombinedStationarityOut::kpss_is_stationary`):

| `adf_is_stationary` | `kpss_is_stationary` | D-04 label | Current (wrong) label |
|---|---|---|---|
| true | true | `stationary` | `stationary` (correct, unchanged) |
| true | false | `difference_stationary` | `trend_stationary` (WRONG) |
| false | false | `non_stationary` | `difference_stationary` (WRONG) |
| false | true | `trend_stationary` | `non_stationary` (WRONG) |

Only the `(true,true)` branch is currently correct; all three others swap pairwise. This mapping is independently corroborated by a general web search on the ADF/KPSS four-way convention [CITED: general econometrics consensus, consistent with statsmodels' stationarity/detrending example] — ADF-non-reject + KPSS-stationary = trend-stationary; ADF-stationary + KPSS-reject = difference-stationary.

**When to use:** This is the exact fix for DIAG-03's four-way mapping half. The constant-series half needs a new pre-check (Pattern 2).

### Pattern 2: Constant-series short-circuit (D-04's "constant" verdict)

**What:** Before calling ADF/KPSS at all, check `filter::is_constant` and short-circuit to a dedicated verdict — the existing `is_constant` helper (`crates/anofox-fcst-core/src/filter.rs:39-48`) already exists and is reused elsewhere in the crate:

```rust
// Source: crates/anofox-fcst-core/src/filter.rs:39-48 (read this session)
pub fn is_constant(values: &[Option<f64>]) -> bool {
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();
    if non_null.len() < 2 {
        return true;
    }
    let first = non_null[0];
    non_null.iter().all(|v| (v - first).abs() < f64::EPSILON)
}
```

Note the signature mismatch: `is_constant` takes `&[Option<f64>]`, but `validation::stationarity(series: &[f64])` takes a plain slice (NULLs already trimmed upstream by the FFI `anofox_ts_series_gate` gate). A `stationarity()` caller that wants the constant short-circuit must either call `is_constant` on a `&[Option<f64>]`-wrapped copy, or (simpler) inline an equivalent all-equal check directly on the `&[f64]` it already has — avoid introducing a second constant-detection implementation; prefer reusing `filter::is_constant`'s exact tolerance (`f64::EPSILON`, not a scale-relative one — flagged separately in the audit as G25/DET-07, Phase 15 scope, not this phase's concern).

**When to use:** Called first in `stationarity()`/`classify_stationarity`'s call path, before the ADF/KPSS calls, returning a `"constant"` verdict and skipping ADF/KPSS entirely (both degenerate on a constant series — KPSS's own `variance <= 0.0` branch already returns a NaN-statistic, `is_stationary: true` result for this case, confirmed by reading `kpss_test`'s source in the 0.16.0 crate).

**No R fixture needed for this case** — `tseries::kpss.test`/`adf.test` error or warn on a literally constant input rather than returning a clean reference value, so this is better verified as a pure Rust/SQL structural test (`assert_eq!(classify_stationarity_from_series(&[5.0; 10]), "constant")`), not an R-fixture-backed value assertion.

### Pattern 3: Ljung-Box `fitted_params` wiring (DIAG-04) — pure plumbing, not a fix

**What:** The upstream crate's `ljung_box` function already has the exact signature DIAG-04 wants:

```rust
// Source: anofox-forecast 0.16.0 src/validation/residual_tests.rs:37 (read this session, via
// ~/.cargo/registry/src/.../anofox-forecast-0.16.0/)
pub fn ljung_box(residuals: &[f64], lags: Option<usize>, fitted_params: usize) -> LjungBoxResult
// ... df: lags.saturating_sub(fitted_params) (line 83)
```

The extension's wrapper hard-codes the third argument:

```rust
// Source: crates/anofox-fcst-core/src/validation.rs:178-180 (read this session)
pub fn ljung_box(residuals: &[f64], lags: Option<usize>) -> LjungBoxOut {
    validation::ljung_box(residuals, lags, 0).into()
}
```

**When to use:** Add a `fitted_params: usize` parameter to this wrapper function, to `anofox_ts_ljung_box` in `crates/anofox-fcst-ffi/src/lib.rs` (currently `anofox_ts_ljung_box(values, validity, length, lags, out_result, out_error)` — no `fitted_params` slot at all per the signature read at `lib.rs:7274-7313`), to the generated C header, to the `TsLjungBoxFunction` scalar function in `src/scalar_functions/diagnostics.cpp`, and to the `ts_ljung_box_by` macro in `src/macros/ts_macros.cpp` as a new named default parameter `{"fitted_params", "0"}`. D-06's `df <= 0 raises an error` requirement is NOT in the crate (`saturating_sub` silently floors at 0, never errors) — this validation must be added in the extension layer (C++ or Rust wrapper) before or after calling the crate function.

**Existing R reference values already cover this** — `validation/reference/fixtures/r_diagnostics/stationarity_portmanteau.json` (generated by `gen_diagnostics_reference.R`) already has `ljung_box_lag10_fitdf0` and `ljung_box_lag10_fitdf2` blocks for every series (`air`, `log_air`, `rw`, `diff_rw`, `ar1`), each carrying R's own `Box.test(y, lag=10, type="Ljung-Box", fitdf=2)` statistic/df/p_value. No new R generator work is needed for DIAG-04's fixture coverage; the Rust test just needs to load this existing fixture and assert against the `fitdf2` block with `fitted_params=2`.

### Pattern 4: Extending the R-forecast parity harness (FCST-09)

**What:** `reference_series()` in `validation/reference/r/common.R` currently returns exactly `air`, `rw`, `ar1`, `inter` — D-08's third simulated series ("trend") does not exist:

```r
# Source: validation/reference/r/common.R:51-61 (read this session)
reference_series <- function() {
  RNGkind("Mersenne-Twister", "Inversion", "Rejection")
  set.seed(20261008)
  air <- AirPassengers
  rw <- cumsum(rnorm(200))
  ar1 <- as.numeric(arima.sim(list(ar = 0.7), n = 300))
  inter <- ifelse(runif(120) < 0.3, rpois(120, 3) + 1, 0)
  list(air = air, rw = rw, ar1 = ar1, inter = inter)
}
```

**When to use:** Add a new `trend` series **after** `inter`'s `runif(120)` draw (append to the end of the function body, return list last), so every existing RNG-dependent fixture (`rw`, `ar1`, `inter`, and anything consuming them downstream) stays byte-identical on regeneration — this exact "append, don't insert" RNG-order discipline is the same one `gen_forecast_reference.R`'s own `ar1_l10` comment already documents for this file ("Consumes no RNG draws, so rw/inter and air_passengers.json stay byte-identical to the pre-existing fixture"). A simple local-trend DGP (e.g. `trend <- cumsum(rnorm(150, mean = 0.3, sd = 1))` — a random walk with drift, i.e. ETS(A,A,N)'s natural DGP) is sufficient; it must be genuinely trending (not already covered by `rw`, which has zero drift) and non-seasonal (SNaive/HW do not apply to it, matching the `rw` family's existing model scoping in `gen_forecast_reference.R`).

Then extend `gen_forecast_reference.R` with a `trend` family block modeled exactly on the existing `rw` family (`naive`, `rwf_drift`, `ses`, `holt`, `ets_auto`, `auto_arima`, `thetaf` — SNaive/HW/Croston are not applicable, matching `rw`'s own scoping), and add it to `sim_fixture$series`.

**Existing tolerance keys already anticipate this phase** — `validation/reference/README.md`'s Families table documents `r_forecast`'s tolerance keys (`closed_form_abs` 1e-8, `model_point_rel` 0.01, `interval_rel` 0.05) as explicitly "parity targets for later phases ... Phase 17 FCST-09, not asserted in Phase 9" — no new tolerance *keys* are required, only (per D-09) documenting a tighter/looser rationale per model tier if the existing three keys are too coarse.

### Anti-Patterns to Avoid

- **Re-deriving the KPSS-trend table by hand:** `tseries::kpss.test(null="Trend")` uses a DIFFERENT 4-point critical-value table ((0.119,0.10)...(0.216,0.01)) and a different (OLS-detrended) cumsum procedure than the Level variant. Do not hand-roll this in C++/SQL or guess at the table — if the scoping decision is to implement it, it belongs in the extension's own Rust core (`anofox-fcst-core`), verified against the fixture generator's ALREADY-EXISTING `kpss_trend` reference block (see Open Questions), not hand-derived.
- **Trusting the asymptotic ADF critical-value constants after the bump:** `-3.43`/`-2.86`/`-2.57` are the large-n asymptotic Dickey-Fuller constants; 0.16.0's `mackinnon_critical_values` is finite-sample-adjusted (uses `nobs`) and will differ by ~0.04-0.2 at n≈40 (see Common Pitfalls for the exact computed numbers).
- **Assuming `adf_test`/`kpss_test` signatures changed:** They did not — `adf_test(series, max_lags)` and `kpss_test(series, lags)` are unchanged backward-compatible delegations to the new options-taking functions. Only the *values* they return changed, not their call sites in the extension's wrapper.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| MacKinnon ADF p-values/critical values | A custom lookup table or interpolation | `anofox_forecast::validation::mackinnon_p_value`/`mackinnon_critical_values` (already in 0.16.0) | Already ported from statsmodels' own shipped coefficients, proven to 1e-10/1e-12 against statsmodels by the upstream crate's own tests |
| Augmented ADF regression | A hand-written OLS-on-differenced-series loop | `anofox_forecast::validation::adf_test_with_options` | Already implements the Cholesky-on-normal-equations augmented regression, column-order-matched to statsmodels |
| KPSS p-value bounding | A custom clamp | `anofox_forecast::validation::kpss_p_value` (public since 0.16.0) | Already matches `tseries::kpss.test`'s exact 4-point table + clamp-outside-table behavior |
| Normal inverse-CDF (z-value for intervals) | A rational approximation | `statrs::distribution::Normal::inverse_cdf`, already used crate-wide since the 0.16.0 `quantile_normal` fix (Peter Acklam's approximation, ~1.15e-9 relative error, replacing the old Abramowitz-Stegun ~4.5e-4 approximation) | The crate fixed this itself in 0.16.0; the extension should not have its own z-value approximation anywhere (verified: it doesn't — the extension's `calculate_confidence_intervals` fabrication path was already deleted in Phase 10, FCST-01/02) |

**Key insight:** Everything DIAG-01/02 needed from the crate is now present and correct in 0.16.0. The only hand-rolling risk in this phase is the KPSS-trend gap (Open Questions) — if the scoping decision is to implement it, it must go in Rust, verified against R, never approximated in C++/SQL.

## Common Pitfalls

### Pitfall 1: Two SQL test files will fail immediately after the bump (ETS "unstable" rejection removed)

**What goes wrong:** `ETSSpec::is_valid()` in the crate changed from a two-combination exclusion list (`ETS(M,A,A)`/`ETS(M,Ad,A)`) to an unconditional `true` — [VERIFIED: anofox-forecast 0.16.0 CHANGELOG.md, "Changed — ETS (UPST-03)" section, read from the registry source cache] states: *"`ETSSpec::is_valid()` now always returns `true`. `ETS(M,A,A)` and `ETS(M,Ad,A)` are valid R `forecast::ets` models ... `from_notation("MAA")`/`from_notation("MAdA")` now parse successfully instead of erroring."*

The extension calls this exact function as a rejection gate in two places in `crates/anofox-fcst-core/src/forecast.rs` (verified by reading both call sites this session):

```rust
// forecast.rs:1383-1389 and (near-identical) forecast.rs:1940-1946
if !parsed_spec.is_valid() {
    return Err(ForecastError::InvalidInput(format!(
        "ETS model '{}' is an unstable combination (multiplicative error with \
         additive components).", notation
    )));
}
```

Three SQL test files assert this now-dead error path:
```
test/sql/ts_forecast_ets_model.test:82-92   (two cases: 'MAA', 'MAdA', each expects the string "unstable")
test/sql/ts_native_param_validation.test:147-155  (same two cases, same expected error substring)
```

**Why it happens:** The extension's rejection gate is a thin wrapper around the crate's own validity check. When the crate widens what it considers valid, the extension's gate silently stops rejecting — the SQL tests, which assert on the rejection, must change.

**How to avoid:** After the bump, update all 4 test cases to assert successful execution (`ts_forecast_by(..., MAP{'model':'MAA'})` now returns a real forecast, not an error) instead of the "unstable" error string. Also delete or rewrite the now-misleading error message and comment at both `forecast.rs` call sites (`// Reject unstable combinations (multiplicative error with additive components)` is no longer accurate — MAA/MAdA are reachable and valid). Document this in CHANGELOG.md as a breaking/behavior-widening change (previously-rejected `ets_spec` values now succeed).

**Warning signs:** `make test`/`test_release` failures on exactly these two test files, with the actual output no longer containing "unstable".

### Pitfall 2: ADF critical-value hardcoded SQL test assertions will almost certainly fail

**What goes wrong:** `test/sql/ts_diagnostics.test:70-87` asserts (tolerance `< 0.05`):
```sql
SELECT ABS((adf).cv_1pct - (-3.43)) < 0.05 ...   -- true (pre-0.16.0)
SELECT ABS((adf).cv_5pct - (-2.86)) < 0.05 ...   -- true (pre-0.16.0)
SELECT ABS((adf).cv_10pct - (-2.57)) < 0.05 ...  -- true (pre-0.16.0)
```
on `diag_series` group B (n=40). After the bump, `adf_test`'s critical values come from `mackinnon_critical_values(regression, nobs)` — a finite-sample-corrected surface, not a fixed constant:

```rust
// Source: anofox-forecast 0.16.0 src/validation/stationarity.rs:573-584 (read this session)
pub fn mackinnon_critical_values(regression: AdfRegression, nobs: usize) -> CriticalValues {
    let eval = |coef: &[f64; 4]| -> f64 {
        let x = 1.0 / nobs as f64;
        coef[0] + coef[1] * x + coef[2] * x * x + coef[3] * x * x * x
    };
    // TAU_2010[1] (regression = Constant/'c'):
    //   cv_1pct coefs:  [-3.43035, -6.5393, -16.786, -79.433]
    //   cv_5pct coefs:  [-2.86154, -2.8903, -4.234, -40.040]
    //   cv_10pct coefs: [-2.56677, -1.5384, -2.809, 0.0]
    ...
}
```

Evaluating this by hand at `nobs≈36-39` (B's 40-point series minus `1 + chosen_lag`): `cv_1pct ≈ -3.61` to `-3.63` (diff from `-3.43` ≈ **0.18-0.20**, exceeds the 0.05 tolerance); `cv_5pct ≈ -2.94` (diff from `-2.86` ≈ **0.08**, exceeds tolerance); `cv_10pct ≈ -2.61` (diff from `-2.57` ≈ **0.04**, borderline-passes). This is arithmetic performed by hand from the verbatim constants above — not independently re-run through the built extension (per the read-only constraint on this research session) — so treat it as a strong, well-evidenced prediction, not a confirmed test result; the executor must actually run the test to confirm.

**Why it happens:** The old hardcoded test comment ("critical values match expected MacKinnon constants for constant regression") was written when the crate used literal asymptotic constants for every sample size; 0.16.0 makes them finite-sample-dependent as MacKinnon always intended.

**How to avoid:** Either (a) widen the tolerance substantially (to ≥0.25) and accept a loose structural check, or (b) — preferred, consistent with this milestone's "assert values against a reference fixture" ethos — compute the n-specific expected critical value via the same formula/constants and assert against that, or pull the expected value from a small checked-in fixture. Do not silently loosen without documenting why in CHANGELOG (D-11 requires before/after rows for breaking changes; this crosses the line into "the test result changed" even if the SQL function's own contract did not).

**Warning signs:** `test/sql/ts_diagnostics.test` failures on the three `cv_*pct` assertions specifically, with `isfinite`/`BETWEEN 0 AND 1` assertions elsewhere in the same file still passing.

### Pitfall 3: A Theta test is explicitly documented to break — update it, don't delete it

**What goes wrong:** `crates/anofox-fcst-core/tests/theta_croston_fitted.rs`'s own doc comment (read this session, lines 20-27) says: *"Phase 11 (UPST-04 ... ) is expected to BREAK the first test here once Theta's alpha stops being fixed at 0.1 — when that happens, update `theta_and_croston_classic_fitted_are_both_ses_alpha_0_1_on_positive_series` to reflect the new (optimised-alpha) Theta behaviour; do not silently delete it, since the other two tests still document real, load-bearing crate behaviour."*

0.16.0's Theta fix (`Theta::new()` now estimates alpha by SES-SSE optimization instead of a fixed `0.1`) does exactly this. The first of the file's three tests will fail; the other two (`wrapper_fitted_is_the_crate_models_own_fitted_values`, `croston_fitted_diverges_from_theta_on_intermittent_series`) should still pass unchanged (they assert dispatch identity and intermittent-series divergence, not the specific alpha=0.1 coincidence).

**Why it happens:** Theta's STM alpha was a hard-coded `0.1` pre-0.16.0; CrostonClassic independently also defaults to alpha `0.1`. The coincidence (bitwise-identical fitted arrays on a non-seasonal, no-zero series) breaks once Theta's alpha becomes optimized (likely landing near `0.9999` on a trending series like the AirPassengers prefix this test uses, matching the `rw` family's observed `0.9999` from the upstream PR evidence).

**How to avoid:** Update the first test's hard-coded `independent_ses_alpha_0_1` expectation / assertions to reflect the real (now-optimized) alpha Theta converges to on this specific 24-point AirPassengers-prefix series — do not simply delete the assertion or loosen it without computing the real expected value.

### Pitfall 4: KPSS `regression := 'ct'` cannot be honored by the crate

**What goes wrong:** D-05 asks for a `regression := 'c' | 'ct'` parameter on `ts_adf_by`, `ts_kpss_by`, AND `ts_stationarity_by`. The crate's `kpss_test(series, lags)` (verified by reading the full function body this session, `stationarity.rs:658-730`) has **no regression/trend parameter at all** — it only ever computes the Level ('c') KPSS statistic (demean the series, cumsum, Bartlett-kernel HAC variance, compare against the fixed Level critical-value table `{0.347, 0.463, 0.574, 0.739}`). There is no `KpssRegression` enum, no trend-detrending step, nothing analogous to `AdfOptions`.

**Why it happens:** None of Phase 11's PRs (#221/#222/#224/#225) added a KPSS-trend variant — PR #221's scope was augmenting the ADF regression and bounding the KPSS p-value, not adding a new KPSS specification. This is a genuine upstream gap, not an extension bug.

**How to avoid:** This needs a scoping decision before Phase 17's plan 1 is finalized — see Open Questions. The ADF side of D-05 is fully deliverable (the crate supports `'c'`/`'ct'` exactly as D-05 specifies); only the KPSS side has a gap.

**Warning signs:** Attempting to find a `KpssRegression`/`AdfRegression`-equivalent for KPSS, or a `kpss_test_with_options`, in the 0.16.0 source will come up empty.

### Pitfall 5: Ljung-Box's `df <= 0` error requirement is NOT enforced by the crate

**What goes wrong:** D-06 requires `df ≤ 0 raises an error`. The crate's `ljung_box` computes `df = lags.saturating_sub(fitted_params)` — `saturating_sub` floors at `0` silently; it never errors, regardless of how large `fitted_params` is relative to `lags`.

**Why it happens:** `saturating_sub` is a `usize`-arithmetic safety idiom (prevents underflow panics), not a validation gate — the crate was never asked to validate this invariant, only to compute df correctly when it's non-negative.

**How to avoid:** The `df <= 0` validation must be added explicitly in the extension (either in the Rust wrapper before calling the crate function, or in the C++ scalar function after reading the crate's returned `df`). Recommended: validate in the Rust wrapper (`crates/anofox-fcst-core/src/validation.rs`'s `ljung_box` function) so both the FFI boundary and any future direct Rust caller get the same guarantee, consistent with the project's "statistics live in Rust, C++/SQL only marshal" rule.

## Code Examples

### Reading the released crate's own CHANGELOG for authoritative scope confirmation

```bash
# Source: direct crates.io registry query, not training data
curl -sS -A "research-agent" https://crates.io/api/v1/crates/anofox-forecast/0.16.0 | python3 -m json.tool
# -> "yanked": false, "created_at": "2026-10-10T22:28:38Z", "num": "0.16.0"

cargo new --lib probe && cd probe
cargo add anofox-forecast@0.16.0 --features anomaly,serde
# Resolves cleanly; CHANGELOG.md is then readable at
# ~/.cargo/registry/src/index.crates.io-*/anofox-forecast-0.16.0/CHANGELOG.md
```

### Threading a new named-default macro parameter (existing precedent to follow)

```cpp
// Source: src/macros/ts_macros.cpp:2416-2427 (read this session) — the pattern
// `ts_adf_by` already uses for `max_lags`; `regression` should follow the same
// shape, added as a new named default:
{"ts_adf_by", {"source", "group_col", "date_col", "value_col", nullptr},
 {{"max_lags", "-1"}, {"regression", "'c'"}, {nullptr, nullptr}},
R"(
SELECT group_col, ts_adf(LIST(value_col::DOUBLE ORDER BY date_col),
                          max_lags::INTEGER, regression::VARCHAR) AS adf
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
...
```

### Ljung-Box df validation (D-06) — not provided by the crate, must be added

```rust
// Recommended location: crates/anofox-fcst-core/src/validation.rs, replacing
// the current hardcoded-0 wrapper (validation.rs:178-180, read this session)
pub fn ljung_box(residuals: &[f64], lags: Option<usize>, fitted_params: usize) -> Result<LjungBoxOut, String> {
    let resolved_lags = lags.unwrap_or_else(|| (residuals.len() / 5).min(10));
    if resolved_lags <= fitted_params {
        return Err(format!(
            "ts_ljung_box: fitted_params ({}) must be less than lags ({}) — degrees of freedom would be <= 0",
            fitted_params, resolved_lags
        ));
    }
    Ok(validation::ljung_box(residuals, lags, fitted_params).into())
}
```

## State of the Art

| Old Approach (≤0.15.10) | Current Approach (0.16.0) | When Changed | Impact |
|--------------------------|----------------------------|---------------|--------|
| ADF: non-augmented regression, 9-step p-value table, hardcoded asymptotic critical values | Genuinely augmented regression; MacKinnon (1994/2010) p-values and finite-sample critical values | 2026-10-10 (PR #221, released 0.16.0) | ADF statistics/p-values/critical-values ALL change numerically; unit-root test size corrected from 63.5% to ~7-9% on an IMA(1,1) fixture |
| KPSS: p-value extrapolated outside the table | p-value bounded to [0.01, 0.10] | 2026-10-10 (PR #221) | p-values near the extremes change; statistic itself unchanged |
| ETS: Σln\|y\| multiplicative-error likelihood, independent parameter boxes, is_valid() rejects MAA/MAdA | Σln\|ŷ\|, R's usual constraints, is_valid() always true (is_r_restricted() replaces it for AutoETS's pool) | 2026-10-10 (PR #224) | Every ETS/AutoETS loglik/AIC/AICc/BIC changes; AutoETS candidate pool changes (AirPassengers: picks ETS(M,A,M) now, not ETS(A,N,M)); explicit MAA/MAdA requests now succeed |
| ETS/SES/Holt/HoltWinters/SeasonalES intervals: flat `sigma*sqrt(ceil(h/m))`-family approximations | Hyndman et al. (2008) class1/2/3 dispatch (analytic or simulated per model shape) | 2026-10-10 (PR #224) | Every interval width changes; some by orders of magnitude (AirPassengers ETS(M,A,M) 95% half-width: 0.07 → 35.3, matching R's 35.0) |
| Theta: fixed alpha=0.1, `1/alpha` drift approximation | Alpha estimated by SES-SSE optimization; thetaf's exact finite-sample drift/interval formulas | 2026-10-10 (PR #222) | Random-walk forecast bias removed; `with_theta_value` signature gains a required `n: usize` parameter (breaking, but the extension has zero call sites) |
| GARCH/TBATS/MSTL intervals: various ad-hoc approximations | GARCH centred on conditional mean; TBATS uses innovations state-space variance; MSTL delegates to the trend forecaster's own bounds | 2026-10-10 (PR #225) | Coverage corrected (GARCH 63%→95%, TBATS 97.6%); two TBATS fixtures and MSTL's short-horizon coverage remain documented, non-crate-specific gaps (optimizer admissibility, STL boundary bias) |

**Deprecated/outdated:**
- Everything in `docs/api/10-diagnostics.md`'s "Caveats" section (lines 169-212) describing non-augmentation, RSS-not-AIC lag selection, and the 9-point p-value table — all three are now false and must be removed, not merely annotated, per D-07.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The exact finite-sample ADF critical-value deviations computed by hand (Pitfall 2: ~0.18-0.20 for cv_1pct, ~0.08 for cv_5pct, ~0.04 for cv_10pct at nobs≈36-39) are arithmetic performed from the verbatim MacKinnon coefficients, not confirmed by actually running the built extension (disallowed in this read-only research session) | Common Pitfalls, Pitfall 2 | If the actual chosen lag at runtime pushes `nobs` meaningfully outside the 36-39 range assumed, the magnitude (though not the direction) of the deviation could differ; the executor should verify by running the test, not by trusting this estimate blindly |
| A2 | A simple local-trend DGP (random walk with drift, e.g. `cumsum(rnorm(150, mean=0.3, sd=1))`) is an adequate "trend" series for D-08/FCST-09 — the exact parameters (mean drift, sd, n) are Claude's discretion per CONTEXT.md, but the choice of DGP *family* (random-walk-with-drift vs. a deterministic-trend-plus-noise alternative) affects which models are "appropriate" R comparisons | Pattern 4 | A deterministic-trend DGP (e.g. `0.5*(1:150) + rnorm(150)`) would exercise ETS(A,A,N)/Holt differently than a stochastic-trend (I(1) with drift) DGP; either is defensible, but the plan should pick one explicitly and document why, not default silently |
| A3 | The recommended location for the `df <= 0` Ljung-Box validation (Rust wrapper, not C++) is a design preference consistent with the project's "statistics in Rust" rule, not something the crate or CONTEXT.md mandates | Pattern 3 / Code Examples | If the plan instead validates in C++ (reading the crate's returned `df` and throwing a DuckDB exception when `df <= 0`), that is equally valid and arguably simpler (no new Result-returning Rust wrapper signature) — this is a genuine implementation choice, flagged as Claude's Discretion territory already covered by CONTEXT.md |

**If this table is empty:** N/A — populated above.

## Open Questions

1. **Can D-05's `regression := 'c' | 'ct'` parameter be delivered for `ts_kpss_by`, given the crate has no KPSS-trend variant in 0.16.0?**
   - What we know: The crate's `AdfOptions.regression: AdfRegression` genuinely supports `NoConstant`/`Constant`/`ConstantTrend` for ADF (verified by reading `stationarity.rs`). The crate's `kpss_test` has NO regression parameter, NO trend table, NO detrending step (verified by reading the full function body) — it unconditionally computes the Level ('c') KPSS statistic. The project's OWN fixture generator (`gen_diagnostics_reference.R`) already generates `kpss_trend` reference values via `tseries::kpss.test(y, null="Trend")` for every series — meaning the research/fixture infrastructure anticipated this capability existing, but the crate side of it was never built (not in any of Phase 11's 5 PRs).
   - What's unclear: Whether Phase 17 should (a) scope `regression` to ADF only and have `ts_kpss_by`'s `regression` parameter either not exist, exist but only accept `'c'` (erroring on `'ct'` with a clear message), or silently ignore `'ct'` and always run Level KPSS (documented); or (b) implement a genuine KPSS-trend variant as new Rust code in `anofox-fcst-core` (OLS-detrend against a linear time trend before the existing cumsum/HAC-variance procedure, with the Trend critical-value table `{0.119, 0.146, 0.176, 0.216}` at `{0.10,0.05,0.025,0.01}` — this table is NOT currently anywhere in the codebase and would need to be added, verified against the already-generated `kpss_trend` fixture block).
   - Recommendation: Option (a) — scope `regression` to ADF for this phase, with `ts_kpss_by`/`ts_stationarity_by` either rejecting `regression:='ct'` with an explicit "KPSS trend variant not yet implemented, use ts_adf_by with regression:='ct' for the ADF-only trend test" error, or (simpler, less surprising) accepting `'ct'` syntactically but running Level KPSS regardless with that behavior documented in the function's description. Implementing a new statistical test (b) is a meaningfully larger, independently-verifiable unit of work that does not fit cleanly into Phase 17's framing as "consume the already-done upstream work" — if the user wants it, it should likely be its own small plan or a tracked follow-up, analogous to how AutoARIMA's gap (D-02) is tracked rather than absorbed into this phase. **This needs a human decision before planning D-05 in detail** — it changes what "trend-stationary, reachable via the regression parameter" can mean for the KPSS half of the four-way verdict.

2. **Does `ts_cv_forecast_by('AutoEnsemble')`'s segfault (crate 0.15.3, STATE.md-tracked) reproduce on 0.16.0?**
   - What we know: The segfault was observed on crate 0.15.3; the extension has since bumped through 0.15.10 (Phase 9) without the memory note being updated to say it's fixed. None of Phase 11's PRs touch ensemble-building code paths directly (they touch ADF/KPSS/Theta/ETS/GARCH/TBATS/MSTL internals, not `AutoEnsemble`'s own dispatch).
   - What's unclear: Whether the segfault's root cause (not investigated in this research session — out of scope for a read-only pass, and the environment constraints prohibit building/running the extension in this tree during this session) is affected incidentally by the ETS/Theta interval-formula changes (since `AutoEnsemble` likely includes ETS/Theta as members).
   - Recommendation: The Phase 17 plan should include an explicit manual re-verification step (run `ts_cv_forecast_by('AutoEnsemble')` once against the built 0.16.0 extension) rather than assuming it's fixed or still broken — this is a "re-check" action item per the research brief, not something resolvable from source reading alone.

3. **Will the `cargo tree` dependency surface actually stay byte-identical when the lock is regenerated?**
   - What we know: The dependency *declarations* (names + version requirement strings) are identical between 0.15.10 and 0.16.0 per the crates.io registry API. A scratch-crate build resolved `anofox-statistics` to `0.4.8` and `trueno` to `0.8.9` with no new crate names.
   - What's unclear: Whether re-running `cargo update -p anofox-forecast` (or equivalent) inside the actual workspace's existing `Cargo.lock` picks up any transitive patch-level version bumps (e.g. a newer `anofox-statistics` patch release published between when 0.15.10's lock entries were pinned and now) that could shift numerical output in some unrelated, as-yet-undiscovered way.
   - Recommendation: The executor must run `cargo tree -p anofox-forecast` before/after in the real workspace and diff it, exactly as Phase 9's own precedent did for the 0.15.3→0.15.10 bump ("125-entry pre-bump snapshot proved only MFLES/AutoMFLES/Laplace point forecasts moved").

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| crates.io registry access | D-01 (consuming the 0.16.0 release) | ✓ [VERIFIED: direct `curl` to `crates.io/api/v1/crates/anofox-forecast` this session] | n/a | — |
| Rscript / R `forecast`/`tseries`/`urca`/`jsonlite` | D-08 (regenerating `gen_forecast_reference.R`) | ✓ [VERIFIED: `Rscript -e 'packageVersion(...)'` run this session] | R 4.6.1, forecast 9.0.2, tseries 0.10.63, urca 1.3.4, jsonlite 2.0.0 — matches `validation/reference/README.md`'s documented toolchain exactly, zero drift | — |
| `cargo` / scratch-crate build of `anofox-forecast@0.16.0` | Confirming dependency-surface identity | ✓ [VERIFIED: built successfully in a scratch directory this session] | — | — |
| Building the extension itself (`make release`) | Confirming the SQL test breakages predicted in Common Pitfalls | ✗ (explicitly disallowed in this research session — a Phase 15 executor may be building in the same tree) | — | No fallback; the Phase 17 EXECUTOR must run this, not research. The specific test-breakage predictions above are well-evidenced from source reading but NOT independently confirmed by a test run — flagged throughout as such. |

**Missing dependencies with no fallback:**
- A real build+test run of the bumped extension — necessarily deferred to the executor (this research session's environment constraints explicitly prohibit it in the shared extension tree).

## Security Domain

`security_enforcement` is enabled (`.planning/config.json` — absent would also default to enabled). This phase touches no new attack surface: no new network calls, no new file I/O beyond the existing fixture-regeneration pattern, no new auth/session concerns. It is a pure numerical-correctness and dependency-version phase operating on already-validated SQL diagnostic functions.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A — DuckDB extension, no auth surface |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Yes | New `regression`/`fitted_params` SQL parameters must validate their domain (regression ∈ {'c','ct'[,'n']}, fitted_params < lags) and reject invalid input with a clear DuckDB exception — following the existing pattern (`check_null_pointers`, `catch_unwind`-wrapped FFI calls) already used by every diagnostics function in `diagnostics.cpp`/`lib.rs` |
| V6 Cryptography | No | N/A |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed `regression`/`fitted_params` input causing a panic across the FFI boundary | Denial of Service | Existing `catch_unwind(AssertUnwindSafe(...))` wrapping in every FFI function (already the established pattern for `anofox_ts_adf`/`anofox_ts_kpss`/etc. — verified present at every call site read this session) must be preserved for any new parameter-handling code added inside those functions |
| Supply-chain: consuming a malicious/compromised crate version | Tampering | `anofox-forecast` is a first-party crate (`sipemu/anofox-forecast`, same maintainer); version pinned exactly (not a caret range) in `Cargo.toml`; `Cargo.lock` committed — standard Rust supply-chain hygiene already in place, unchanged by this phase |

## Sources

### Primary (HIGH confidence)

- `~/.cargo/registry/src/index.crates.io-*/anofox-forecast-0.16.0/src/validation/stationarity.rs` — ADF/KPSS source, read directly this session
- `~/.cargo/registry/src/index.crates.io-*/anofox-forecast-0.16.0/src/validation/residual_tests.rs` — Ljung-Box source, read directly this session
- `~/.cargo/registry/src/index.crates.io-*/anofox-forecast-0.16.0/CHANGELOG.md` — authoritative released-scope confirmation, read directly this session
- `https://crates.io/api/v1/crates/anofox-forecast` / `.../0.16.0` / `.../0.16.0/dependencies` — registry API, queried directly this session via `curl`
- `crates/anofox-fcst-core/src/validation.rs`, `crates/anofox-fcst-ffi/src/lib.rs` (lines 7000-7340), `src/scalar_functions/diagnostics.cpp`, `src/macros/ts_macros.cpp` (lines 2395-2490), `crates/anofox-fcst-core/src/forecast.rs` (lines 1360-1400, 1880-1950), `crates/anofox-fcst-core/src/filter.rs`, `crates/anofox-fcst-core/tests/theta_croston_fitted.rs`, `validation/reference/r/common.R`, `validation/reference/r/gen_forecast_reference.R`, `validation/reference/r/gen_diagnostics_reference.R`, `validation/reference/README.md`, `docs/api/10-diagnostics.md`, `test/sql/ts_diagnostics.test`, `test/sql/ts_forecast_ets_model.test`, `test/sql/ts_native_param_validation.test` — all read directly this session
- `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-0{1..11}-SUMMARY.md` — Phase 11's own executor summaries, read directly this session
- `.planning/research/STAT-AUDIT-2026-10-08.md` §A1, §C, §H — the original audit, read directly this session
- Rscript version checks (R 4.6.1, forecast 9.0.2, tseries 0.10.63, urca 1.3.4, jsonlite 2.0.0) run directly in this environment this session

### Secondary (MEDIUM confidence)

- General web search corroboration of the ADF/KPSS four-way stationarity classification convention (statsmodels' own stationarity/detrending example page appeared in results; summary consistent with D-04's locked mapping) [CITED: general econometrics consensus]

### Tertiary (LOW confidence)

- None — every substantive claim in this document was verified against either the released crate's own source/CHANGELOG, this repository's own source, or a direct registry/toolchain query this session.

## Metadata

**Confidence breakdown:**
- Standard stack (the 0.16.0 bump itself): HIGH — confirmed released on crates.io, dependency surface read directly, CHANGELOG read directly
- Architecture (diagnostics fix locations): HIGH — every file/line cited was read this session, with verbatim quotes
- Pitfalls (test breakage predictions): HIGH for the ETS "unstable" test breakage (directly confirmed via CHANGELOG + test file content); MEDIUM for the exact magnitude of the ADF critical-value test breakage (correct formula and constants, hand-computed consequence, not independently run against the built extension per this session's constraints)
- FCST-09 fixture gap (missing "trend" series): HIGH — confirmed by reading `reference_series()`'s complete source
- KPSS-trend gap (Open Question 1): HIGH confidence that the gap exists; the resolution is a scoping decision, not a research question

**Research date:** 2026-10-12
**Valid until:** This research is tied to a specific, dated upstream release (anofox-forecast 0.16.0, 2026-10-10) and a specific point-in-time read of this repository's source. Re-verify file line numbers if Phases 15/16 (which execute first per D-03) touch any of the same files before Phase 17 executes.
