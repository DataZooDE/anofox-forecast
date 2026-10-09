# Docs / README / Skills Accuracy Audit — 2026-10-09

Read-only audit of README.md, docs/**, plugins/anofox-forecast/skills/*/SKILL.md and plugin.json against
the code on branch fix/statistical-correctness after Phase 9 (anofox-forecast 0.15.10, anofox-regression
0.5.15, fdars-core 0.3, MSRV 1.89). [SQL] = reproduced against build/release (2026-10-09).
"(Pn)" = being corrected in v0.10.0 Phase n (.planning/ROADMAP.md).
Categories: SV stale-version · FC false-claim · RA removed-API · SIG wrong signature.

Policy for this quick task:
- SV / RA / SIG and rejected-parameter docs → fix definitively now.
- FC whose code fix lands in a later phase → describe CURRENT behaviour honestly + "(being corrected in v0.10.0, Phase N)". Do not describe future behaviour as current.

## Versions
| file:line | claim | reality | change | cat |
|---|---|---|---|---|
| skills/{data-prep,eda,models,backtest,detection}/SKILL.md frontmatter `version: 0.15.3` + header "Extension v0.15.3"; plugin.json:4 | extension v0.15.3 | 0.15.3 = crate version. Extension uses CalVer (CHANGELOG.md:7-9, latest tag v2026.09.26); crate now 0.15.10 | "Extension v2026.09.26 · crate anofox-forecast 0.15.10" (plugin.json version: use the extension CalVer) | SV |
| 07-forecasting.md:818,827,845; reference/models/ensemble/ensemble_inspect.md:162,166,170,176,211,295,298,302; models SKILL:438 | "crate 0.15.3 limitation" (non-Mean weights NULL) | still true in 0.15.10 (AutoEnsemble.ensemble private, auto.rs:81,111-116) | "anofox-forecast ≤0.15.10" | SV |
| 11-conformal-prediction.md:664; models SKILL:424 | ts_cv_forecast_by('AutoEnsemble') crashes "in 0.15.3" | still segfaults [SQL] | keep warning, drop version | SV |
| 10-diagnostics.md:144,205 | 'ct'/'n' unavailable "in v0.15.3" | still true (stationarity.rs identical in 0.15.10) | "≤0.15.10" | SV |
| .claude/CLAUDE.md:7,17,45,46,57,58,242 | anofox-forecast v0.15.3 / anofox-regression v0.5.3 | now 0.15.10 / 0.5.15 | update (keep fdars-core 0.3; Dependencies constraint line → reflect v0.10.0 bumps) | SV |
| 07-forecasting.md:529,603 | ensemble intervals/introspection "planned for Phase 6" | shipped (inspect fns exist); native bounds still empty | update | SV |
| reference/models/global_ets.md:107 | "33 models" | README says 36 | unify (36) | SV |

## Diagnostics (docs/api/10-diagnostics.md, README:41-44)
| file:line | claim | reality | change | cat |
|---|---|---|---|---|
| 10:12,47-50 | "Augmented" Dickey-Fuller | upstream compute_adf_statistic (0.15.10 validation/stationarity.rs:190-236) regresses Δy on y₋₁ only; lag only trims sample | "non-augmented DF regression (constant); lags only trim the sample (P11/P17)" | FC |
| 10:64,74 | AIC lag selection | compute_aic returns raw RSS, ignores k; compute_rss ignores lags | "lag chosen by min RSS, not AIC (P11)" | FC |
| 10:155-165 | MacKinnon interpolation "like statsmodels"; rtol parity | 9-step lookup, no interpolation; size 63.5% at 5% | remove parity/interpolation claims; "coarse step-table p-value (P11)" | FC |
| 10:206-207 | KPSS p clamped [0.01,0.10] | extrapolated over [0,1] (also validation.rs:89-90 doc comment) | "unbounded approximation (P11)" | FC |
| 10:230-241 | trend_stationary = ADF-stat & KPSS-nonstat | inverted vs statsmodels convention; constant → non_stationary | flag labels inverted until P17 | FC |
| 10:257 | Ljung-Box df = lags | accurate; no fitted-params df correction | add caveat (P17) | misleading |
| 10:9 | order via ORDER BY | interior NULLs silently spliced out (diagnostics.cpp:31-45) | document (P12) | omission |
| README:44 | "numerically cross-validated against statsmodels" | false for ADF/KPSS p-values | remove/restrict to verified tests (Ljung-Box, JB, DW, KPSS statistic) | FC |

