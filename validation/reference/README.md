# Reference-Fixture Harness (TEST-01)

## 1. Purpose

Every statistic and model-point value the `anofox_forecast` extension computes must
be verified against an independent reference implementation — R's `forecast` /
`tseries` / `urca` ecosystem, or Python's `scipy` / `numpy` / `statsmodels` /
`tsfresh` — before it is trusted. This directory is the harness that makes that
verification repeatable:

- `r/` and `python/` hold checked-in **generator scripts**. Running a generator
  produces one or more JSON (plus, for some families, CSV) **fixture files** under
  `fixtures/<family>/`.
- Fixtures are **ground truth**: they carry the exact input data, the exact R/Python
  call that produced the expected output, the tool/package versions that produced
  it, and the tolerance a consuming test should use. They are regenerated only
  deliberately — never as a side effect of running `cargo test`.
- Rust (and, where noted, SQL) tests load these fixtures and assert the product's
  output against them within the fixture's own declared tolerance.

This harness is seeded in Phase 9 and extended by every later phase of the
`v0.10.0` "Statistical Correctness" milestone — each new statistical family gets
its own fixture family here, following the contract in section 3.

## 2. Layout

```
validation/reference/
├── README.md                      # this file
├── r/
│   ├── common.R                   # script-dir resolution, provenance, shared reference_series()
│   ├── gen_forecast_reference.R   # writes r_forecast/{air_passengers,simulated_series}.json
│   ├── gen_diagnostics_reference.R# writes r_diagnostics/stationarity_portmanteau.json
│   └── ...                        # later phases add generators here
├── python/
│   ├── pyproject.toml             # uv project: numpy, scipy, pandas, statsmodels, tsfresh (package = false)
│   ├── uv.lock                    # committed lockfile — `uv run --locked` refuses a drifted lock
│   ├── fixture_io.py              # shared provenance + strict-JSON/CSV writers
│   ├── gen_moments.py             # writes moments/scipy_moments.json
│   ├── gen_multiscale_ols.py      # writes multiscale_ols/{cases.json,exog_typical.csv}
│   ├── gen_panel_nan_guard.py     # writes panel_nan_guard/cases.json
│   └── ...                        # later phases add generators here
└── fixtures/
    ├── r_forecast/{air_passengers,simulated_series}.json
    ├── r_diagnostics/stationarity_portmanteau.json
    ├── moments/scipy_moments.json
    ├── multiscale_ols/{cases.json,exog_typical.csv}
    ├── panel_nan_guard/cases.json
    └── <family>/...                # later phases add families here
```

The Rust loader lives outside this directory, at
`crates/anofox-fcst-core/tests/common/mod.rs` — see section 7.

## 3. Fixture contract

Every fixture JSON file MUST be a JSON object with these top-level keys (enforced
automatically by `fixture_provenance_contract` in
`crates/anofox-fcst-core/tests/reference_fixtures.rs`, for every fixture present
today and every fixture a later phase adds):

| Key | Meaning |
|-----|---------|
| `family` | Must equal the fixture's parent directory name (e.g. `moments` for `fixtures/moments/scipy_moments.json`) |
| `generator` | Repo-relative POSIX path of the script that produced this file (e.g. `validation/reference/python/gen_moments.py`); must resolve to an existing file |
| `tool_versions` | Non-empty object of tool/package name → exact version string that produced this file |
| `tolerance` | Non-empty object of tolerance keys a consuming test reads via `common::tolerance(&fx, key)` — never hard-code a tolerance in a Rust test |
| (family-specific) | `inputs`/`series`/`vectors`/`cases`/`parameters` — the actual input data, parameters, and expected outputs; shape is per-family, not a rigid schema (see Open Question 1 in 09-RESEARCH.md — the shape is left to stabilize organically) |

**Numeric encoding:** a fixture value is either a JSON number, or (for non-finite
floats) one of the strings `"NaN"`/`"NA"` (not-a-number), `"Inf"`/`"Infinity"`
(positive infinity), `"-Inf"`/`"-Infinity"` (negative infinity) — decoded by
`common::num()`. R's `jsonlite` and this repo's `fixture_io.py` both emit this
encoding; `json.dumps(..., allow_nan=False)` on the Python side guarantees the
file itself is strict JSON (no bare `NaN`/`Infinity` literals).

