# Statistical Correctness Audit — 2026-10-08

Scope: whole extension (SQL macros, C++, Rust core, FFI) + upstream crates
(anofox-forecast 0.15.3, anofox-regression 0.5.3, fdars-core 0.3.0).
Guiding rule (user): **all statistical analysis must be correct and computed in the Rust crates**;
C++/SQL only marshal, group, dispatch.

Method: 6 parallel audits; numerical comparison vs R (forecast, tseries, urca, stats::stl),
scipy/numpy, re-implemented tsfresh formulas, Monte-Carlo simulation. Paths relative to repo root;
core = `crates/anofox-fcst-core/src`, ffi = `crates/anofox-fcst-ffi/src`.
Line numbers refer to the test/duckdb-2.0 tree at audit time (main is near-identical for these files).

Reference scripts from the audit (session scratchpad, may be ephemeral):
`/tmp/claude-1000/-home-simonm-projects-duckdb-anofox-forecast/ee86fffc-9d6b-4781-a89d-aacb4f70fb57/scratchpad/{cpp_sql,core_stats,core_detect,forecast_ffi,upstream_af,upstream_fdars_reg}`
— regenerate R/scipy reference fixtures if missing.

---

## A. Forecast wrapper (core/forecast.rs, ffi/lib.rs) — highest severity

| ID | Location | Issue | Evidence |
|---|---|---|---|
| A1 | forecast.rs:796-799, 1003; `calculate_confidence_intervals` :3414 | Intervals fabricated for all models except GARCH/Kalman/AutoEnsemble: point ± z·SD(raw series)·√h. Crate intervals (`predict_with_intervals`) discarded; Laplace distributional intervals overwritten. | AirPassengers 95% h=1 half-width 234 for every model (R: naive ±66, auto.arima ±22.5, ets ±26); negative lower bounds; MC coverage 99-100%, width 1.9×–4.7× truth |
| A2 | :3422-3428 | z from step table (0.85→1.28, 0.975→1.96, <0.8→1.0) | — must be `Φ⁻¹(1-α/2)` |
| A3 | :800-836, `calculate_fitted_values` :3449 | Fitted/residuals = SES(α=0.3) for all but Naive/SeasonalNaive/SWA; MSE from those. Crate `fitted_values()/residuals()` discarded. | MSE 2090.28 identical across SES/Holt/AutoARIMA/AutoETS/Theta/HW/MSTL/Croston |
| A4 | `forecast_arima` :1538 | "ARIMA" = AR(1) on diffs with fixed coef 0.5; n<5 → Naive labelled ARIMA | — |
| A5 | `forecast_arima_with_exog` :3259, `forecast_theta_with_exog` :3297, :3207 | Exog: OLS + fake ARIMA on residuals (AutoARIMA search never run); Optimized/DynamicTheta → plain Theta; OLS failure → zero coefs silently | should use crate exog support |
| A6 | `forecast_with_exog`/`forecast_with_model` :1058-1170 | exog path ignores ets_model, garch_p/q, kalman_model; AutoTheta regressors dropped | — |
| A7 | `forecast_auto_ets` :1782 | any crate error → hand-picked HW/Holt/SES still labelled AutoETS | silent fallback |
| A8 | `forecast_ets` :1474 | "ETS" w/o spec → HW/Holt/SES(α=0.3); ensemble builder :2775 "ETS" = AAA/ANN | inconsistent |
| A9 | dispatch :657-728 | AutoMFLES≡MFLES, AutoMSTL≡MSTL; MSTL/TBATS default period 12; HW/SeasonalES period 0 → 2 | silent |
| A10 | forecast_kalman :2522; ffi/lib.rs:7016,7045 | unknown kalman_model → local_level; panel Croston variant exact-match only | silent |
| A11 | ffi/lib.rs:3412,3667,4141,2248 | invalid UTF-8 model → "auto" | edge |
| A12 | ffi/lib.rs:3370 parse_seasonal_periods_str | unparsable entries dropped silently | silent |
| A13 | ffi build_series :104 | missing calendar dates not detected; valid-NaN → fit error; all-NULL → NaN forecasts, regressors have no validity | edge |
| A14 | build_forecaster :2616-2780 | ensemble members get different defaults than standalone | inconsistent |
| A15 | forecast.rs apply_regression | constant-in-history exog column → NaN OLS coef → all-NaN forecast | real bug (holiday flag absent in train) |

## B. Wrapper call bugs to fdars (core)