## Forecasting (07-forecasting.md, docs/reference/models/**, models SKILL)
| file:line | claim | reality | change | cat |
|---|---|---|---|---|
| 07:250-251; 08:192; guides/03:49,273; all reference/models yhat_lower/upper rows | model-based prediction intervals | all but GARCH/Kalman/AutoEnsemble: point ± z·popSD(raw series)·√h with step z-table (forecast.rs:798,1003,3414-3446); crate intervals discarded | "heuristic ±z·SD(history)·√h; not calibrated — use conformal (P10)"; guides/03:273 "~90% coverage" false | FC |
| 07:200 | Laplace distributional intervals | overwritten by same formula | say so (P10) | FC |
| 07:179; models SKILL:527; reference/state-space/arima.md:3,17,24-43,58-80; README:32,269 | ARIMA with explicit (p,d,q)(P,D,Q,s) | p/d/q keys rejected (ts_forecast_native.cpp:277-313) [SQL]; forecast_arima = AR(1) on first diffs, fixed coef 0.5, Naive if n<5 | "fixed heuristic, no order params; use AutoARIMA (P10)" | FC |
| ~30 per-series reference/models pages (Parameters tables ~:24-33); 07:170-195; models SKILL:478-552 | include_fitted, include_residuals, alpha, beta, gamma, theta, alpha_d, alpha_p, use_box_cox, stl_method, iterations | not valid keys (ValidateParamKeys :278-283); examples in holt/holt_winters/ses/seasonal_es/tsb/tbats/theta/dynamic_theta/ses_optimized/seasonal_es_optimized/croston_optimized fail at bind | remove; valid keys = model, seasonal_period, seasonal_periods, confidence_level, window, model_pool, laplace_variant, laplace_seasonal_batch_init, garch_p, garch_q, kalman_model, top_k, combination_method | FC |
| same pages | confidence_level default 0.95 | 0.90 (ts_forecast_native.cpp:44,357) | 0.90 | FC |
| 07:143,186; SKILL:467; auto_mfles.md, auto_mstl.md | auto-selection | same path as MFLES/MSTL (forecast.rs:696-716) | "currently an alias (P10)" | FC |
| 07:178; SKILL:464 | AutoETS | crate error → silent HW/Holt/SES fallback still labelled AutoETS | note (P10) | FC |
| reference/state-space/ets.md:24 | default 'AAA' | no spec → HW else Holt else SES(α=0.3) | describe (P10) | FC |
| 07:283-289; SKILL:171 | ARIMAX/ThetaX | OLS + fixed AR(1) on residuals; plain Theta | "not a joint fit (P10)" | FC |
| SKILL:448 | insample_fitted | SES(α=0.3) surrogate for all but Naive/SNaive/SWA; AutoARIMA = Theta output [SQL] | say so (P10) | FC |
| reference mstl/tbats/auto_*; SKILL:17,280; ~10 pages "Seasonality NOT auto-detected" | periods required, no auto-detect | auto-detected when omitted (forecast.rs:603-607; native :647); MSTL/TBATS fallback [12] | fix | FC |

## Metrics & conformal (09, 11, backtest SKILL, README:51)
| file:line | claim | reality | change | cat |
|---|---|---|---|---|
| 09:11,70,112; backtest SKILL:8,122 | MASE scaled by in-sample seasonal naive | MAE/MAE(baseline) ≡ rmae (metrics.rs:165-187) | "relative MAE (P13)" | FC |
| 09:66; SKILL:118 | MAPE | zero actuals dropped, denominator shrinks | document (P13) | FC |
| 11:322 | calibrate coverage empirical | nominal 1−α (ts_macros.cpp:1716) | "nominal" | FC |
| 11:240-292; SKILL:162-178 | per-horizon intervals | LIST(... ORDER BY point_forecast) destroys horizon order (ts_macros.cpp:1653,1735) | warn (P16) | FC |
| 11:139,291,503 | asymmetric "tighter" | ~40% too wide (stat audit G13) | remove claim (P16) | FC |
| 11:618,626-631 | strategies naive/cv/adaptive | valid split/crossval/jackknife+ (conformal.rs:518-525); naive/adaptive silently → split [SQL]; jackknife+ ≡ split | fix | FC |
| README:51; backtest SKILL:150,266 | "guaranteed coverage" | not given ordering/pooling issues | soften (P16) | FC |
| 01-table-macros:52,60 | ts_diff, ts_conformal | not registered | ts_diff_by / ts_conformal_by | RA |