**CSV companions:** a `*.csv` file under a fixture family directory MUST have a
sibling `*.json` file in the same directory carrying that family's provenance —
`fixture_provenance_contract` enforces this too. The CSV itself carries only the
tabular data; tolerance and tool versions live in the JSON sibling.

**Tests must read tolerances from the fixture, never hard-code them** — this is
what makes a tolerance change in a regenerated fixture visible to every consuming
test without a separate Rust edit.

## 4. Families

| Family | Files | Generator | Tolerance keys (values) | Consumers |
|--------|-------|-----------|--------------------------|-----------|
| `r_forecast` | `air_passengers.json`, `simulated_series.json` | `r/gen_forecast_reference.R` | `closed_form_abs` (1e-8), `model_point_rel` (0.01), `interval_rel` (0.05) | Phase 9 (closed-form Naive/SeasonalNaive/RandomWalkDrift), Phase 10 (FCST-01 model-point parity), Phase 17 (FCST-09 full R parity) |
| `r_diagnostics` | `stationarity_portmanteau.json` | `r/gen_diagnostics_reference.R` | `statistic_abs` (1e-6), `p_value_abs` (1e-4) | Phase 11 (upstream diagnostics), Phase 17 (DIAG-03..05) |
| `moments` | `scipy_moments.json` | `python/gen_moments.py` | `abs` (1e-12), `rel` (1e-12) | Phase 9 (self-consistency pin), Phase 13 (STAT-01 `ts_stats` moments) |
| `multiscale_ols` | `cases.json`, `exog_typical.csv` | `python/gen_multiscale_ols.py` | `coef_rel` (1e-8), `coef_abs` (1e-9), `fitted_abs_per_sd` (1e-8), `sql_point_abs` (1.0) | Phase 9: `forecast.rs::tests::fit_ols_regression_matches_numpy_lstsq_fixture` (DEP-01, inline), `tests/exog_ols_memory.rs` (15k-row peak-heap guard), `validation/reference/sql/check_dep01_exog.sql` (SQL-level, DEP-01) |
| `panel_nan_guard` | `cases.json` | `python/gen_panel_nan_guard.py` | outcome-class only (`abs: 0.0`, see fixture `tolerance.notes`) | Phase 9: `anofox-fcst-ffi::panel_ffi_tests::panel_nan_guard_fixture_outcomes` (DEP-02) |
| `forecast_snapshot` | `point_forecasts.json` | `crates/anofox-fcst-core/tests/forecast_snapshot.rs` (bless mode: `ANOFOX_BLESS_FORECAST_SNAPSHOT=1 cargo test -p anofox-fcst-core --test forecast_snapshot`) | `point_abs` (1e-6), `point_rel` (1e-6) | Phase 9 (DEP-02 pre/post-bump point-forecast parity for every `ts_forecast_by` model), and any later phase that deliberately changes point forecasts — re-blessing replaces the anofox-forecast 0.15.3 baseline, so record the reason in that phase's SUMMARY. `allowed_to_change`: MFLES, AutoMFLES, Laplace. |
| `r_entropy` | `approx_entropy.json` | `r/gen_entropy_reference.R` | `abs` (1e-12) | Phase 13 (STAT-03, D-03): `ts_stats` `entropy` column (Pincus approximate entropy, m=2, r=0.2*sd) |
| `gap_counts` | `cases.json` | `python/gen_gap_counts.py` | `count_abs` (0, exact integer counts) | Phase 13 (STAT-03, D-04): `ts_stats` `n_gaps`/`n_gap_runs`/`expected_length` (13-01); Phase 13 data quality gap/temporal scoring and month-end-aware gap fill (`fill_cases`, 13-03) |
| `quality_scores` | `cases.json` | `python/gen_quality_reference.py` | `score_abs` (1e-12) | Phase 13 (D-05/D-06, STAT-04, 13-03): `anofox_fcst_core::quality::compute_data_quality` -- all five scores, `n_gaps`/`n_gap_runs`/`n_missing`/`is_constant`/`is_short` |
| `r_mase` | `accuracy_mase.json` | `r/gen_mase_reference.R` | `mase_abs` (1e-12) | Phase 13 (D-08, STAT-06, 13-04): `anofox_fcst_core::metrics::mase` (Hyndman-Koehler MASE, scaled by the in-sample seasonal-naive MAE at lag `seasonal_period`) -- each case carries BOTH `mase_accuracy` (`forecast::accuracy()`) and `mase_manual` (the formula), which must agree |
| `tsfresh_features` | `mapping.json`, `white_noise_n200.json`, `random_walk_n200.json`, `seasonal_ar12_n144.json`, `trend_season_n120.json`, `intermittent_n100.json`, `short_n20.json`, `constant_n30.json`, `all_zero_n50.json`, `tiny_n8.json` | `python/gen_tsfresh_features.py` | `abs` (1e-9), `rel` (1e-9) -- see `mapping.json`'s own `tolerance` and per-row overrides | Phase 14 plans 14-01..14-04 (FEAT-01..FEAT-05): `anofox_fcst_core::features::extract_features` -- all 117 `list_features()` names, via `crates/anofox-fcst-core/tests/tsfresh_features_reference.rs` and `test/sql/ts_features_reference.test` |
| `numpy_detrend` | `polyfit_cases.json` | `python/gen_detrend_reference.py` | `trend_abs` (1e-8), `coef_rel` (1e-8), `coef_abs` (1e-10), `rss_rel` (1e-8) | Phase 15 plan 15-01 (DEP-03, WRAP-01, D-01): `anofox_fcst_core::detrending::{detrend_linear,detrend_polynomial,detrend}` -- fitted trend, ascending-power coefficients (raw 0-based index basis), detrended residuals, and RSS for degree 1/2/3 on 4 seeded series, via `crates/anofox-fcst-core/tests/detrend_reference.rs` and `test/sql/ts_detection_reference.test` |
| `scipy_peaks` | `cases.json` | `python/gen_scipy_peaks_reference.py` | `prominence_abs` (1e-12), `height_abs` (0.0) | Phase 15 plan 15-02 (DET-01, D-05): `anofox_fcst_core::peaks::{local_maxima,peak_prominences,find_peaks,detect_peaks}` -- `scipy.signal.find_peaks` 1.18.1 plateau-midpoint local maxima and `scipy.signal.peak_prominences` (every case, even without a `prominence` filter), for an audited random walk, a seasonal sine with height/distance/prominence filters (incl. a combined case), plateau/edge/tie/degenerate series, via `crates/anofox-fcst-core/tests/peaks_reference.rs` and `test/sql/ts_detection_reference.test` |
| `changepoints` | `bocpd_cases.json`, `pelt_cases.json` | `python/gen_changepoint_reference.py` | `prob_abs` (1e-10), `sigma_rel`/`cost_rel` (1e-12/1e-9) | Phase 15 plan 15-03 (DET-02, DET-03, D-06, D-07): `anofox_fcst_core::changepoint::{robust_noise_scale,bocpd_trace,detect_changepoints_bocpd,detect_changepoints}` -- an independent numpy BOCPD (Adams & MacKay 2007: normalised Student-t predictive, sample-mean Normal-Gamma update, truncation-with-renormalisation at run length 500) and an independent numpy exact optimal-partitioning DP (PELT's own ground truth, no pruning), via `crates/anofox-fcst-core/tests/changepoint_reference.rs` and `test/sql/ts_detection_reference.test` (BOCPD only -- PELT has no SQL surface) |
| `r_decomposition` | `cases.json` | `r/gen_decomposition_reference.R` | `component_abs` (1e-8), `classical_abs` (1e-10) | Phase 15 plan 15-04 (DET-04, D-09, D-10): `anofox_fcst_core::decomposition::{mstl_fit,mstl_decompose,classical_decompose}` -- R `forecast::mstl()` (6 `mstl_cases`: single/multi/dropped-period/non-seasonal/constant) and R `stats::decompose()` (5 `decompose_cases`: additive/multiplicative, odd/even period, short series), via `crates/anofox-fcst-core/tests/mstl_decompose_reference.rs` and `test/sql/ts_detection_reference.test` |
| `period_detection` | `cases.json` | `python/gen_period_detection_reference.py` | `power_rel` (1e-9), `power_abs` (1e-12), `acf_abs` (1e-12), `rss_rel` (1e-9), `rss_abs` (1e-20), `ls_rel` (1e-9), `detrend_abs` (1e-10) | Phase 15 plan 15-05 (DET-05, DET-06, D-11, D-12): `anofox_fcst_core::periods::{detrended_periodogram,estimate_period_fft_ts,acf,estimate_period_acf_ts,regression_period_rss,estimate_period_regression_ts,lomb_scargle_power,lomb_scargle}` and `seasonality`'s detrend-first strength functions -- `scipy.signal.periodogram`/`lombscargle`/`detrend`, `statsmodels.tsa.stattools.acf`, and `numpy.linalg.lstsq` RSS across five case groups (periodogram, acf, regression, lombscargle, strength), via `crates/anofox-fcst-core/tests/period_detection_reference.rs` and `test/sql/ts_detection_reference.test` |
| `conformal` | `split_cases.json` | `python/gen_conformal_reference.py` | `score_abs` (1e-9), `coverage_abs` (1e-12) | Phase 16 plan 16-01 (RUST-04, D-01, D-02, D-03; extended by 16-02/16-03): `anofox_fcst_core::conformal::{conformal_from_pairs,conformal_calibrate}` -- per-group and pooled split-conformal `conformity_score`/empirical `coverage`/`nominal_level`/`n_calibration` on a two-group calibration pool (`two_group`, alpha 0.1/0.2) and a pairwise-complete `ts_mean_interval_width` case (`interval_width`), via `crates/anofox-fcst-core/tests/conformal_reference.rs` and the non-json-gated `test/sql/ts_conformal_reference.test` |
| `bootstrap` | `lognormal_cases.json` | `python/gen_bootstrap_reference.py` | `bound_rel_width` (0.12) | Phase 16 plan 16-03 (CONF-01, D-10): `anofox_fcst_core::bootstrap::{bootstrap_intervals,bootstrap_quantiles}` -- an independent numpy simulation of the SAME residual-resampling construction (`anofox_forecast::postprocess::bootstrap::{simulate_paths,extract_quantiles}`) on a discrete mirror-anchor case (exact 100.0/110.0) and a seeded right-skewed log-normal residual case plus its exact negation (`lognormal_right`/`lognormal_left`), proving the long tail lands on the correct side after the fix for the pre-Phase-16 sign bug (G2), via `crates/anofox-fcst-core/tests/conformal_reference.rs` and the non-json-gated `test/sql/ts_conformal_reference.test` |

**`tsfresh_features`'s `mapping.json` is a second, hand-maintained fixture** (not
generator output) alongside the nine series fixtures -- it is the D-01
completeness contract that makes the other nine files meaningful. Each of its
117 rows names, per feature in `anofox_fcst_core::features::list_features()`,
either the exact `tsfresh.feature_extraction.feature_calculators` function and
parameters to call (`reference: "tsfresh"`), or a hand-written numpy/scipy
formula for names with no tsfresh counterpart (`reference: "numpy/scipy"`,
D-02). `mapping.json`'s own `generator` key names the Python script that reads
it (`gen_tsfresh_features.py`), which calls the named calculator **directly**
with the row's pinned params -- never via `tsfresh.extract_features()`'s
settings-driven convenience API, which silently uses the wrong default
parameter grid for several of this project's exact choices (agg_linear_trend
chunk_len, approximate_entropy r=0.2, …; see 14-RESEARCH.md "Fixture
Generation Methodology" / Pitfall 2). An optional `deviation` entry documents
a specific series where the row's own `expected_ours` value is the
intentionally correct Rust output (with a reason), and an optional
`tolerance` override replaces the family-wide default for that one row (also
with a reason, D-10).

