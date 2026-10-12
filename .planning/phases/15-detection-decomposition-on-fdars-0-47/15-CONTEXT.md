# Phase 15: Detection & Decomposition on fdars 0.47 - Context

**Gathered:** 2026-10-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Detrending, seasonality classification, period detection, peaks, changepoints and MSTL/classical decomposition return correct, non-trivial results matching numpy / scipy / statsmodels / R. All of this runs on fdars-core 0.47, whose FdMatrix API structurally prevents the swapped size-argument bug that made detrend/decompose/seasonality no-ops. Requirements DEP-03, WRAP-01, DET-01..DET-07. Audit input: `.planning/research/STAT-AUDIT-2026-10-08.md` §B (B1–B3), §G (G1, G3–G11, G17–G22, G25, G26) and §H (fdars-core).

Out of scope: conformal/bootstrap (G2, G12–G16 → Phase 16) and gaps/frequency (G23/G24, done in Phase 13).

</domain>

<decisions>
## Implementation Decisions

### fdars bump and wrapper fixes (DEP-03, WRAP-01)
- **D-01:** The first plan is the tracer. Bump fdars-core 0.3 → 0.47 with features `["parallel"]` on native and `["js"]` on WASM, and drop `linalg`. Fix the ~30 mechanical FdMatrix compile errors (detrending.rs, seasonality.rs, periods.rs, peaks.rs). The new API removes B1/B2, so `ts_detrend`, `ts_detrend_by`, `ts_decompose_seasonal`, `classify_seasonality` and amplitude modulation become non-trivial. Pin it with `ts_detrend` on a cubic series matching numpy `polyfit` to 1e-8, plus a strong sine classified as seasonal.
- **D-02:** Method names: `'quadratic'` → polynomial degree 2, `'cubic'` → degree 3. An unknown method raises an error listing the valid methods, never a silent fallback to auto (B3).
- **D-03:** WASM check: `cargo check --target wasm32-unknown-emscripten` for anofox-fcst-core and anofox-fcst-ffi, plus `cargo tree` showing that `anofox-regression 0.4.0` is gone. The full wasm_eh extension build is left to CI.
- **D-04:** Defects that remain in fdars 0.47 (ACF period G17, FFT period G18, wavelet strength G19) are fixed or replaced in our core wrapper. No upstream fdars PR in this phase; an upstream issue goes on the deferred list.

### Peaks and changepoints (DET-01, DET-02, DET-03)
- **D-05:** Peaks: re-implement scipy `find_peaks` in core instead of calling fdars, covering plateau midpoints, prominence, height and distance. Existing SQL parameters map onto it. A scipy fixture must find every reference peak at the right index and no minima (audit: 51 found vs scipy 66, only 31 matching).
- **D-06:** BOCPD: Adams & MacKay done properly (G3–G6): a normalised Student-t predictive, a Normal-Gamma update using the sample mean, and run-length truncation that renormalises instead of dropping mass. A changepoint is flagged when the MAP run length resets. The reference is an independent numpy implementation in `validation/reference/python`, plus behaviour tests: a 3σ step is detected within a few steps, and stationary noise produces no changepoints.
- **D-07:** PELT (G7, G8): the cost is normalised by a robust noise variance (MAD of first differences) with a BIC-style penalty, so a series and that series ×100 give identical changepoints. L1 cost uses the median, and pruning actually works. The reference is a numpy exact optimal-partitioning DP (PELT must equal it on small n), because R `changepoint` and Python `ruptures` are not installed.
- **D-08:** Breaking output changes get a CHANGELOG before/after table and no compatibility flag (same as Phases 13 and 14).

### Decomposition, periods and seasonal strength (DET-04, DET-05, DET-06)
- **D-09:** MSTL: `mstl_decompose` goes through Phase 13's R-faithful `stl.rs` and the `forecast::mstl` iteration already in `stats.rs` (shortest period first, 2 passes, R `s.window = 7 + 4*i`). The primary reference is R `forecast::mstl` (installed); statsmodels `MSTL` is only noted. There is one shared implementation for the `ts_stats` strengths and for decomposition.
- **D-10:** Classical decomposition: an even period uses the centred 2×m moving average (R `stats::decompose`), so a constant series of 1s yields a trend of exactly 1.0 (G10, G11). Checked against an R `decompose` fixture.
- **D-11:** Period detection (G17, G18, G20, G21):
  - FFT removes a linear trend before the periodogram, handles odd n, and never returns n or anything ≥ n/2.
  - ACF allows lag 2 and requires the peak to exceed the 1.96/√n significance bound, else it returns no period.
  - The regression-period default grid includes all integers 2..n/2, so it finds 12.
  - Lomb-Scargle raises errors for n_frequencies < 2 and min > max.
  - References: scipy `periodogram` / `lombscargle` and statsmodels `acf`.