| ID | Location | Issue |
|---|---|---|
| B1 | detrending.rs:149,172,195,224,246,311,358 | fdars called `(values, n, 1, argvals)`; signature is `(data, n_samples=1, m=len)` → invalid-input path returns trend=0, detrended=input. `ts_detrend`, `ts_detrend_by`, `ts_decompose_seasonal` are no-ops |
| B2 | seasonality.rs:672-675, 798-801 | same swap: `classify_seasonality` (strong sine → non-seasonal, NaN), `detect_amplitude_modulation` |
| B3 | detrending | method 'quadratic' silently → auto |

## C. Diagnostics / validation (core/validation.rs + upstream stationarity.rs)

| ID | Issue |
|---|---|
| C1 | ADF (upstream `compute_adf_statistic`) not augmented — lagged Δy never in regression; lag only trims sample. Size test: 63.5% rejection at 5% on IMA(1,1) unit root (urca 10.5%) |
| C2 | ADF p-value = 9-step table, not MacKinnon; no trend/constant option |
| C3 | KPSS p-value extrapolated to [0,1]; must be bounded [0.01,0.10] like tseries (statistic itself correct) |
| C4 | `classify_stationarity` (validation.rs:124) labels inverted vs statsmodels convention; trend-stationary unreachable; constant series → "non_stationary" |
| C5 | Ljung-Box always fitted_params=0 (no df correction for model residuals) (validation.rs:179) |
| C6 | docs/api/10-diagnostics.md:155-165, :206 false claims (statsmodels parity, interpolation, KPSS clamp) |

Verified correct: Ljung-Box Q/p, Box-Pierce, Jarque-Bera, Durbin-Watson, KPSS statistic + Bartlett lag.

## D. C++/SQL layer — statistics outside Rust / marshalling bugs

| ID | Location | Issue |
|---|---|---|
| D1 | src/scalar_functions/metrics.cpp:13-30 `ExtractListAsDouble` (copied in conformal, bootstrap, diagnostics, ts_periods, ts_detrend, ts_seasonality, ts_changepoints, ts_features, ts_peaks, ts_decomposition) | NULLs dropped per list independently → misaligned pairs; for single-list diagnostics NULLs spliced out (lag structure shifts). `ts_mae([1,NULL,3,4],[1,2,NULL,4])`=0.333 (correct 0). Fix: validity passed to Rust, pairwise-complete policy owned by Rust |
| D2 | same scalar fns: `FlatVector::GetData` w/o `ToUnifiedFormat` | constant list arg vs table → wrong (`ts_mae(a,[0,0,0])` over 5 rows → 1, NULL×4). Template: ts_stats.cpp:99-135 |
| D3 | ts_macros.cpp:322-336 `ts_diff_by` | `LAG(v, d)` = lag-d diff, not d-th order diff. Rust `filter.rs:134 diff` exists |
| D4 | ts_macros.cpp:1652 `ts_conformal_by`, :1731 `ts_conformal_apply_by` | `LIST(... ORDER BY forecast value)` — horizon order destroyed; no date/step column |
| D5 | ts_macros.cpp:1756 `ts_interval_width_by` | lower/upper sorted independently (mean ok, pairing lost) |
| D6 | ts_macros.cpp:1645,1706 | residuals in SQL; `ts_conformal_calibrate` pools all groups; `coverage` column is nominal not empirical |
| D7 | ts_macros.cpp:303-319 `ts_fill_nulls_mean_by` | AVG in SQL; Rust `anofox_ts_fill_nulls_mean` exists |
| D8 | ts_backtest_native.cpp:281-374 ComputeMetric | metrics in C++; pools across groups (:861); unknown metric → RMSE; missing bounds → 0.0; registered but unused by macros → move to Rust or delete |
| D9 | ts_aggregate_hierarchy.cpp:292 | NULL → 0.0 in aggregation |
| D10 | ts_forecast_agg.cpp:408-417 | frequency from median µs step (month drift); Rust `anofox_ts_detect_frequency` exists |
| D11 | ts_forecast_agg.cpp:277, ts_features_agg.cpp:231 | NULL rows skipped + validity all-ones → series spliced (inconsistent with ts_forecast_by) |
| D12 | conformal.cpp:1342 | n_folds = size/horizon truncation unchecked |
| D13 | ts_filter.cpp, ts_imputation.cpp scalar bodies | dead code (Register no-ops) |
| D14 | ts_macros.cpp drop_constant | exact MIN≠MAX vs Rust 1e-10 tolerance |
| D15 | ts_forecast_exog_by :1110 | (suspected) generate_series end bound uses 30-day month |
| D16 | ts_backtest_native.cpp:818 | error sign forecast-actual vs SQL actual-forecast |

## E. Core statistics (stats.rs, quality.rs, metrics.rs)