**As of Phase 14 plan 14-04, every one of the 117 rows is `status: "match"`
(the FEAT-05 117/117 gate) -- `mapping.json` no longer carries any
`"pending:<owner-plan>"` row.**
`crates/anofox-fcst-core/tests/tsfresh_features_reference.rs` enforces this:
`mapping_covers_list_features_exactly` guards the schema and 117/117
coverage and asserts every row's `status` is `"match"`; the nine
`tsfresh_parity_<series>` tests each assert all 117 rows and require exactly
117 assertions per series (no silent skips); `sample_entropy_roadmap_anchor`
pins the ROADMAP's literal `sample_entropy` anchor value independently of the
fixture JSON.

### Intentional deviations and tolerance overrides (D-03, D-10)

The complete, authoritative list lives in `mapping.json`'s own `deviation`/
`tolerance` keys per row; this table mirrors it for human readability.

| Feature | Series | Our value | Reason |
|---------|--------|-----------|--------|
| `skewness` | `constant_n30`, `all_zero_n50` | `NaN` | Zero-variance sample: G1/G2 are `0/0`. `scipy.stats.skew(bias=False)` and `ts_stats` return `NaN` (D-05); pandas/tsfresh return `0` by convention, which is arguably wrong for an undefined moment (D-03). |
| `kurtosis` | `constant_n30`, `all_zero_n50` | `NaN` | Same zero-variance rationale as `skewness` above (shared `sample_excess_kurtosis_g2` helper, D-05). |

