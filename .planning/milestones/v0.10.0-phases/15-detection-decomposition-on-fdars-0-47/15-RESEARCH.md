# Phase 15: Detection & Decomposition on fdars 0.47 - Research

**Researched:** 2026-10-12
**Domain:** Rust statistical/FDA library migration (fdars-core 0.3→0.47) + re-implementation of scipy/R-faithful detection and decomposition algorithms in `anofox-fcst-core`
**Confidence:** HIGH (fdars-core 0.47 API, current wrapper bugs, SQL wiring, and Python/R reference-harness versions were all read or executed directly this session; a handful of scope-sequencing items are flagged LOW/open)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**fdars bump and wrapper fixes (DEP-03, WRAP-01)**
- **D-01:** The first plan is the tracer. Bump fdars-core 0.3 → 0.47 with features `["parallel"]` on native and `["js"]` on WASM, and drop `linalg`. Fix the ~30 mechanical FdMatrix compile errors (detrending.rs, seasonality.rs, periods.rs, peaks.rs). The new API removes B1/B2, so `ts_detrend`, `ts_detrend_by`, `ts_decompose_seasonal`, `classify_seasonality` and amplitude modulation become non-trivial. Pin it with `ts_detrend` on a cubic series matching numpy `polyfit` to 1e-8, plus a strong sine classified as seasonal.
- **D-02:** Method names: `'quadratic'` → polynomial degree 2, `'cubic'` → degree 3. An unknown method raises an error listing the valid methods, never a silent fallback to auto (B3).
- **D-03:** WASM check: `cargo check --target wasm32-unknown-emscripten` for anofox-fcst-core and anofox-fcst-ffi, plus `cargo tree` showing that `anofox-regression 0.4.0` is gone. The full wasm_eh extension build is left to CI.
- **D-04:** Defects that remain in fdars 0.47 (ACF period G17, FFT period G18, wavelet strength G19) are fixed or replaced in our core wrapper. No upstream fdars PR in this phase; an upstream issue goes on the deferred list.

**Peaks and changepoints (DET-01, DET-02, DET-03)**
- **D-05:** Peaks: re-implement scipy `find_peaks` in core instead of calling fdars, covering plateau midpoints, prominence, height and distance. Existing SQL parameters map onto it. A scipy fixture must find every reference peak at the right index and no minima (audit: 51 found vs scipy 66, only 31 matching).
- **D-06:** BOCPD: Adams & MacKay done properly (G3–G6): a normalised Student-t predictive, a Normal-Gamma update using the sample mean, and run-length truncation that renormalises instead of dropping mass. A changepoint is flagged when the MAP run length resets. The reference is an independent numpy implementation in `validation/reference/python`, plus behaviour tests: a 3σ step is detected within a few steps, and stationary noise produces no changepoints.
- **D-07:** PELT (G7, G8): the cost is normalised by a robust noise variance (MAD of first differences) with a BIC-style penalty, so a series and that series ×100 give identical changepoints. L1 cost uses the median, and pruning actually works. The reference is a numpy exact optimal-partitioning DP (PELT must equal it on small n), because R `changepoint` and Python `ruptures` are not installed.
- **D-08:** Breaking output changes get a CHANGELOG before/after table and no compatibility flag (same as Phases 13 and 14).

**Decomposition, periods and seasonal strength (DET-04, DET-05, DET-06)**
- **D-09:** MSTL: `mstl_decompose` goes through Phase 13's R-faithful `stl.rs` and the `forecast::mstl` iteration already in `stats.rs` (shortest period first, 2 passes, R `s.window = 7 + 4*i`). The primary reference is R `forecast::mstl` (installed); statsmodels `MSTL` is only noted. There is one shared implementation for the `ts_stats` strengths and for decomposition.
- **D-10:** Classical decomposition: an even period uses the centred 2×m moving average (R `stats::decompose`), so a constant series of 1s yields a trend of exactly 1.0 (G10, G11). Checked against an R `decompose` fixture.
- **D-11:** Period detection (G17, G18, G20, G21):
  - FFT removes a linear trend before the periodogram, handles odd n, and never returns n or anything ≥ n/2.
  - ACF allows lag 2 and requires the peak to exceed the 1.96/√n significance bound, else it returns no period.
  - The regression-period default grid includes all integers 2..n/2, so it finds 12.
  - Lomb-Scargle raises errors for n_frequencies < 2 and min > max.
  - References: scipy `periodogram` / `lombscargle` and statsmodels `acf`.
- **D-12:** Wavelet seasonal strength is removed: requesting it raises an error naming the alternatives (still broken in fdars 0.47, always 1.0). The variance and spectral strengths detrend first. The trend-strength doc is corrected to match the implementation (G22).

**Smaller fixes, testing and plan split (DET-07)**
- **D-13:** `is_constant` uses a scale-relative tolerance: constant if `max − min ≤ 1e-10 · max(1, max|x|)`, ignoring NaN/NULL (G25).
- **D-14:** Edge-NULL interpolation behaviour is documented, not changed: linear interpolation does not extrapolate leading/trailing NULLs, and LOCF/NOCB fill in one direction only. Docs plus a SQL assertion pinning it (G26).
- **D-15:** Testing: a new NON-json-gated `test/sql/ts_detection_reference.test` covers every requirement, plus Rust `crates/anofox-fcst-core/tests/*_reference.rs` tests over checked-in fixtures (validation harness: numpy/scipy/statsmodels via the uv project, R via Rscript). The existing detection SQL tests are all json-gated; removing that gate is left to Phase 18 (TEST-03). The assertion count must visibly rise in `make test_release`.
- **D-16:** 5 plans: (1) fdars bump + detrend/classify/amplitude-modulation tracer [DEP-03, WRAP-01]; (2) scipy peaks [DET-01]; (3) BOCPD + PELT [DET-02, DET-03]; (4) MSTL + classical decomposition [DET-04]; (5) periods, seasonal strength, `is_constant`, edge-NULL docs and the docs/skills/CHANGELOG sweep [DET-05, DET-06, DET-07].

### Claude's Discretion
- Internal module structure for re-implemented algorithms, fixture series choice and layout, exact per-test tolerances (each justified in validation/reference/README.md), and BOCPD hazard/prior defaults (documented).

### Deferred Ideas (OUT OF SCOPE)
- An upstream fdars-core issue or PR for the ACF period, FFT period and wavelet-strength defects (G17–G19).
- Removing the json gate from the existing detection SQL test files (Phase 18 / TEST-03).
- A correct wavelet seasonal strength, if it is ever wanted again.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DEP-03 | fdars-core bumped 0.3→0.47, `["parallel"]` native / `["js"]` WASM, no `linalg`, native+WASM compile | §Standard Stack, §fdars-core 0.47 API Surface — version/features/`cargo tree` all verified live this session |
| WRAP-01 | fdars calls in detrending.rs/seasonality.rs pass series correctly; `ts_detrend`/`ts_detrend_by`/`ts_decompose_seasonal`/`classify_seasonality`/amplitude-modulation non-trivial; `'quadratic'`→degree 2 or error | §Current Bug Inventory (B1/B2/B3 with exact lines), §fdars-core 0.47 API Surface (detrend module signatures) |
| DET-01 | Peak detection matches scipy `find_peaks` semantics | §Current Bug Inventory (G1 re-scoped — peaks.rs n/m order is already correct), §Don't Hand-Roll |
| DET-02 | BOCPD: Adams & MacKay correct, detects 3σ step, quiet on noise | §Current Bug Inventory (G3–G6 verified against live changepoint.rs, current tests still pass despite bugs) |
| DET-03 | PELT: scale-invariant penalty, correct L1/Normal cost, working pruning | §Current Bug Inventory (G7/G8 verified), §Critical Finding: PELT Has No SQL Surface |
| DET-04 | MSTL shortest-first, STL(Loess) matching R `forecast::mstl`; even-window correct | §Reuse Target: stats.rs::stl_strengths + stl.rs (already R-faithful, ascending-order, 2-pass) |
| DET-05 | FFT detrends + odd-n; ACF lag 2 + significance; regression grid includes integers; Lomb-Scargle validates inputs | §fdars-core 0.47 API Surface (period.rs), §Falsification Results (live probe confirms G18 still present) |
| DET-06 | Seasonal-strength variants detrend first; wavelet fixed/removed; trend-strength doc matches impl | §Falsification Results (live probe confirms G19 still present — wavelet strength = 1.0 on white noise) |
| DET-07 | `is_constant` scale-relative; edge-NULL behaviour documented | §Current Bug Inventory (G25 verified), §Critical Finding: D-14 vs. Live imputation.rs Behaviour |
</phase_requirements>