| ID | Location | Issue | Evidence |
|---|---|---|---|
| E1 | stats.rs:192-212 | skew/kurt: population m3/m4 ÷ sample sd^k then G1/G2 correction → biased | x20 kurt −0.096 vs scipy +0.277 |
| E2 | stats.rs:217 | bimodality coef lacks SAS small-sample term | 0.600 vs 0.466 (crosses 0.555 threshold) |
| E3 | stats.rs:527-568 | trend_strength = |r| linear; seasonality_strength = max|ACF| at 2/4/7/12 — not Wang/Hyndman STL F_T/F_S | RW seasonality 0.96 vs F_S 0.05 |
| E4 | stats.rs:571 | "approximate entropy" is 10-bin Shannon | doc |
| E5 | stats.rs:465 | n_gaps counts gap intervals not missing periods | doc/semantics |
| E6 | ffi/lib.rs:4235 → quality.rs:66 | `ts_data_quality_by` passes dates=None → n_gaps 0, temporal 1.0 always; frequency/n_short ignored |
| E7 | quality.rs:178 find_mode | HashMap tie-break nondeterministic |
| E8 | quality.rs:252 behavioral_score | penalises ACF1>0.95, white noise perfect (design suspicion) |
| E9 | metrics.rs:113 mape | zero/NaN actuals dropped silently, denominator shrinks |
| E10 | metrics.rs:165 mase | relative MAE vs baseline, not Hyndman-Koehler (in-sample seasonal naive scaling); ≡ rmae; docs/skill claim otherwise |

Verified correct: MAE/MSE/RMSE/bias/R²/sMAPE(0-200)/pinball/mqloss/coverage, sample var/sd, ACF, type-7 quantiles, trimmed mean, Hill.

## F. Features (features.rs) vs tsfresh

| ID | Location | Issue |
|---|---|---|
| F1 | :525-540 | partial_autocorrelation lag3..5 = lag-2 formula (use Durbin-Levinson) |
| F2 | :146 | mean_second_derivative_central missing ½ |
| F3 | :131,626 | last_location_of_max/min uses idx/n not (idx+1)/n |
| F4 | :895 | time_reversal_asymmetry wrong term order |
| F5 | :970 | fft_coefficient ÷n (tsfresh unnormalised); O(n²) DFT |
| F6 | :998 | spectral centroid/variance on two-sided spectrum incl. DC (≈n/2) |
| F7 | :1075 | agg_linear_trend rvalue unsigned |
| F8 | :66 | skewness/kurtosis population vs pandas bias-corrected |
| F9 | :707 | sample_entropy wrong template counts (0.981 vs 1.099) |
| F10 | :658 | binned_entropy rounding vs np.histogram |
| F11 | various | benford |v|<1 ignored; permutation entropy normalised; LZ algorithm differs; number_peaks threshold doc |

## G. Detection / conformal / decomposition (core)

| ID | Location | Issue |
|---|---|---|
| G1 | fdars seasonal.rs:1093 via peaks.rs:151 | peak index shifted (j-1), missed peaks, minima reported, plateau prominence 0. scipy find_peaks 66 vs ours 51 (31 match). Re-implement in core (scipy semantics) |
| G2 | bootstrap.rs:72 | residual sign flipped → mirrored paths for skewed residuals |
| G3 | changepoint.rs:263 | BOCPD Student-t predictive missing normaliser (Γ, 1/scale) |
| G4 | changepoint.rs:255 | beta_n uses posterior mean not sample mean |
| G5 | changepoint.rs:339 | truncate(500) drops long run lengths |
| G6 | changepoint.rs:291 | rule P(r_t=1)>0.5 never fires; 3σ step missed |
| G7 | changepoint.rs:119 | PELT penalty 2 ln n on unnormalised SSE → scale-dependent (×100 → 32 CPs) |
| G8 | changepoint.rs:63,43,147 | normal cost degenerate; L1 uses mean not median; PELT pruning set unused (O(n³)) |
| G9 | decomposition.rs:254 | MSTL periods processed longest-first (R/statsmodels shortest-first) |
| G10 | decomposition.rs:57-130,272 | not STL/Loess: one pass classical MA; even period (m+1) MA instead of 2×m |
| G11 | decomposition.rs:208 | even window sums w+1 divides by w (constant 1 → 1.05) |
| G12 | conformal.rs:139,744,765,815,828,937 | ⌈(n+1)(1-α)⌉>n clamped to max (should be ∞/error); α=0 accepted; NaN residuals unfiltered |
| G13 | conformal.rs:393,772 | asymmetric targets 1-α/2 per side (~40% too wide) — use α/2,1-α/2 of signed residuals |
| G14 | conformal.rs:701,809,330,914 | adaptive ignores calibration difficulty; batch-mean normalisation |
| G15 | conformal.rs:735,878 | "JackknifePlus" = split |
| G16 | upstream conformal per-step :475 | partial_cmp unwrap panics on NaN |
| G17 | fdars ACF period (periods.rs:299) | min lag 3, first local max w/o significance |
| G18 | fdars FFT period | no detrend (trend → period n); odd-n last bin |
| G19 | fdars seasonal strength | wavelet strength always 1.0; variance/spectral no detrend |
| G20 | periods.rs regression-period defaults | 50-candidate grid misses 12 → multiples |
| G21 | periods.rs:563 lomb_scargle | n_frequencies=1 div0; inverted grid when min>max |
| G22 | seasonality.rs:436 | trend strength doc R² returns |r| |
| G23 | gaps.rs:420 detect_frequency | HashMap tie nondeterministic; doc says seconds, is µs |
| G24 | gaps.rs:165 | monthly fill emits 1st-of-month for month-end data |
| G25 | filter.rs:47 is_constant | absolute EPSILON tolerance |
| G26 | imputation.rs:80 | edge NULL fill behaviour undocumented |