No `mapping.json` row carries a per-row `tolerance` override as of 14-04 --
the family-wide default (`abs: 1e-9, rel: 1e-9`) held on every one of the 117
features across all 9 fixture series, including the FFT/spectral family
(rustfft's evaluation order) and the entropy/Benford/Lempel-Ziv family added
in plans 14-03/14-04.

**`numpy_detrend` tolerance rationale (Phase 15 plan 15-01):** `trend_abs`
(1e-8) and `coef_abs` (1e-10) bound the fitted-trend and coefficient
differences in absolute terms -- both `numpy.polyfit` (SVD-based) and
fdars-core's own QR/SVD polynomial fit are well-conditioned for the degree
1-3, n <= 60 cases here, so double-precision linear-algebra noise is the
only expected discrepancy, well under 1e-8. `coef_rel` (1e-8) additionally
bounds the larger-magnitude coefficients in relative terms, since the raw
0-based index basis (as opposed to fdars-core's internal `t/(n-1)`
normalisation) can produce leading coefficients orders of magnitude apart in
size. `rss_rel` (1e-8) is relative for the same reason, but the Rust test
additionally folds in `trend_abs` as an absolute floor: a perfect/constant
fit's true RSS is conceptually 0, and `numpy.polyfit`'s own floating-point
evaluation of that 0 is not bit-identical to fdars-core's (observed ~1e-29
vs exactly `0.0`), which a relative-only bound cannot absorb.