- **D-12:** Wavelet seasonal strength is removed: requesting it raises an error naming the alternatives (still broken in fdars 0.47, always 1.0). The variance and spectral strengths detrend first. The trend-strength doc is corrected to match the implementation (G22).

### Smaller fixes, testing and plan split (DET-07)
- **D-13:** `is_constant` uses a scale-relative tolerance: constant if `max − min ≤ 1e-10 · max(1, max|x|)`, ignoring NaN/NULL (G25).
- **D-14:** Edge-NULL interpolation behaviour is documented, not changed: linear interpolation does not extrapolate leading/trailing NULLs, and LOCF/NOCB fill in one direction only. Docs plus a SQL assertion pinning it (G26).
- **D-15:** Testing: a new NON-json-gated `test/sql/ts_detection_reference.test` covers every requirement, plus Rust `crates/anofox-fcst-core/tests/*_reference.rs` tests over checked-in fixtures (validation harness: numpy/scipy/statsmodels via the uv project, R via Rscript). The existing detection SQL tests are all json-gated; removing that gate is left to Phase 18 (TEST-03). The assertion count must visibly rise in `make test_release`.
- **D-16:** 5 plans: (1) fdars bump + detrend/classify/amplitude-modulation tracer [DEP-03, WRAP-01]; (2) scipy peaks [DET-01]; (3) BOCPD + PELT [DET-02, DET-03]; (4) MSTL + classical decomposition [DET-04]; (5) periods, seasonal strength, `is_constant`, edge-NULL docs and the docs/skills/CHANGELOG sweep [DET-05, DET-06, DET-07].

### Claude's Discretion
- Internal module structure for re-implemented algorithms, fixture series choice and layout, exact per-test tolerances (each justified in validation/reference/README.md), and BOCPD hazard/prior defaults (documented).

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `crates/anofox-fcst-core/src/stl.rs` (R `stats::stl` port) and `supsmu.rs` from Phase 13, plus the `forecast::mstl` iteration in `stats.rs` (around :548–:600).
- The validation harness in `validation/reference/` (uv project with numpy 2.5.3 / scipy 1.18.1 / statsmodels 0.15.0; R scripts in `validation/reference/r/`; `fixture_io.py`), Rust `tests/common/mod.rs::load_fixture()`, and the non-gated `*_reference.test` SQL pattern.
- The phase snippet harness (`.planning/phases/14-tsfresh-feature-parity/snippets/`) for docs checks.
- R `forecast` and `pracma` are installed; R `changepoint` and Python `ruptures` are not.

### Established Patterns
- fdars-core use: `crates/anofox-fcst-core/Cargo.toml` (native `["parallel","linalg"]`, WASM `["js"]`); fdars is called from detrending.rs (491 lines), seasonality.rs (943), periods.rs (2103) and peaks.rs (323). changepoint.rs (551), decomposition.rs (369), filter.rs (398, `is_constant` :47) and imputation.rs (194) are core-only.
- Breaking statistic changes get CHANGELOG before/after rows. Docs and skills are updated in the same phase. Stale "fix planned" notes are removed.

### Integration Points
- Existing SQL tests (all json-gated): ts_changepoints, ts_classify_seasonality, ts_decomposition, ts_detrend, ts_peaks, ts_periods(_advanced/_specialized), ts_seasonality. These will shift numerically.
- Skill: `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md`. Docs: the detection/decomposition pages under `docs/api/`.

</code_context>

<specifics>
## Specific Ideas

- ROADMAP anchors: cubic `ts_detrend` trend = numpy polyfit to 1e-8 (previously trend 0); a strong sine is classified as seasonal (previously non-seasonal/NaN); peaks match scipy (previously 51 vs 66, 31 matching); BOCPD detects a 3σ step and stays quiet on noise; PELT ×100 gives the same changepoints (previously 32 spurious); a constant series of 1s gives an MSTL/decompose trend of exactly 1.0 (previously 1.05); the FFT period on trend + seasonal is the seasonal period, not n; the regression-period default finds 12; wavelet strength on white noise ≈ 0 or the method is removed (D-12: removed).
- DoD as in Phases 13 and 14: `cargo fmt --check`, `cargo test --workspace`, `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`, `make test_release`, snippet harness 0 FAIL, plus the WASM `cargo check` (D-03).

</specifics>

<deferred>
## Deferred Ideas

- An upstream fdars-core issue or PR for the ACF period, FFT period and wavelet-strength defects (G17–G19).
- Removing the json gate from the existing detection SQL test files (Phase 18 / TEST-03).
- A correct wavelet seasonal strength, if it is ever wanted again.

</deferred>