Verified correct: split-conformal level, bootstrap seeding/quantile, PELT L2 recursion, FFT freq→period map, Lomb-Scargle formula, ACF, LOCF/NOCB/mean/interp, diff, fill_gaps_fixed, winkler, coverage.

## H. Upstream crates

### anofox-forecast (0.15.3 pinned; 0.15.10 = repo HEAD src)
- Bump 0.15.3→0.15.10: builds unchanged, identical output except MFLES runaway fix (#219), panel NaN guards (0.15.9), Laplace leaf fixes (0.15.7). Do it.
- Unfixed upstream bugs (fix in ~/projects/rust/anofox-forecast, release, then bump):
  - H1 ADF not augmented + step p-value (=C1/C2) → proper ADF (constant / constant+trend), AIC/BIC lag selection, MacKinnon (1994/2010) p-values
  - H2 KPSS p-value bounding (=C3)
  - H3 AutoARIMA: d via var-ratio heuristic not KPSS; D not OCSB/seasonal strength; CSS AIC over varying sample sizes; AIC not AICc. RW picks (0,1,0) 1% vs R 69.5%; stationary AR(0.7) d≥1 98% vs R 19%
  - H4 ETS: loglik/AIC for multiplicative error uses additive variance; objective uses Σln|y| not Σln|ŷ|; seasonal skips first m obs; no usual constraints. MAM α/γ 0.9999 vs R 0.395/0.40
  - H5 ETS intervals σ√h / σ√⌈h/m⌉ — not Hyndman class-1/2/3 analytic variances
  - H6 Theta STM fixed α=0.1 (RW bias); interval formula
  - H7 upstream validation report: TBATS widths 3-8% of statsforecast, MSTL no intervals, GARCH 0.13×, Theta 2× at h≥6
- Verified fine: Croston/SBA/TSB/ADIDA/IMAPA = statsforecast; ARIMA estimation at fixed order ≈ R within 1%; split conformal.

### anofox-regression (=0.5.3 pinned)
- Bump → =0.5.15: compiles unchanged. Fixes OLS pivot scrambling with multi-scale regressors (0.5.13 #26; ts_forecast_exog_by gave −79 vs true −8.8) and O(n²) memory (0.5.14; 1.7GB at 15k rows). Also lifts anofox-forecast's internal use (^0.5.3).
- anofox-regression 0.4.0 in lock comes from fdars-core `linalg` feature (unused functional ridge) → drop `linalg`.

### fdars-core (0.3.0 pinned; 0.47 latest)
- Numerics of used seasonal/detrend fns essentially unchanged to 0.47; LOESS kernel → tricube (0.7.2), Hilbert odd-length fix (0.8.2), NaN-safe sorts.
- Bump: ~30 mechanical compile errors in 4 files (FdMatrix API — structurally prevents B1/B2), features `["parallel"]` (drop linalg), WASM `js` type-checks.
- G17-G19 remain in 0.47 → fix in core wrapper (detrend before FFT, ACF significance, hide/fix wavelet) and/or upstream fdars.

### Toolchain
- Rust 1.86 claim stale (trueno dep needs newer) → update docs/CI MSRV.

## I. Testing debt
Existing unit tests mostly assert lengths only — why none of this was caught. Every fix needs value assertions against R/scipy reference fixtures (checked-in fixture files generated by checked-in R/Python scripts).