**`scipy_peaks` tolerance rationale (Phase 15 plan 15-02):** `prominence_abs`
(1e-12) is tight because the Rust port's prominences are computed from the
exact same input doubles with no intermediate fitted model (unlike
`numpy_detrend`'s SVD-based fits) -- both scipy's `peak_prominences` and the
Rust `O(n)` monotonic-stack port are simple min/max/subtract chains over the
input array, so the only expected discrepancy is double-precision summation
order, well under 1e-12. `height_abs` (0.0) is exact: a peak's height is
read directly from the input series at an integer index on both sides, with
no floating-point computation involved -- any non-zero difference would
mean the port picked the wrong index, not a rounding difference.

**`changepoints` tolerance rationale (Phase 15 plan 15-03):** `prob_abs`
(1e-10) bounds BOCPD's per-step `P(r_t=1)` in absolute terms -- the
Student-t predictive is computed via `ln_gamma` (statrs vs scipy's
`gammaln`, both IEEE-754 double implementations of the same special
function), so the two can differ at the level of the last 1-2 ULPs of the
log-gamma evaluation itself, well under 1e-10 even after 1500 sequential
updates (`long_n1500`) compound that noise. `sigma_rel` (1e-12) is tight
because `robust_noise_scale` is a closed-form MAD/median computation with no
iterative fit. `cost_rel` (1e-9) is looser than `sigma_rel` because PELT's
total cost sums `O(n)` segment-cost terms (each itself a sum over a
segment), so floating-point summation-order noise compounds across segments
more than a single scale computation does.

**`r_decomposition` tolerance rationale (Phase 15 plan 15-04):**
`component_abs` (1e-8) bounds the `mstl_fit`/`mstl_decompose` trend/seasonal/
remainder against R `forecast::mstl()` -- `stl.rs`/`supsmu.rs` are 1:1 ports
of R's own `stats::stl`/`stats::supsmu` C/Fortran sources (Phase 13), so
only floating-point summation-order noise is expected, matching
`r_stl_strength`'s own `component_abs`. `classical_abs` (1e-10) is tighter
for `classical_decompose` against R `stats::decompose()` because the
centred moving-average filter and per-phase mean are simple sum/divide
chains with no iterative fit -- the only expected discrepancy is
double-precision summation order over at most ~150 terms.

**Deliberate `classical_decompose` deviation from R `stats::decompose`
(Phase 15 plan 15-04, D-10):** R's `type = "multiplicative"` silently
divides by non-positive values, producing `Inf`/`NaN`/negative results with
no diagnostic. `classical_decompose` rejects any value `<= 0` for the
multiplicative kind with `InvalidParameter` instead -- a deliberate,
documented improvement over R's silent garbage-out behaviour, not a parity
bug. No fixture case exercises this path (every `decompose_cases` series is
strictly positive).

**`changepoints` measured false-alarm rate (Phase 15 plan 15-03, not part of
the fixture itself but measured against the fixed implementation and
recorded here per D-06's own instruction that BOCPD's real false-alarm
behaviour be documented, not tuned away):** on 100 independent `N(0,1)`
series of length 200 (seeds `16000..16099`), `hazard_lambda=250` (default)
flags 20 points in total across all 100 series, `500` flags 11, `1000`
flags 5. On 100 independent 3-sigma-step series of length 200 (step at
index 100, seeds `17000..17099`), the step is flagged within 3 samples of
the true changepoint (index 100-103) in 94/100 series at every one of
those three hazard values.

**`period_detection` tolerance rationale (Phase 15 plan 15-05):** `power_rel`/`power_abs`
bound `detrended_periodogram` against scipy's own `periodogram(detrend='linear',
scaling='spectrum')` -- both FFTs are unnormalised `O(n log n)` transforms of the same
linearly-detrended input, so only floating-point summation-order noise is expected. `acf_abs`
(1e-12) is tight for the same reason: both implementations are a direct `sum/sum` ratio with
no iterative fit. `rss_rel` (1e-9) bounds `regression_period_rss` against `numpy.linalg
.lstsq` in relative terms; `rss_abs` (1e-20) is an additional absolute floor for the
near-perfect-fit case (`pure_sine12_n120` at its true period), where both `numpy.linalg
.lstsq` (SVD-based) and the Rust modified-Gram-Schmidt QR solver evaluate a conceptual `0`
RSS to ~1e-28 but not bit-identically -- a relative-only bound cannot absorb that (same
rationale as `numpy_detrend`'s `trend_abs`, above). `ls_rel` (1e-9) bounds
`lomb_scargle_power` against `scipy.signal.lombscargle(..., normalize=True) * n/2` over the
EXACT frequency grid the Rust implementation itself builds (not scipy's own, since
`lombscargle` takes no period/frequency range argument -- the generator reproduces the grid
construction formula). `detrend_abs` (1e-10) bounds the `detrend_linear` residual fed to the
seasonal-strength functions against `scipy.signal.detrend(type='linear')`, matching
`numpy_detrend`'s own `trend_abs` for the same least-squares-fit reason.

**`conformal` tolerance rationale (Phase 16 plan 16-01):** `score_abs` (1e-9) is looser
than most of this file's quantile-only families because `conformity_score` compounds two
sources of floating-point noise rather than one: numpy's own `np.quantile(..., method=
"linear")` lerp formula evaluates `lower + (upper - lower) * fraction` on the SAME sorted
`|residual|` array the Rust `compute_quantile` (`conformal.rs`) evaluates with an
algebraically-equivalent but not bit-identical expression, AND each residual itself is a
subtraction (`actual - forecast`) performed independently in numpy (float64 throughout) and
in Rust (`f64`, `complete_cases`-filtered) -- two independent rounding sources at
double-precision, well under 1e-9 even summed. `coverage_abs` (1e-12) is tight because
empirical coverage is an exact fraction of EXACT comparisons (`|residual| <= conformity_score`
evaluated on the same values both sides already agree to `score_abs`) over a fixed, small `n`
(20 or 40) -- the only possible discrepancy is a residual landing within `score_abs` of the
threshold and comparing differently, which the fixture's hand-picked values avoid.

**`bootstrap` tolerance rationale (Phase 16 plan 16-03, D-10):** `bound_rel_width` (0.12)
bounds a 20,000-path Rust simulation against the fixture's 200,000-path numpy ground truth, as
a FRACTION of the ground truth's own interval width at that step -- not an absolute tolerance,
since the width itself grows with horizon. The mirror-anchor case needs no tolerance at all:
its residuals are strictly `{0.0, 10.0}` with `P(10.0) = 0.1`, so the 5%/95% order statistics
land deep inside a long run of identical values regardless of path count or RNG algorithm,
making `lower`/`upper` exactly `100.0`/`110.0` on BOTH numpy and Rust. The continuous
`lognormal_right`/`lognormal_left` cases are the ones that need the 0.12 bound: residuals
rounded to 4 decimals and only 5 cumulative draws per path make the shortest-horizon step's
width narrow enough that relative sampling error reaches ~8.9% even re-simulated independently
at 100,000 paths in numpy (measured directly, not assumed) -- a quantization/clustering
artifact of the discrete draws landing near that quantile's order-statistic index, not Monte
Carlo noise that keeps shrinking with more paths. 0.12 covers the measured worst case with
margin while still failing hard on an actual sign/construction bug, which misses by closer to
100% (the wrong tail entirely stays on the wrong side), not ~9%.

## 5. Tool versions

These are the toolchain versions this phase verified against. **Each fixture's own
`tool_versions` block is authoritative** — if a later regeneration bumps a package,
the fixture records the new version and this table becomes historical context, not
a live source of truth.

- **R** 4.6.1, with `forecast` 9.0.2, `tseries` 0.10.63, `urca` 1.3.4, `jsonlite`
  2.0.0. `feasts` is **not installed** — any STL-strength family (STAT-02) must use
  `stats::stl` instead.
- **Python** 3.12, managed via `uv` (see `python/pyproject.toml` / `uv.lock`), with
  `numpy` 2.5.3, `scipy` 1.18.1, `pandas` 3.0.6, `statsmodels` 0.15.0, `tsfresh`
  0.21.2.

## 6. Regeneration

Regenerate a family only deliberately — never as a side effect of running tests.

```bash
# R families (run from anywhere; common.R resolves paths relative to the script):
Rscript validation/reference/r/gen_forecast_reference.R
Rscript validation/reference/r/gen_diagnostics_reference.R