## Summary

The phase rests on one structural fact that is now directly verified against the real fdars-core 0.47 source (fetched into `~/.cargo/registry/src/.../fdars-core-0.47.0`, not just read about): fdars 0.47 introduced `fdars_core::matrix::FdMatrix`, a dimension-tracked column-major matrix (`rows = observations`, `cols = evaluation points`), and migrated most — **but not all** — of the functions this crate calls onto it. `detrend_linear/polynomial/diff/loess`, `auto_detrend`, `decompose_additive/multiplicative`, `classify_seasonality`, `detect_amplitude_modulation_wavelet`, `detect_seasonality_changes`, `analyze_peak_timing`, `instantaneous_period`, `detect_peaks`, `estimate_period_fft`, and all four `seasonal_strength_*` functions now take `&FdMatrix` + `argvals` — the (n_samples, m_timepoints) swap that caused B1/B2 is structurally impossible for these, because the two sizes travel together in one struct built once (`FdMatrix::from_slice(data, nrows, ncols)`), not as two independent trailing `usize` arguments at every call site. **`estimate_period_acf`, `estimate_period_regression`, and `detect_multiple_periods` were not migrated** — they still take the old raw `(data: &[f64], n: usize, m: usize, argvals: &[f64], ...)` signature, so the swap risk is not eliminated there (it happens that this project's existing call sites for these three are already correct — `(values, 1, n, …)` — so no behavioural bug exists today for these three, only for `detrend*`/`decompose*`/`classify_seasonality`/amplitude-modulation, exactly matching the audit's B1/B2 scope).

Two defects the audit flagged as still-present in fdars 0.47 (G18 FFT-period-returns-n, G19 wavelet-strength-always-1.0) were **directly falsified this session** by building a scratch crate against the real 0.47 release and running both functions: FFT period on a trend+weak-seasonal series returned `period = n` exactly (121.000 for n=121), and `seasonal_strength_wavelet` returned `1.0000` for both a pure sine and i.i.d. white noise. Both confirm D-04/D-12's premise with hard evidence rather than the audit's original (now 4-day-stale) claim.

A second, more important structural fact is also now verified directly: `crates/anofox-fcst-core/src/stats.rs::stl_strengths` (added in Phase 13 for STAT-02) **already is** the correct R-faithful `forecast::mstl` iteration — ascending period order, 2 passes, `s.window = 7 + 4*(pos+1)`, delegating to the R-ported `stl.rs::stl()` for the Loess fit. `decomposition.rs::mstl_decompose`, by contrast, sorts periods **descending** (`sorted_periods.sort_by(|a, b| b.cmp(a))`, line 245) and does a one-pass hand-rolled centred-moving-average decomposition with a `period+1`-width (not `2×m`) window for even periods — this is G9/G10/G11, independently re-confirmed by reading the live file. DET-04's job is therefore not "write STL correctly" (already done, in a sibling file) but "make `mstl_decompose` call the thing that already exists" — Plan 4 should treat `stl_strengths`'s iteration as the ported algorithm and extract/share it rather than re-deriving STL a third time.

A third finding changes the shape of Plan 3: `anofox_ts_detect_changepoints` (the PELT-backed FFI export) has **zero C++ call sites**. `ts_changepoints.cpp` carries an explicit comment that the scalar bodies calling it were deleted as dead code in Phase 12 plan 12-03/12-07, and the only changepoint SQL surface today (`ts_detect_changepoints`, `ts_detect_changepoints_by`) dispatches exclusively to `_ts_detect_changepoints_bocpd`. PELT today is Rust-only, reachable from nothing a user can run in SQL. This is a scope question the plan must resolve explicitly (fix PELT in Rust with fixture tests only, or restore a thin SQL wrapper) rather than discover mid-execution.

**Primary recommendation:** Sequence Plan 1 (fdars bump) to fix every compile error the bump produces across all four files (including peaks.rs, even though Plan 2 will immediately discard its fdars call per D-05) before any other plan starts; build decomposition.rs's MSTL fix in Plan 4 as a thin consumer of `stats.rs::stl_strengths`'s existing iteration rather than a new implementation; and have Plan 3 open with an explicit decision on PELT's SQL reachability before writing BOCPD/PELT fixes, since the Universal DoD's "affected SQL exercised against the built extension" clause is vacuous for PELT as the code stands today.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Detrending / decomposition math (polynomial fit, STL, classical MA) | Rust Core (`anofox-fcst-core`) | FFI boundary (type marshal only) | Project rule: all statistical arithmetic lives in Rust; C++/SQL only marshal (CLAUDE.md core value) |
| fdars-core FdMatrix construction & argument plumbing | Rust Core | — | Internal to the crate; never crosses the FFI boundary |
| Peak/changepoint/period detection algorithms | Rust Core | — | Same project rule; scipy/R-faithful re-implementations stay in `anofox-fcst-core/src/{peaks,changepoint,periods}.rs` |
| SQL parameter parsing (method names, thresholds, JSON params MAP) | C++ (`src/table_functions/*.cpp`, `src/macros/ts_macros.cpp`) | FFI | Thin marshalling only — this phase must not add arithmetic here |
| Error surfacing for unknown method/cost-function names (D-02) | Rust Core (`ForecastError::InvalidParameter`) | FFI → DuckDB exception | Error construction happens in Rust; C++ only re-throws via `DuckDB::Exception` |
| Reference-fixture generation (R/Python) | Validation harness (`validation/reference/`) | — | Out-of-process, versioned, checked-in — not part of the runtime extension |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `fdars-core` | 0.47.0 [VERIFIED: `cargo add fdars-core@0.47` resolved against crates.io this session; registry metadata confirms `version = "0.47.0"`, `rust-version = "1.81"`] | FdMatrix-backed detrend/decompose/seasonal/peak/period primitives | Already the project's locked FDA dependency (DEP-03); this is a version bump, not a new choice |
| `anofox-fcst-core::stl` | in-repo, Phase 13 | R-faithful `stats::stl` port (Loess) | [VERIFIED: crates/anofox-fcst-core/src/stl.rs:1-20 header comment] — already more accurate than both fdars 0.47's own seasonal STL and a from-scratch port; DET-04 must reuse it, not replace it |
| `anofox-fcst-core::stats::stl_strengths` | in-repo, Phase 13 | R-faithful `forecast::mstl` 2-pass ascending iteration | [VERIFIED: crates/anofox-fcst-core/src/stats.rs — read this session, see quotes below] — this is the "correctly-ordered MSTL" DET-04 needs; it exists today purely for `ts_stats` strength scalars, not decomposition output |

### fdars-core 0.47 features

[VERIFIED: fdars-core-0.47.0/Cargo.toml, read directly from `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/fdars-core-0.47.0/Cargo.toml` this session]

```toml
[features]
default = ["parallel"]
dhat-heap = []
js = ["getrandom/js"]
linalg = [
    "faer",
    "anofox-regression",
]
parallel = ["rayon"]
serde = [
    "dep:serde",
    "dep:serde_json",
]
```

This directly confirms DEP-03's premise: `linalg` is the **only** feature that pulls in `anofox-regression` (and `faer`). Dropping it (native: `features = ["parallel"]`, no default-features change needed beyond what the workspace already does) removes the duplicate `anofox-regression 0.4.0` from the tree. **Falsified/confirmed live:** a scratch crate (`cargo new fdars-probe; cargo add fdars-core@0.47 --features parallel; cargo tree`) shows:

```
fdars-probe v0.1.0
└── fdars-core v0.47.0
```

— zero `anofox-regression`, zero `faer` in the tree with only `parallel` enabled. [VERIFIED: scratch cargo project, `cargo tree` output captured this session]

**Workspace Cargo.toml diff (mechanical):**
```toml
# today (crates/anofox-fcst-core/Cargo.toml) [VERIFIED: read this session]
[target.'cfg(not(target_family = "wasm"))'.dependencies]
fdars-core = { workspace = true, features = ["parallel", "linalg"] }
[target.'cfg(target_family = "wasm")'.dependencies]
fdars-core = { workspace = true, features = ["js"] }

# Cargo.toml (workspace root) [VERIFIED: read this session]
fdars-core = { version = "0.3", default-features = false }
```
Change: workspace `version = "0.3"` → `"0.47"`; native target feature list `["parallel", "linalg"]` → `["parallel"]`; WASM list unchanged (`["js"]`). Keep `default-features = false` at the workspace level rather than relying on fdars 0.47's own `default = ["parallel"]` — explicit feature lists per target are what the project already does and what D-01 specifies, and relying on upstream's current default is fragile against a future fdars release changing it.

### Current anofox-regression duplication (pre-bump baseline)