## Stats / detection / data prep (03, 04, 05*, 06, 02, skills)
| file:line | claim | reality | change | cat |
|---|---|---|---|---|
| 04:306-321; data-prep SKILL:91 | diff_order = d-th order differencing | LAG(v,d) lag-d difference [SQL] | "y[t]−y[t−k] (P12)" | FC |
| 05a:62,76-87; detection SKILL:266-276; data-prep SKILL:92 | ts_detrend_by works | fdars arg swap → trend=0, output=input [SQL]; 'quadratic'/'cubic' → auto | "currently a no-op (P15)" | FC |
| 05a:23-47; detection SKILL:248-283 | MSTL with Loess, seasonal periods | native passes nullptr periods (ts_mstl_decomposition_native.cpp:296; ffi lib.rs:2897); classical MA trend only [SQL] | "no seasonal component returned (P15)" | FC/SIG |
| 05a:137-138; detection SKILL:202,295 | label sets; classification/confidence cols | real labels seasonality.rs:101-107,266-275; arg swap → sine = non_seasonal/NaN [SQL] | fix labels + no-op note (P15) | FC |
| 03:87 | approximate entropy | 10-bin Shannon | rename (P13) | FC |
| 03:85-86; EDA SKILL:325 | strengths 0-1, >0.6 strong | |r| linear fit; max|ACF| lags 2/4/7/12 | describe formulas, drop thresholds (P13) | FC |
| 03:90,106-114 | n_gaps = missing periods | counts gap runs [SQL] | fix (P13) | FC |
| 03:127-149; EDA SKILL:79-94 | temporal_score/n_gaps meaningful | dates None → always 1.0/0; n_short/frequency ignored [SQL] | "placeholders (P13)" | FC |
| 03:75-76 | skew/kurt G1/G2 | biased mix | caveat (P13) | FC |
| 05b:26,47-57 | nested STRUCT output | flat columns | fix | SIG |
| 06:101-104,129; detection SKILL:181,290 | value col in output; alias claim | output group/date/is_changepoint/prob only (ts_changepoints.cpp:519-528); single-series macro differs | fix | SIG |
| data-prep SKILL:146,149 | ts_fill_gaps_by(..., {tolerance}) | 5 args only [SQL Binder Error] | remove | SIG |
| EDA SKILL:344 | q.product_id | column is unique_id | fix | SIG |
| 02:178 | values summed | NULL→0 (ts_aggregate_hierarchy.cpp:291) | document (P12) | misleading |

## Features (20, 21, README:60,261, EDA SKILL:6,256)
"tsfresh-compatible" false for: PACF lag3-5 (features.rs:506), mean_second_derivative_central (:146), first/last_location (:131), time_reversal_asymmetry (:911), fft_coefficient (:978), spectral centroid/variance (:998), agg_linear_trend rvalue (:1074), skewness/kurtosis (:66), sample_entropy (:709), binned_entropy (:658), benford (:561), permutation_entropy (:806), number_peaks threshold (:696) → "tsfresh-inspired; listed features deviate (P14)".
21-feature-reference formula text contradicting code: :249 (fft_coefficient_0 = mean, not sum), :111 (deviation from mean, not prominence), :158 (power-weighted two-sided incl. DC), :171 (rvalue unsigned), :201-205 (PACF ≥3 = lag 2).

## Verified accurate (leave)
ts_backtest_auto_by only as "REMOVED" note; 0.90 default in 07 + models skill; GARCH/Kalman/AutoEnsemble NULL bounds; signatures of ts_forecast_by, ts_cv_*, ts_conformal_*, ts_fill_gaps_by, ts_drop_*, ts_fill_nulls_*, ts_detect_peaks_by params, ts_classify_seasonality_by, ts_analyze_peak_timing_by; Ljung-Box default lags min(10,n/5); DW range/labels; "117 features"; ts_detect_periods_by NULL-row note; ts_detect_changepoints_by signature + hazard 250; README MSRV 1.89.

## Unverified (check before editing or leave untouched)
MSTL forecaster Loess (crate path); 05 period-detection max_period defaults; EDA skill "116 columns" vs 117; asymmetric width (stat audit, not re-measured); BOCPD "never fires" (not reproduced — a step test detected the break; do NOT claim it never fires).