# Python families (uv project is cwd-independent via --project):
uv run --locked --project validation/reference/python python validation/reference/python/gen_moments.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_multiscale_ols.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_panel_nan_guard.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_gap_counts.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_quality_reference.py

# R families added in Phase 13:
Rscript validation/reference/r/gen_entropy_reference.R
Rscript validation/reference/r/gen_mase_reference.R

# Python family added in Phase 14 (reads mapping.json; never edit the nine
# series fixtures by hand -- only mapping.json and this script):
uv run --locked --project validation/reference/python python validation/reference/python/gen_tsfresh_features.py

# Python family added in Phase 15 plan 15-01:
uv run --locked --project validation/reference/python python validation/reference/python/gen_detrend_reference.py

# Python family added in Phase 15 plan 15-02:
uv run --locked --project validation/reference/python python validation/reference/python/gen_scipy_peaks_reference.py

# Python family added in Phase 15 plan 15-03:
uv run --locked --project validation/reference/python python validation/reference/python/gen_changepoint_reference.py

# R family added in Phase 15 plan 15-04:
Rscript validation/reference/r/gen_decomposition_reference.R

# Python family added in Phase 15 plan 15-05:
uv run --locked --project validation/reference/python python validation/reference/python/gen_period_detection_reference.py

# Python family added in Phase 16 plan 16-01 (extended by 16-02/16-03):
uv run --locked --project validation/reference/python python validation/reference/python/gen_conformal_reference.py