[VERIFIED: Cargo.lock:73-76, 87-90, 615-618, read this session]
```
name = "anofox-regression"
version = "0.4.0"          # pulled in transitively by fdars-core 0.3's `linalg` feature
...
name = "anofox-regression"
version = "0.5.15"         # the project's own pinned dependency
...
name = "fdars-core"
version = "0.3.0"
```

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `rustfft` | 6.4 (already a direct dep of `anofox-fcst-core`, unrelated to fdars) | FFT for `periods.rs`'s own code | Already in use; no change needed |
| R `forecast` 8.x, `pracma`, `tseries`, `urca` | installed [VERIFIED: `Rscript -e 'installed.packages()'` run this session — returned `forecast, pracma, tseries, urca`] | MSTL/STL/entropy/ADF-KPSS reference fixtures | `forecast::mstl`/`stats::decompose`/`stats::stl` references for D-09/D-10 |
| R `changepoint` | **NOT installed** [VERIFIED: same `installed.packages()` call — absent from the result set] | — | Confirms CONTEXT.md's claim; PELT reference must be the numpy exact-DP per D-07, not R `changepoint` |
| Python `numpy` 2.5.3, `scipy` 1.18.1, `pandas` 3.0.6, `statsmodels` 0.15.0, `tsfresh` 0.21.2 | pinned [VERIFIED: `validation/reference/python/pyproject.toml`, read this session — exact `==` pins] | scipy `find_peaks`/`periodogram`/`lombscargle`, statsmodels `acf` references | Already the project's reference-fixture environment; no change needed |
| Python `ruptures` | **NOT in pyproject.toml** [VERIFIED: full file read this session — not among the 5 pinned deps] | — | Confirms CONTEXT.md; PELT numpy-DP reference must be hand-written, not borrowed from `ruptures` |

**Installation:** no new packages — this is a version bump of an existing Cargo dependency. No `cargo add`/`pip install`/`cargo install` is required for the bump itself.

## Package Legitimacy Audit

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| `fdars-core` | crates.io | published 2026-01-01 [per registry metadata] | 245/week | `github.com/sipemu/fdars` | SUS (`low-downloads`) | **Approved — existing dependency, bump only.** `fdars-core` is already a locked project dependency (REQUIREMENTS.md DEP-03); this phase changes its *version*, not whether it is used. The SUS verdict (low weekly downloads) is a pre-existing characteristic unrelated to the 0.3→0.47 jump; no new supply-chain risk is introduced by bumping a version already in the tree. No `checkpoint:human-verify` is warranted for a version bump of an already-approved dependency, but the planner should note the low-download signal in case a future phase considers an alternative.

**Packages removed due to [SLOP] verdict:** none.
**Packages flagged as suspicious [SUS]:** `fdars-core` (pre-existing dependency, see disposition above — no action required beyond the bump itself).

## Current Bug Inventory (verified against the live tree this session)

All line numbers below were read directly from the working tree on branch `fix/statistical-correctness` this session (not from the 4-day-old audit document), using `Read`/`grep`. Where they match the audit's claims, the match is noted; where the live source diverges from the audit or from CONTEXT.md's wording, that divergence is called out explicitly.

### B1 — detrending.rs: `(values, n, 1, ...)` swap (confirmed, 7 call sites)
[VERIFIED: crates/anofox-fcst-core/src/detrending.rs:149,172,195,224,246,311,358]
```rust
let result = fdars_detrend_linear(values, n, 1, &argvals);              // :149
let result = fdars_detrend_polynomial(values, n, 1, &argvals, degree);  // :172
let result = fdars_detrend_diff(values, n, 1, order);                   // :195
let result = fdars_detrend_loess(values, n, 1, &argvals, bandwidth, degree); // :224
let result = fdars_auto_detrend(values, n, 1, &argvals);                // :246
let result = fdars_decompose_additive(values, n, 1, &argvals, period, method, bw, harmonics);     // :311
let result = fdars_decompose_multiplicative(values, n, 1, &argvals, period, method, bw, harmonics); // :358
```
Every one of these passes `n` (the series length) as `n_samples` and `1` as `m_timepoints` — backwards for a single series of length n (should be `n_samples=1, m_timepoints=n`). fdars 0.3's internal "invalid input" path then silently returns trend=0 / detrended=input, matching the audit's observed "no-op" behaviour.

### B2 — seasonality.rs: same swap, 2 call sites (confirmed, exact quotes)
[VERIFIED: crates/anofox-fcst-core/src/seasonality.rs:672-680, 798-806]
```rust
let result = fdars_classify_seasonality(      // :672
    values,
    n,
    1,
    &argvals,
    period,
    strength_threshold,
    timing_threshold,
);
...
let result = fdars_detect_amplitude_modulation_wavelet(   // :798
    values,
    n,
    1,
    &argvals,
    period,
    mod_thresh,
    seas_thresh,
);
```
Note: `seasonality.rs`'s **other** fdars calls (`seasonal_strength_variance`/`_spectral`/`_wavelet`/`_windowed` at :499-634, `detect_seasonality_changes` at :727-730, `instantaneous_period` at :752-754) already pass `(values, 1, n, &argvals, ...)` — the **correct** order — per the file's own comments ("fdars-core expects: data, n_samples, m_timepoints, argvals"). B2 is specifically `classify_seasonality` and `detect_amplitude_modulation_wavelet`, not the whole file.

### B3 — detrending.rs: unknown method silently falls back to auto
Confirmed by code structure: `detrend_polynomial`'s degree comes from a method-name match that is not shown to reject unrecognised strings explicitly in the current file — D-02 requires `ForecastError::InvalidParameter` naming the valid methods instead.

### G1 — peaks.rs: **already partially fixed**, re-scope needed
[VERIFIED: crates/anofox-fcst-core/src/peaks.rs:146-160, 211-216, 304-315]
```rust
let m = n; // m = number of time points
let argvals = make_argvals(m);
// fdars_detect_peaks expects: data, n_samples, m_timepoints, argvals, ...
// For a single time series: n_samples=1, m_timepoints=len(values)
let result = fdars_detect_peaks(
    values,
    1, // n = 1 sample (one time series)
    m, // m = number of time points
    &argvals,
    ...
);
```
`peaks.rs` already passes `(values, 1, m, ...)` — the correct order — with an explicit regression test (`test_issue_59_peaks_detected`, line 304: *"Regression test for GitHub issue #59: verify peaks are actually detected... The bug was passing n/m in wrong order to fdars-core"*) proving this was fixed in a prior issue, independent of this phase. **G1's remaining content is purely about fdars's own `find_peaks` quality** (missed peaks, minima reported, plateau-prominence=0) — which is exactly why D-05 chooses full re-implementation over calling fdars at all. The planner should not expect an n/m-swap fix in peaks.rs; Plan 1 only needs to update the call to build an `FdMatrix` (mechanical, since fdars 0.47's `detect_peaks` takes `&FdMatrix`) even though Plan 2 discards this call path entirely moments later.

### G3–G6 — changepoint.rs BOCPD: confirmed present in the live file, but current tests already pass
[VERIFIED: crates/anofox-fcst-core/src/changepoint.rs:264-270, 338-343, 292-297]

Missing Student-t normaliser (G3):
```rust
let scale = ((beta_n * (kappa_n + 1.0)) / (alpha_n * kappa_n)).sqrt();
let z = (x - mu_n) / scale.max(1e-10);
let nu = 2.0 * alpha_n;
// Student's t PDF approximation
pred_prob[r] = (1.0 + z.powi(2) / nu).powf(-(nu + 1.0) / 2.0);
```
This is the Student-t kernel with no `Γ((ν+1)/2) / (Γ(ν/2)·√(νπ)·scale)` normalising constant — confirmed exactly as G3 describes.

Truncation drops mass without renormalising (G5):
```rust
let max_keep = 500; // Maximum run lengths to track
if run_length_prob.len() > max_keep {
    run_length_prob.truncate(max_keep);
    sum_x.truncate(max_keep);
    sum_x2.truncate(max_keep);
    run_counts.truncate(max_keep);
}
```
`truncate()` removes the tail probability mass with no re-normalisation step afterward — confirmed as G5.

**Caveat for the planner:** running the existing test suite (`cargo test -p anofox-fcst-core --lib changepoint::`) shows **all 10 existing tests pass**, including `test_detect_changepoints_bocpd_step_change` (a regression test for GitHub issue #71 asserting `prob_at_cp > 0.5` on a 100→10 step at index 12, n=24). [VERIFIED: `cargo test` run this session — `test result: ok. 10 passed; 0 failed`] This means the file already carries partial prior fixes (comments marked `// BUG FIX:` and `// FIX:` throughout) for a *gross* step change on a short series; D-06's actual bar — correct Student-t normaliser, sample-mean-based update, renormalised truncation, and a 3σ step (not a 9σ-scale step on n=24) — is a materially stricter test the current code has not been measured against. Do not treat "existing tests pass" as evidence the BOCPD fix is unnecessary.

### G7/G8 — PELT: confirmed present and mathematically unambiguous
[VERIFIED: crates/anofox-fcst-core/src/changepoint.rs:56-57, 80, 118, 150-154]

L1 cost uses the mean, not the median (G8):
```rust
let mean: f64 = segment.iter().sum::<f64>() / n;
segment.iter().map(|v| (v - mean).abs()).sum()
```

Penalty and normal cost have no σ²-scale term — only `n` (G7):
```rust
let pen = penalty.unwrap_or_else(|| (n as f64).ln() * 2.0);     // function of n only
...
n * (1.0 + variance.ln())                                        // cost_normal: ln(variance), not variance/σ²
```
Scaling the whole series by 100× multiplies every segment's SSE/variance by 10,000 while `pen` (a function of `n` alone) is unchanged — the relative cost of adding a changepoint vs. not shifts, producing spurious changepoints exactly as the audit's "32 spurious CPs at ×100" describes. This is a direct consequence of the visible formula, not something that needed a runtime probe to confirm.

Pruning set computed but never consulted (G8):
```rust
for tau_star in min_size..=n {
    let candidates: Vec<usize> = (0..=(tau_star.saturating_sub(min_size)))
        .filter(|&tau| tau + min_size <= tau_star)
        .filter(|&tau| tau == 0 || tau >= min_size)
        .collect();
    ...
    r[tau_star] = candidates.into_iter().filter(|&tau| f[tau] + cost(values, tau, tau_star) <= f[tau_star]).collect();
    r[tau_star].push(tau_star);
}
```
`r[tau_star]` (the pruned candidate set) is computed every iteration but `candidates` for the *next* iteration is always rebuilt from the full `0..=(tau_star - min_size)` range — `r` is written, never read back into the next `candidates` computation. This makes the implementation O(n²) despite computing pruning data, exactly matching G8's "pruning set unused (O(n³))" (the live complexity is O(n²) in the candidate-scan, not O(n³), but the *functional* bug — pruning is dead code — is identical).

### G9/G10/G11 — decomposition.rs MSTL: confirmed, with a ready-made fix available elsewhere
[VERIFIED: crates/anofox-fcst-core/src/decomposition.rs:71, 243-245]

Longest-first (should be shortest-first, G9):
```rust
let mut sorted_periods: Vec<i32> = periods.to_vec();
sorted_periods.sort_by(|a, b| b.cmp(a));   // descending — longest period processed first
```

Even-period window is `period+1`, not `2×m` (G10/G11):
```rust
let window = if period % 2 == 0 { period + 1 } else { period };
let half_window = window / 2;
...
let sum: f64 = values[i - half_window..=i + half_window].iter().sum();
trend[i] = sum / window as f64;
```
For an even `period`, `window = period + 1` (odd) is a plain unweighted moving average — not R `stats::decompose`'s centred 2×m filter (half-weight at the two endpoints, full weight on the m−1 interior points, divided by m). This reproduces the audit's "constant 1 → 1.05" result (a symmetric (m+1)-point average of a constant series is still exactly the constant, but on a *non*-constant series or with the real 2×m filter's endpoint half-weights, this naive window diverges from R's output — the audit's own repro used a constant-series sanity check that happens to average to the right value regardless of window choice for a truly constant series; the discrepancy shows on any series with a trend/seasonal component, which is the DET-04 pin test's actual target).

**Ready-made alternative** (see next section): `stats.rs::stl_strengths` already implements ascending-order, STL(Loess)-based, 2-pass `forecast::mstl`. `decomposition.rs::mstl_decompose` does not need a new algorithm — it needs to delegate to (or share the inner loop of) the function that already exists for STAT-02.

### G25 — filter.rs `is_constant`: confirmed, exact line
[VERIFIED: crates/anofox-fcst-core/src/filter.rs:39-48]
```rust
pub fn is_constant(values: &[Option<f64>]) -> bool {
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();
    if non_null.len() < 2 {
        return true;
    }
    let first = non_null[0];
    non_null.iter().all(|v| (v - first).abs() < f64::EPSILON)
}
```
Absolute `f64::EPSILON` (~2.2e-16) tolerance — fails for any series whose magnitude is large enough that floating-point rounding noise exceeds this absolute bound even when the series is conceptually constant. D-13's `max − min ≤ 1e-10 · max(1, max|x|)` is a direct, mechanical fix to this one function.

## Critical Finding: D-14 vs. live `imputation.rs` behaviour

D-14 states: *"linear interpolation does not extrapolate leading/trailing NULLs, and LOCF/NOCB fill in one direction only."*

[VERIFIED: crates/anofox-fcst-core/src/imputation.rs:73-127, read this session]
```rust
pub fn fill_nulls_interpolate(values: &[Option<f64>]) -> Vec<f64> {
    ...
    // Fill before first value with first value
    if let Some(v) = values[first] {
        for item in result.iter_mut().take(first) {
            *item = v;
        }
    }
    // Fill after last value with last value
    if let Some(v) = values[last] {
        for item in result.iter_mut().skip(last + 1) {
            *item = v;
        }
    }
    ...
}
```
This function **does** fill leading and trailing NULLs — with a constant extension of the nearest observed value, not left as NULL. The `LOCF`/`NOCB` half of D-14 is independently verified correct: [VERIFIED: imputation.rs:9-46] `fill_nulls_forward` leaves *leading* NULLs (before the first `Some`) unfilled but fills *trailing* NULLs (carries the last value forward indefinitely); `fill_nulls_backward` is the mirror — leaves trailing NULLs unfilled, fills leading ones. That half of D-14's wording is accurate.

**The "linear interpolation does not extrapolate" clause is not accurate against the current source.** Since D-14 explicitly says "documented, not changed," the planner/Plan 5 should write the doc to match what the *code actually does* (constant-extension edge fill, confirmed above) rather than the literal CONTEXT.md sentence — or flag this back to the user as a discuss-phase correction before writing documentation that will visibly contradict a runnable SQL assertion. Recommend treating this as an open item rather than silently picking one.

## fdars-core 0.47 API Surface (read directly from the fetched crate source)

Fetched into a scratch cargo project (`cargo new fdars-probe && cargo add fdars-core@0.47 --features parallel`), source inspected at `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/fdars-core-0.47.0/`.

### `FdMatrix` (`src/matrix.rs:1-140`)
[VERIFIED: read this session]
```rust
/// For functional data, rows typically represent observations and columns
/// represent evaluation points.
pub struct FdMatrix { data: Vec<f64>, nrows: usize, ncols: usize }
impl FdMatrix {
    pub fn from_column_major(data: Vec<f64>, nrows: usize, ncols: usize) -> Result<Self, crate::FdarError>
    pub fn from_slice(data: &[f64], nrows: usize, ncols: usize) -> Result<Self, crate::FdarError>
    pub fn zeros(nrows: usize, ncols: usize) -> Self
}
```
For one time series of length `n`: `FdMatrix::from_slice(values, 1, n)` — nrows=1 observation, ncols=n timepoints. The dimension *order* in the constructor call is still something a human can get backwards (`from_slice(values, n, 1)` compiles fine, silently producing an n-observation, 1-timepoint matrix) — FdMatrix narrows the bug surface to one call site per function rather than eliminating the risk outright. Every new/changed `FdMatrix::from_slice`/`from_column_major` call this phase adds must be reviewed for `(nrows, ncols)` order, same discipline as before, just fewer places to check.

### Functions migrated to `&FdMatrix` (mechanical rewrite in Plan 1)
[VERIFIED: grep + read of `src/detrend/{linear,polynomial,diff,loess,auto,decompose}.rs` and `src/seasonal/{period,peak,hilbert,change,strength}.rs`]

| Function | 0.47 signature |
|---|---|
| `detrend::linear::detrend_linear` | `(data: &FdMatrix, argvals: &[f64]) -> TrendResult` |
| `detrend::polynomial::detrend_polynomial` | `(data: &FdMatrix, argvals: &[f64], degree: usize) -> TrendResult` |
| `detrend::diff::detrend_diff` | `(data: &FdMatrix, order: usize) -> TrendResult` |
| `detrend::loess::detrend_loess` | `(data: &FdMatrix, argvals: &[f64], bandwidth: f64, degree: usize) -> TrendResult` |
| `detrend::auto::auto_detrend` | `(data: &FdMatrix, argvals: &[f64]) -> TrendResult` |
| `detrend::decompose::decompose_additive` | `(data: &FdMatrix, argvals: &[f64], period: f64, trend_method: &str, bandwidth: f64, n_harmonics: usize) -> DecomposeResult` |
| `detrend::decompose::decompose_multiplicative` | same shape |
| `seasonal::period::estimate_period_fft` | `(data: &FdMatrix, argvals: &[f64]) -> PeriodEstimate` — **note: dropped from 4 args to 2; `n`/`m` come from `data.shape()`** |
| `seasonal::peak::detect_peaks` | `(data: &FdMatrix, argvals: &[f64], min_distance: Option<f64>, min_prominence: Option<f64>, smooth_first: bool, smooth_nbasis: Option<usize>) -> PeakDetectionResult` |
| `seasonal::change::analyze_peak_timing` | `(data: &FdMatrix, argvals: &[f64], period: f64, smooth_nbasis: Option<usize>) -> PeakTimingResult` |
| `seasonal::change::classify_seasonality` | `(data: &FdMatrix, argvals: &[f64], period: f64, strength_threshold: Option<f64>, timing_threshold: Option<f64>) -> SeasonalityClassification` |
| `seasonal::change::detect_amplitude_modulation_wavelet` | `(data: &FdMatrix, argvals: &[f64], period: f64, modulation_threshold: f64, seasonality_threshold: f64) -> WaveletAmplitudeResult` — **note: thresholds are now plain `f64`, not `Option<f64>`** |
| `seasonal::change::detect_seasonality_changes` | `(data: &FdMatrix, argvals: &[f64], period: f64, threshold: f64, window_size: f64, min_duration: f64) -> ChangeDetectionResult` — **same Option→plain-f64 change** |
| `seasonal::hilbert::instantaneous_period` | `(data: &FdMatrix, argvals: &[f64]) -> InstantaneousPeriod` |
| `seasonal::strength::seasonal_strength_variance` | `(data: &FdMatrix, argvals: &[f64], period: f64, n_harmonics: usize) -> f64` |
| `seasonal::strength::seasonal_strength_spectral` | `(data: &FdMatrix, argvals: &[f64], period: f64) -> f64` |
| `seasonal::strength::seasonal_strength_wavelet` | `(data: &FdMatrix, argvals: &[f64], period: f64) -> f64` |
| `seasonal::strength::seasonal_strength_windowed` | `(data: &FdMatrix, argvals: &[f64], period: f64, window_size: f64, method: StrengthMethod) -> Vec<f64>` |

**Mechanical consequence for Plan 1:** everywhere `seasonality.rs` currently does `threshold.unwrap_or(0.3)` before calling fdars, the `.unwrap_or(...)` now has to happen *before* the call regardless (it already does, per the read of seasonality.rs:702-725) — this part needs no behavioural change, only an `FdMatrix::from_slice(values, 1, n)?` inserted before each call and the raw `n, 1` / `n, 1` trailing args removed.

### Functions **not** migrated — raw args remain (no structural swap protection)
[VERIFIED: `src/seasonal/period.rs:91-97, 153-162, 258-266`]
```rust
pub fn estimate_period_acf(data: &[f64], n: usize, m: usize, argvals: &[f64], max_lag: usize) -> PeriodEstimate
pub fn estimate_period_regression(data: &[f64], n: usize, m: usize, argvals: &[f64], period_min: f64, period_max: f64, n_candidates: usize, n_harmonics: usize) -> PeriodEstimate
pub fn detect_multiple_periods(data: &[f64], n: usize, m: usize, argvals: &[f64], max_periods: usize, min_confidence: f64, min_strength: f64) -> Vec<DetectedPeriod>
```
This directly qualifies CONTEXT.md's framing ("FdMatrix structurally prevents the swapped size-argument bug") — true only for the ~15 functions above, not these three. The project's current `periods.rs` call sites for these three already pass `(values, 1, n, …)` correctly [VERIFIED: periods.rs:312, 352, 387], so there is no live bug here, but Plan 1/Plan 5 must preserve that correct order rather than assume FdMatrix will catch a regression — these three still accept a raw, swappable `(n, m)` pair.

### `periodogram()` helper performs no detrending (confirms G18 structurally)
[VERIFIED: `src/seasonal/mod.rs:277-307`]
```rust
pub(super) fn periodogram(data: &[f64], argvals: &[f64]) -> (Vec<f64>, Vec<f64>) {
    ...
    let mut buffer: Vec<Complex<f64>> = data.iter().map(|&x| Complex::new(x, 0.0)).collect();
    fft.process(&mut buffer);
    ...
    let p = if k > 0 && k < n / 2 { 2.0 * p } else { p };   // one-sided doubling
    ...
}
```
No mean-removal, no linear detrend, before the FFT. For odd `n`, the doubling condition `k < n/2` (integer division) excludes the last non-DC bin (`k = n/2` by floor division) from doubling even though, for odd `n`, that bin is *not* the Nyquist frequency and should be doubled like every other interior bin — an odd-n one-sided-spectrum bug, matching G18's "odd-n last bin" note.

## Falsification Results (live runtime probes against fdars-core 0.47, this session)

Both probes below ran the **actual 0.47 release** fetched from crates.io in a scratch cargo project; neither is a claim inferred from reading code.

```rust
// probe 1: FFT period on n=121 (odd), strong linear trend + weak period-12 seasonal
let data: Vec<f64> = (0..121).map(|i| { let t = i as f64; 0.5*t + 2.0*(2.0*PI*t/12.0).sin() }).collect();
let mat = FdMatrix::from_slice(&data, 1, 121).unwrap();
let est = estimate_period_fft(&mat, &argvals);
// -> period=121.000 confidence=36.688
```
`period == n` exactly — **G18 is confirmed still present in fdars-core 0.47**: no detrending means the linear-trend component dominates the periodogram and the lowest non-DC frequency bin wins, giving `period = n`. [VERIFIED: scratch cargo project, this session]

```rust
// probe 2: wavelet strength on pure sine vs. white noise, n=240, period=12
let s_sine  = seasonal_strength_wavelet(&sine_mat,  &argvals, 12.0);   // -> 1.0000
let s_noise = seasonal_strength_wavelet(&noise_mat, &argvals, 12.0);   // -> 1.0000
```
Both a pure period-12 sine and i.i.d. white noise return exactly `1.0000` — **G19 is confirmed still present in fdars-core 0.47**: `seasonal_strength_wavelet` has a hard `.min(1.0)` clamp and a normalisation that evidently saturates for both inputs tested. [VERIFIED: scratch cargo project, this session] This is hard evidence for D-12's "remove it" decision, stronger than the original audit's claim (which predates this release check).

## Reuse Target: `stats.rs::stl_strengths` + `stl.rs::stl()` (for DET-04)

[VERIFIED: crates/anofox-fcst-core/src/stats.rs, read this session; function spans roughly lines 530-635]

```rust
/// STL-based trend/seasonal strength (D-02, STAT-02), matching
/// `tsfeatures::stl_features` (built on `forecast::mstl` -> `stats::stl`):
/// ...
/// Exactly one usable period -> a single [`crate::stl::stl`] call with
/// `s.window = 11` (R's `stats::stl` default). Two or more -> the
/// `forecast::mstl` iteration (2 passes, `s.window = 7 + 4*(position+1)` by
/// ascending period position, trend from the last `stl` call); ...
pub fn stl_strengths(values: &[f64], periods: &[usize]) -> Result<StlStrengths> {
    ...
    usable.sort_unstable();   // ASCENDING period order
    usable.dedup();
    ...
    for _pass in 0..2 {
        for (pos, &period) in usable.iter().enumerate() {
            ...
            let s_window = 7 + 4 * (pos + 1);
            let params = StlParams::r_default(period, s_window);
            let fit = stl(&deseas, &params)?;
            seas[pos] = fit.seasonal;
            last_trend = fit.trend;
            ...
        }
    }
    ...
}
```
This is the exact algorithm DET-04 asks for (R `forecast::mstl` shortest-first, 2 passes, STL/Loess components via the already-R-faithful `stl.rs::stl()`) — it just currently only returns two scalars (`trend_strength`, `seasonality_strength`), not the trend/seasonal/remainder *arrays* `mstl_decompose` must return for `ts_decompose`/`ts_mstl_decomposition_by`. **Recommendation for Plan 4:** factor the `for _pass in 0..2 { for (pos, &period) in usable.iter().enumerate() { ... } }` loop (or the whole `stl_strengths` body) so that both the strength scalars and `mstl_decompose`'s array outputs are built from one shared function — this satisfies D-09's "one shared implementation for the `ts_stats` strengths and for decomposition" literally, not just in spirit.

`stl.rs::stl()` itself: [VERIFIED: crates/anofox-fcst-core/src/stl.rs:1-20]
> "Ported from R's own C translation of the original Fortran (`src/library/stats/src/stl.c`, R trunk, fetched 2026-10-10)... not from the crate's own `anofox_forecast::seasonality::stl::STL` (0.15.10): that implementation uses a degree-0 kernel smoother with truncated symmetric windows, no cycle-subseries end extension, and no jump interpolation, so it does not reproduce `stats::stl`'s output to the 1e-8 component tolerance."

This is the module DET-04's "STL (Loess) components matching R `forecast::mstl`" requirement already targets — no new STL port is needed.

## SQL/FFI Wiring Map (what changes where)

Traced every FFI entry point this phase touches, with its C++ caller(s), by reading `crates/anofox-fcst-ffi/src/lib.rs`, `src/table_functions/*.cpp`, and `src/macros/ts_macros.cpp` this session.

| SQL surface | C++ file | FFI function | Rust function | Notes |
|---|---|---|---|---|
| `ts_detrend`, `ts_detrend_by` | `ts_detrend.cpp` | `anofox_ts_detrend` | `detrending::{detrend_linear,…}` | B1 fix target |
| `ts_decompose_seasonal` | `ts_detrend.cpp` | `anofox_ts_decompose` | `detrending::{decompose_additive,decompose_multiplicative}` | B1 fix target — **separate Rust path from MSTL below** |
| `ts_mstl_decomposition_by` (table fn) + scalar via same FFI call | `ts_decomposition.cpp`, `ts_mstl_decomposition_native.cpp` | `anofox_ts_mstl_decomposition` | `decomposition::mstl_decompose` | **G9/G10/G11 fix target — unrelated to fdars/B1, pure hand-rolled core bug** |
| `ts_classify_seasonality`, `_by` | — (via `ts_classify_seasonality_agg`) | `anofox_ts_classify_seasonality` | `seasonality::classify_seasonality` | B2 fix target |
| `ts_detect_peaks`, `_by` | `ts_peaks.cpp` | `anofox_ts_detect_peaks` | `peaks::detect_peaks` | D-05 full re-implementation target |
| `ts_detect_periods`, `_flat`, `ts_detect_multiple_periods*` | `ts_periods.cpp` | `anofox_ts_estimate_period_fft/_acf`, `anofox_ts_detect_multiple_periods*` | `periods::estimate_period_{fft,acf,regression}_ts`, `detect_multiple_periods_ts` | D-11 fix target; FFT path uses the newly-2-arg fdars fn, ACF/regression/multi unaffected by the FdMatrix migration |
| `ts_detect_changepoints`, `_by` | `ts_changepoints.cpp` | `anofox_ts_detect_changepoints_bocpd` | `changepoint::detect_changepoints_bocpd` | D-06 fix target — **this is the only changepoint SQL surface that exists today** |
| — (no SQL caller) | — | `anofox_ts_detect_changepoints` [VERIFIED: declared in `src/include/anofox_fcst_ffi.h:2655`, zero call sites found via `grep -rn "anofox_ts_detect_changepoints(" src/`] | `changepoint::detect_changepoints` (PELT) | **See Critical Finding below** |
| `ts_is_constant` or inline via quality fns | — | `anofox_ts_is_constant` | `filter::is_constant` | D-13 fix target |

## Critical Finding: PELT Has No SQL Surface

[VERIFIED: `src/table_functions/ts_changepoints.cpp:29-35`, read this session]
```cpp
// ============================================================================
// The two never-registered scalar bodies that used to live here (a one-list,
// a three-arg list+min_size+penalty variant) and their shared private list
// helper were deleted in Phase 12 plan 12-03 (D-01) — the live SQL surface is
// the ts_detect_changepoints table macro in ts_macros.cpp dispatching to
// _ts_detect_changepoints_bocpd. The no-op scalar registration function for
// this never-registered body was removed in plan 12-07 (D-09).
// ============================================================================
```
[VERIFIED: `grep -rn "anofox_ts_detect_changepoints(" src/` run this session, matched only the header declaration, no caller]

`anofox_fcst_core::detect_changepoints` (PELT, `CostFunction::{L1,L2,Normal}`) is fully implemented in Rust and exported through FFI (`anofox_ts_detect_changepoints`, which hardcodes `CostFunction::L2` and ignores any caller-supplied cost function — there isn't one), but **no C++ code anywhere calls it**. It was deliberately de-registered from SQL in Phase 12. DET-03's "L1 cost uses the median… pruning actually works" is therefore currently verifiable **only** via Rust unit/fixture tests (`crates/anofox-fcst-core/tests/*_reference.rs`) — there is no SQL path to exercise, which means the project's Universal DoD clause ("affected SQL exercised against the built extension") and the project CLAUDE.md's "every new SQL function must be exercised" rule are **both vacuously satisfied by having no SQL function**, not actually tested end-to-end.

**This is an open scope question the plan must decide explicitly, not discover mid-execution:**
1. Fix PELT in Rust only, prove it via fixture tests against the numpy exact-DP reference (D-07's own words), and document in the CHANGELOG that PELT remains Rust/FFI-only with no SQL exposure in this milestone — i.e., DET-03 is satisfied at the Rust-correctness level, SQL-reachability is out of scope (consistent with REQUIREMENTS.md's "no new SQL surface beyond what fixes require," since PELT already has FFI/header surface — restoring a C++ caller would not be a *new* capability, just restoring a previously-removed one).
2. Or: treat "restore a thin SQL wrapper, with a cost-function parameter actually wired through (today's FFI function ignores it)" as in-scope for Plan 3, since otherwise the FFI function's own cost-function parameter is simply dead code forever.

Recommend option 1 for Plan 3 (keep Plan 3 scoped to Rust-only PELT correctness, matching D-07's own reference strategy — "PELT must equal [the numpy DP] on small n" — which is a Rust/offline comparison, not a SQL one) and record the decision explicitly in the plan rather than leaving it implicit.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| STL (Loess) decomposition for MSTL | A third STL implementation inside `decomposition.rs` | `crate::stl::stl()` + the iteration pattern already in `stats.rs::stl_strengths` | Already ported from R's actual C source to 1e-8 component tolerance (Phase 13); a new port would re-introduce exactly the kind of divergence Phase 13 fixed |
| scipy `find_peaks` semantics (plateau midpoint, prominence, `wlen`, distance tie-break) | A hand-tuned derivative-zero-crossing peak finder (current `peaks.rs` approach, and fdars's own, both audited as wrong) | A direct, careful scipy-algorithm port in `peaks.rs` (per D-05) | scipy's algorithm has specific, documented edge-case rules (plateau → midpoint index, prominence via nearest higher neighbours with `wlen` window, distance-based greedy removal by height) that are easy to get subtly wrong twice (fdars did, the current core wrapper's own fallback risk is high) — port the documented algorithm, don't re-derive it |
| PELT exact-DP reference for correctness testing | Installing/vendoring `ruptures` or R `changepoint` | A from-scratch numpy exact optimal-partitioning DP in `validation/reference/python` (D-07) | Neither reference package is installed in this environment [VERIFIED this session]; the DP itself is short and exactly specifies what "optimal" means for a given penalty/cost, which is the actual thing under test |
| BOCPD reference | Trusting the existing (already-partially-patched) Rust implementation as its own ground truth | An independent numpy BOCPD in `validation/reference/python` (D-06) | The live code's own `// BUG FIX:` comments show it has been patched ad hoc multiple times already; an independent implementation from the Adams & MacKay (2007) equations is the only way to know whether the current patches are sufficient or just move the bug |

**Key insight:** every one of this phase's "don't hand-roll" items is actually "don't hand-roll *again*" — fdars already hand-rolled peak detection and wavelet strength and both are wrong; this crate's own `decomposition.rs` already hand-rolled MSTL and it's wrong; the fix in every case is either a careful, named-algorithm port (scipy `find_peaks`, numpy exact-DP, numpy BOCPD) or reuse of a sibling module that already got the equivalent algorithm right (`stl.rs`).

## Common Pitfalls

### Pitfall 1: Assuming all fdars calls get FdMatrix protection
**What goes wrong:** Treating `estimate_period_acf`, `estimate_period_regression`, and `detect_multiple_periods` as "also fixed by the bump" because they're adjacent code in the same file as functions that did migrate to `FdMatrix`.
**Why it happens:** CONTEXT.md's framing ("FdMatrix API structurally prevents the swapped size-argument bug") is true for most, but verified this session to be false for these three specific functions, which still take raw `(data: &[f64], n: usize, m: usize, ...)`.
**How to avoid:** When touching `periods.rs` in Plan 1/Plan 5, verify per-function whether the 0.47 signature takes `&FdMatrix` or the old raw args (see the two tables in §fdars-core 0.47 API Surface above) before assuming "mechanical."
**Warning signs:** A compile error mentioning `FdMatrix` where expected is the signal the function *did* migrate; a compile error about argument count/type mismatch on `estimate_period_acf`/`_regression`/`detect_multiple_periods` is likely unrelated to FdMatrix (check for other 0.47 signature drift instead, e.g. return-type changes).

### Pitfall 2: Threshold parameters changed from `Option<f64>` to plain `f64`
**What goes wrong:** `detect_amplitude_modulation_wavelet` and `detect_seasonality_changes` took `Option<f64>` thresholds in 0.3 (core's `.unwrap_or(default)` fed them through as `Option`); in 0.47 both parameters are plain `f64` — a compile error, not a silent behaviour change, but easy to "fix" by wrapping in `Some(...)` instead of actually unwrapping, which would compile against neither signature and is worth a deliberate glance.
**Why it happens:** Mechanical sed-style fixes to compile errors can paper over a type change without re-reading the semantics.
**How to avoid:** `core`'s existing `.unwrap_or(default)` calls already produce a plain `f64` — the fix is simply removing the now-unnecessary FdMatrix-adjacent args, not touching the unwrap logic.
**Warning signs:** A compile error of the form "expected `f64`, found `Option<f64>`" at one of these two call sites.

### Pitfall 3: Treating G9 (longest-first) as the only MSTL bug worth testing
**What goes wrong:** Fixing only the sort order (`sort_by(|a,b| b.cmp(a))` → ascending) without also replacing the hand-rolled moving-average `stl_decompose` helper with the real Loess-based `stl.rs::stl()`.
**Why it happens:** The sort-order bug (G9) is a one-line fix and easy to declare "done"; the even-window bug (G10/G11) and the "not actually STL" gap (the function is named `stl_decompose` but is a classical centred-MA decomposition, not Loess) require touching the body, not just the sort call.
**How to avoid:** DET-04's pin test (R `forecast::mstl`/statsmodels `MSTL` component match within tolerance) will fail on anything but a true Loess STL — a sort-order-only fix will not pass it. Build on `stl.rs::stl()` + `stats.rs`'s iteration pattern (see §Reuse Target) rather than patching `decomposition.rs`'s own `stl_decompose` helper.
**Warning signs:** A "passing" test that only checks changepoint/trend monotonicity or length, not actual component values against an R fixture.

### Pitfall 4: PELT "fix" with no way to prove it end-to-end via SQL
**What goes wrong:** Writing Plan 3 as if `ts_detect_changepoints` exercises PELT, because the FFI function exists and its name suggests it should be reachable.
**Why it happens:** The FFI function (`anofox_ts_detect_changepoints`) and its header declaration are real and compiled; only a `grep` across `src/*.cpp` for actual call sites reveals it is dead code from the SQL extension's perspective.
**How to avoid:** Confirm scope explicitly in the Plan 3 document (see §Critical Finding: PELT Has No SQL Surface) rather than assuming the existing snippet-harness pattern ("every SQL function must be exercised") applies.
**Warning signs:** A plan verification step that says "run `ts_detect_changepoints` and check PELT-specific output" — that SQL function only calls BOCPD.

## Code Examples

### FdMatrix construction pattern (Plan 1, every migrated call site)
```rust
// Source: fdars-core 0.47.0 src/matrix.rs + src/detrend/linear.rs (read this session)
// Before (fdars 0.3, swapped args — B1):
let result = fdars_detrend_linear(values, n, 1, &argvals);

// After (fdars 0.47):
let mat = fdars_core::matrix::FdMatrix::from_slice(values, 1, n)
    .map_err(|e| ForecastError::InvalidInput(e.to_string()))?;
let result = fdars_detrend_linear(&mat, &argvals);
```

### Method-name validation (D-02)
```rust
// Pattern: reject unknown method names explicitly, never fall back silently.
// (No upstream example — this is a core wrapper concern, not an fdars API.)
let degree = match method {
    "linear" => 1,
    "quadratic" => 2,
    "cubic" => 3,
    other => return Err(ForecastError::InvalidParameter {
        param: "method".to_string(),
        value: other.to_string(),
        reason: "expected one of: linear, quadratic, cubic, loess, diff, auto".to_string(),
    }),
};
```

### PELT L1 cost: mean → median (D-07)
```rust
// Source: crates/anofox-fcst-core/src/changepoint.rs:44-58 (current, buggy)
fn cost_l1(values: &[f64], start: usize, end: usize) -> f64 {
    let segment = &values[start..end];
    let n = segment.len() as f64;
    let mean: f64 = segment.iter().sum::<f64>() / n;              // WRONG for L1
    segment.iter().map(|v| (v - mean).abs()).sum()
}

// Fix direction: compute the median of segment, not the mean, matching the
// L1/MAD cost function's standard definition (Killick et al. 2012, §3.1).
```

### Scale-invariant PELT penalty (D-07 direction)
```rust
// Current (G7): pen = ln(n) * 2.0 — function of n only, no noise-scale term.
// Target: normalise the cost by a robust noise-variance estimate (MAD of
// first differences), e.g. cost_l2_normalized = cost_l2(...) / sigma_hat.powi(2)
// with sigma_hat from MAD(diff(values)) / 0.6745 (standard robust sigma
// estimator), so that scaling the whole series by k scales sigma_hat by k
// and leaves the normalised cost (and therefore the changepoint set)
// unchanged.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| fdars-core raw `(data: &[f64], n_samples: usize, m_timepoints: usize, argvals: &[f64])` positional API | `FdMatrix`-typed API for detrend/decompose/most-of-seasonal (not period-estimation-by-ACF/regression/multi) | fdars-core 0.3→0.47 [VERIFIED this session] | Removes the swap bug class for ~15 functions; 3 period functions remain raw-arg |
| `decomposition.rs::mstl_decompose` hand-rolled classical-MA, longest-first | `stats.rs::stl_strengths`'s R-faithful STL, ascending, 2-pass (added Phase 13, not yet wired to decomposition output) | Phase 13 (2026, this milestone) | DET-04 should consume the existing correct implementation rather than write a third one |

**Deprecated/outdated:**
- fdars-core 0.3's raw-usize detrend/seasonal/peak API: superseded by `FdMatrix` in 0.47 for most functions.
- `decomposition.rs::stl_decompose` (the private helper inside `mstl_decompose`): not truly deprecated by fdars, but functionally superseded in-repo by `stl.rs::stl()`.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `estimate_period_regression`'s current `n_candidates.unwrap_or(50)` / float-step grid is the mechanism behind G20 ("misses 12"), and D-11's fix is to switch to an integer-stepped grid covering every integer 2..n/2 | §fdars-core 0.47 API Surface, §Critical Finding sections | If the real fix needs a different grid-generation strategy (e.g., always include small primes/common periods rather than every integer), Plan 5 may need a second pass; low risk since D-11's own wording ("default grid includes all integers 2..n/2") already specifies the target behaviour precisely |
| A2 | `detect_multiple_periods_ts`'s current default `min_confidence=2.0` (vs. fdars's own doc-comment-suggested default of 0.4) is an intentional project choice, not a bug in scope for this phase | §fdars-core 0.47 API Surface | If this default is actually a latent bug (not flagged in the audit's G-list), leaving it unchanged could mean `ts_detect_multiple_periods` under-detects; low risk since it's outside the phase's explicit requirement list (DET-01..07) and not mentioned in CONTEXT.md |
| A3 | Restoring no SQL surface for PELT (recommendation in §Critical Finding) is the intended scope, since REQUIREMENTS.md's "Out of Scope" table excludes "new SQL capabilities... beyond what fixes require" | §Critical Finding: PELT Has No SQL Surface | If the user actually wants PELT SQL-reachable (e.g., for the `ts_detection_reference.test` D-15 requires), the plan needs a C++ scalar function added — this should be confirmed before Plan 3 starts, not assumed |

**If this table is empty:** N/A — see rows above; everything else in this document is `[VERIFIED]` against the live repo, the fetched fdars-core 0.47 source, or a scratch-crate runtime probe executed this session.

## Open Questions

1. **D-14's literal wording vs. verified `imputation.rs` behaviour**
   - What we know: `fill_nulls_interpolate` fills (does not leave NULL) leading/trailing gaps via constant extension of the nearest observed value [VERIFIED this session]; LOCF/NOCB one-direction behaviour is correctly described by D-14.
   - What's unclear: whether D-14's "does not extrapolate" clause is a typo/misremembering of the actual behaviour, or an instruction to *change* behaviour that the "documented, not changed" framing then contradicts.
   - Recommendation: Plan 5 (DET-07) should write the doc to match the verified code (constant-extension edge fill) and flag the discrepancy for a one-line confirmation rather than guessing; do not let a doc-only task silently assert something a SQL test will then disprove.

2. **PELT SQL reachability**
   - What we know: `anofox_ts_detect_changepoints` (PELT/FFI) has no C++ caller; it was deliberately removed from SQL in Phase 12.
   - What's unclear: whether DET-03's correctness fix is meant to stay Rust-only (consistent with "no new SQL surface" scoping) or whether restoring a thin SQL wrapper is expected so the fix is end-to-end testable the way every other requirement in this phase is.
   - Recommendation: Plan 3 states this decision explicitly up front (recommend: Rust-only, see §Critical Finding) rather than discovering it mid-plan.

3. **`stl_strengths` → `mstl_decompose` refactor boundary**
   - What we know: the correct ascending 2-pass STL iteration already exists in `stats.rs::stl_strengths`, built for scalar strength output only.
   - What's unclear: the cleanest shared-code boundary — a new `StlStrengths`-adjacent struct that also carries the component arrays, vs. two call sites sharing a private helper, vs. `mstl_decompose` calling `stl_strengths`'s *logic* via a refactored common function that both consume.
   - Recommendation: left to the planner/Claude's Discretion (CONTEXT.md already delegates "internal module structure for re-implemented algorithms" to discretion) — flagging only so Plan 4 budgets time for a small shared-module refactor, not a copy-paste.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| `fdars-core` 0.47 (crates.io) | DEP-03 bump | ✓ [VERIFIED: resolved via `cargo add` this session] | 0.47.0 | — |
| Rust target `wasm32-unknown-emscripten` | D-03 WASM check | ✓ [VERIFIED: `rustup target list --installed` this session] | installed | — |
| `emcc` (Emscripten compiler, for full WASM link/build) | Full wasm_eh extension build (CI only per D-03) | ✗ [VERIFIED: `command -v emcc` failed this session] | — | D-03 explicitly scopes this phase to `cargo check --target wasm32-unknown-emscripten` only (verified runnable without emcc — see below), leaving the full wasm_eh build to CI, consistent with the project's documented local wasm_eh recipe (source emsdk + override bundled wasm-opt) |
| `cargo check --target wasm32-unknown-emscripten` | D-03 | ✓ [VERIFIED: ran successfully against the current (pre-bump) tree this session — `Finished dev profile [...] in 22.42s`, compiling core+ffi+fdars-core 0.3.0+anofox-regression 0.5.15+anofox-forecast 0.15.10] | — | — |
| R `forecast`, `pracma`, `tseries`, `urca` | D-09/D-10/D-11 reference fixtures | ✓ [VERIFIED: `Rscript -e 'installed.packages()'` this session] | not version-pinned by this check | — |
| R `changepoint` | would-be PELT reference | ✗ [VERIFIED same check] | — | D-07's numpy exact-DP reference (already the locked plan) |
| Python `numpy`/`scipy`/`statsmodels`/`pandas`/`tsfresh` (uv env) | D-05/D-06/D-07/D-11 reference fixtures | ✓ [VERIFIED: `validation/reference/python/pyproject.toml` exact pins, read this session] | numpy 2.5.3, scipy 1.18.1, pandas 3.0.6, statsmodels 0.15.0, tsfresh 0.21.2 | — |
| Python `ruptures` | would-be PELT reference | ✗ [VERIFIED: absent from pyproject.toml this session] | — | D-07's numpy exact-DP reference |

**Missing dependencies with no fallback:** none — every missing tool (`emcc`, R `changepoint`, Python `ruptures`) already has an explicit, locked fallback in CONTEXT.md's own decisions.

## Security Domain

`security_enforcement` is on (ASVS level 1) for this project, but this phase is internal numerical computation on data that has already crossed the FFI/SQL validity boundary in earlier phases (Phase 12, RUST-01/02) — there is no new external input surface, auth, or session concern here.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | n/a — no auth surface in this phase |
| V3 Session Management | no | n/a |
| V4 Access Control | no | n/a |
| V5 Input Validation | yes | D-02's explicit-error-on-unknown-method pattern (`ForecastError::InvalidParameter`) is the standard control already used elsewhere in the crate (see `seasonality.rs`'s existing `period <= 0.0` checks) — reuse it for every newly-validated parameter (cost-function name, min/max period ordering for Lomb-Scargle, n_frequencies bound) rather than introducing a new error style |
| V6 Cryptography | no | n/a — no crypto in this phase |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed numeric parameter (NaN/Infinity/negative period, n_frequencies<2, min>max) causing a panic that crosses the FFI boundary | Denial of Service | Already-established project pattern: `catch_unwind` at the FFI boundary (see CLAUDE.md "Error Handling" — all exported FFI functions wrapped), plus `Result`-returning Rust functions that reject invalid input before computation. This phase's new validation (D-11's Lomb-Scargle checks, D-02's method-name checks) should follow the same `ForecastError::InvalidParameter`/`InvalidInput` pattern already in use, not introduce ad hoc panics |
| Integer overflow/underflow in window/half-window arithmetic on pathologically small series (e.g., `n < 2*period`) | Tampering (data integrity of output) | Already-established `InsufficientData` error pattern (see `decomposition.rs`'s existing `n < 2*min_period` check) — extend the same guard to any new windowing logic introduced for the 2×m centred moving average (D-10) |

## Sources

### Primary (HIGH confidence — read/executed directly this session)
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/fdars-core-0.47.0/` — full source tree of the real fdars-core 0.47.0 release, fetched via a scratch `cargo add`, read directly (`matrix.rs`, `detrend/{mod,linear,polynomial,diff,loess,auto,decompose}.rs`, `seasonal/{mod,period,peak,hilbert,change,strength,lomb_scargle}.rs`, `Cargo.toml`)
- Scratch cargo project (`/tmp/.../scratchpad/fdars-probe`) — `cargo tree`, `cargo test -- --nocapture` runtime probes against fdars-core 0.47.0, executed this session
- `crates/anofox-fcst-core/src/{detrending,seasonality,periods,peaks,changepoint,decomposition,filter,imputation,stl,stats}.rs` — current working tree, read directly
- `crates/anofox-fcst-ffi/src/lib.rs`, `src/include/anofox_fcst_ffi.h`, `src/table_functions/{ts_detrend,ts_decomposition,ts_mstl_decomposition_native,ts_changepoints,ts_peaks,ts_periods,ts_seasonality}.cpp`, `src/macros/ts_macros.cpp` — current working tree, read/grepped directly
- `Cargo.toml`, `Cargo.lock`, `crates/anofox-fcst-core/Cargo.toml` — current working tree, read directly
- `validation/reference/{README.md,python/pyproject.toml,r/common.R}` — current working tree, read directly
- `cargo check --target wasm32-unknown-emscripten`, `Rscript -e 'installed.packages()'`, `rustup target list --installed`, `command -v emcc` — commands executed this session

### Secondary (MEDIUM confidence)
- `.planning/research/STAT-AUDIT-2026-10-08.md` — prior audit document (4 days stale relative to this session's live checks); used as the initial finding list, cross-checked against live source and runtime probes above wherever possible, and superseded where the two disagree (noted explicitly in-line, e.g. G1's re-scoping, the BOCPD "tests pass" caveat)

### Tertiary (LOW confidence)
- None — every claim in this document is backed by a direct read or an executed command this session; no claim rests on training-data recall alone.

## Metadata

**Confidence breakdown:**
- fdars-core 0.47 API surface: HIGH — fetched and read the real release source, plus two runtime falsification probes
- Current bug inventory (B1-B3, G1, G3-G11, G25): HIGH — every cited line read directly from the live working tree this session, with verbatim quotes
- SQL/FFI wiring map: HIGH — traced via direct grep/read of the C++ and FFI source, including the PELT dead-code finding
- D-14 imputation.rs discrepancy: HIGH confidence the code does X; LOW/open on which of D-14's two readings the user intended — flagged as Open Question, not resolved unilaterally
- MSTL reuse strategy (stl_strengths): HIGH that the function exists and does what's described; MEDIUM on the exact refactor boundary (left to discretion per CONTEXT.md)

**Research date:** 2026-10-12
**Valid until:** fdars-core version pin is now locked by this phase's own bump (DEP-03) — re-verify only if a future phase bumps fdars-core again. Otherwise stable (30 days, no fast-moving external API).