# Python family added in Phase 16 plan 16-03:
uv run --locked --project validation/reference/python python validation/reference/python/gen_bootstrap_reference.py

# Determinism check — every generator is seeded and writes no timestamps/hostnames,
# so regenerating with the same toolchain versions must produce an empty diff:
git diff --exit-code -- validation/reference/fixtures
```

If `git diff` is non-empty after a deliberate regeneration, either a package version
changed (expected — update `tool_versions` by committing the diff) or a generator
has a latent non-determinism bug (investigate before committing).

## 7. Consuming fixtures from Rust

**From an integration test** (`crates/anofox-fcst-core/tests/*.rs`, the normal case
— sees only the crate's public API):

```rust
mod common;
use common::{load_fixture, f64_vec, num, tolerance, assert_close, assert_vec_close};

let fx = load_fixture("multiscale_ols/cases.json");
let tol = tolerance(&fx, "coef_abs");
```

**From an inline `#[cfg(test)]` block inside `src/`** (needed when the function
under test is private, e.g. `fit_ols_regression` in `forecast.rs` — integration
tests cannot see non-`pub` items): the library includes the same loader via a
`#[path = "../tests/common/mod.rs"]` module (added in 09-03), which is why
`crates/anofox-fcst-core/tests/common/mod.rs` must stay free of any
`anofox_fcst_core`/`anofox_fcst_ffi` crate references — it has to compile
standalone as an integration test AND be re-pathed into the library's own
`cfg(test)` build.

`anofox-fcst-ffi` also includes the same loader this way (09-03): a
`#[cfg(test)] #[path = "../../anofox-fcst-core/tests/common/mod.rs"]` module
right above `panel_ffi_tests` in `crates/anofox-fcst-ffi/src/lib.rs`, reaching
the identical file `anofox-fcst-core` itself `#[path]`s in — not a duplicate
copy. `repo_root()` resolves correctly from either crate because both sit two
directories below the workspace root.

## 9. SQL checks

Some fixtures are additionally proven at the SQL level, against the actual
**built extension** (not just the Rust crates), because the FFI/SQL layer adds
behaviour (date handling, column dispatch, the `ts_forecast_exog_by` macro
expansion) that a Rust-only test cannot see.

| Script | Fixture | Run command | Expected output |
|--------|---------|--------------|------------------|
| `sql/check_dep01_exog.sql` | `multiscale_ols/{cases.json,exog_typical.csv}` | `python3 -c "import resource,subprocess,sys;rc=subprocess.run(sys.argv[1:],stdin=open('validation/reference/sql/check_dep01_exog.sql')).returncode;print('MAXRSS_KB',resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss);sys.exit(rc)" ./build/release/duckdb -unsigned -bail` (run from the repo root, after `make release` / `GEN=ninja make release`) | `DEP01_SQL_PASS typical` and `DEP01_SQL_PASS 15k`, exit 0 |

Needs the extension built (`make release`) and the `json` extension available
(autoloaded by DuckDB on first use if not statically linked). Tolerance is
read from `multiscale_ols/cases.json`'s `tolerance.sql_point_abs` at run time,
never hardcoded in the script.

## 8. Adding a family (Phases 10-18)

1. Write a generator in `r/` or `python/` (whichever tool owns the reference
   implementation for the statistic in question).
2. Use the shared provenance helpers — `provenance()`/`write_fixture()` in
   `r/common.R`, or `generator_path()`/`tool_versions()`/`write_fixture()` in
   `python/fixture_io.py` — so `fixture_provenance_contract` accepts the new file
   for free.
3. Choose tolerance keys with a one-line rationale (why this bound, not a tighter
   or looser one) and record them in the fixture's own `tolerance` object.
4. Write a Rust test (integration test under `tests/`, or inline `#[cfg(test)]` if
   testing a private function) that loads the fixture and asserts the product's
   output against it, reading tolerance from the fixture.
5. Add a row to the Families table in section 4 of this README.
