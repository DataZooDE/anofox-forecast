//! Core forecasting library for the anofox-forecast DuckDB extension.
//!
//! This crate provides the Rust implementation of time series analysis
//! and forecasting functions.

pub mod bootstrap;
pub mod calendar;
pub mod changepoint;
pub mod conformal;
pub mod decomposition;
pub mod detrending;
pub mod error;
pub mod features;
pub mod filter;
pub mod forecast;
pub mod gaps;
pub mod imputation;
pub mod metrics;
pub mod missing;
pub mod peaks;
pub mod periods;
pub mod quality;
pub mod seasonality;
pub mod stats;
pub mod stl;
pub mod supsmu;
pub mod validation;

// Shared fixture loader (std + serde_json only) from
// crates/anofox-fcst-core/tests/common/mod.rs, compiled into the library's
// own cfg(test) build so inline #[cfg(test)] blocks (e.g. forecast.rs's
// fit_ols_regression_matches_numpy_lstsq_fixture) can load
// validation/reference/ fixtures without a visibility change to private
// functions (09-03; see 09-RESEARCH.md Open Question 2).
#[cfg(test)]
#[path = "../tests/common/mod.rs"]
mod test_fixtures;

// Re-exports for convenience
pub use bootstrap::{
    bootstrap_intervals, bootstrap_quantiles, BootstrapIntervalsResult, BootstrapQuantilesResult,
};
pub use calendar::{detect_step, future_dates, CalendarStep, ONE_DAY_MICROS};
pub use changepoint::{
    bocpd_trace, detect_changepoints, detect_changepoints_bocpd, robust_noise_scale, BocpdResult,
    BocpdTrace, ChangepointResult, CostFunction,
};
pub use conformal::{
    // New Learn/Apply API (v2)
    conformal_apply,
    conformal_apply_score,
    conformal_calibrate,
    conformal_coverage,
    conformal_evaluate,
    conformal_from_pairs,
    // Legacy API (still available)
    conformal_intervals,
    conformal_learn,
    conformal_predict,
    conformal_predict_adaptive,
    conformal_predict_asymmetric,
    conformal_predict_multi,
    conformal_quantile,
    conformalize,
    interval_width,
    mean_interval_width,
    winkler_score,
    CalibrationProfile,
    ConformalCalibration,
    ConformalEvaluation,
    ConformalInterval,
    ConformalMethod,
    ConformalMultiResult,
    ConformalPerStepResult,
    ConformalResult,
    ConformalStrategy,
    PredictionIntervals,
};
pub use decomposition::{
    classical_decompose, mstl_decompose, mstl_fit, ClassicalDecomposition, ClassicalKind,
    InsufficientDataMode, MstlDecomposition, MstlFit,
};
pub use detrending::{
    decompose, decompose_additive, decompose_multiplicative, detrend, detrend_auto, detrend_diff,
    detrend_linear, detrend_loess, detrend_polynomial, DecomposeMethod, DecomposeResult,
    DetrendMethod, DetrendResult,
};
pub use error::{ForecastError, Result};
pub use features::{extract_features, list_features, validate_feature_params};
pub use filter::{
    diff, diff_aligned, diff_lag, drop_edge_zeros, drop_leading_zeros, drop_trailing_zeros,
    is_constant, is_constant_slice, is_short,
};
pub use forecast::{
    forecast, forecast_explain, forecast_explicit_ensemble, forecast_inspect, forecast_with_exog,
    inspect_auto_ensemble, inspect_explicit_ensemble, list_models, parse_order_triplet,
    parse_seasonal_periods, ExogenousData, ForecastOptions, ForecastOptionsExog, ForecastOutput,
    LaplaceVariant, ModelType,
};
pub use gaps::{
    detect_frequency, fill_forward, fill_gaps, gap_counts, resolve_gap_step, GapCounts,
};
pub use imputation::{
    fill_nulls_backward, fill_nulls_const, fill_nulls_forward, fill_nulls_interpolate,
    fill_nulls_mean,
};
pub use metrics::{bias, coverage, mae, mape, mase, mse, quantile_loss, r2, rmae, rmse, smape};
pub use missing::{
    complete_cases, drop_missing, fill_missing, propagate_missing, trim_edges_reject_interior,
};
pub use peaks::{
    analyze_peak_timing, detect_peaks, detect_peaks_default, get_peak_indices, get_peak_values,
    Peak, PeakDetectionResult, PeakTimingResult,
};
pub use periods::{
    acf, aic_comparison, autoperiod, cfd_autoperiod, detect_multiple_periods_ts, detect_periods,
    detect_periods_with_validation, detrended_periodogram, estimate_period_acf_ts,
    estimate_period_fft_ts, estimate_period_regression_ts, lomb_scargle, lomb_scargle_power,
    matrix_profile_period, regression_period_rss, sazed_period, ssa_period, stl_period,
    AicPeriodResult, AutoperiodResult, DetectedPeriod, LombScargleResult,
    MatrixProfilePeriodResult, MultiPeriodResult, PeriodMethod, SazedPeriodResult,
    SinglePeriodResult, SsaPeriodResult, StlPeriodResult, DEFAULT_TOLERANCE,
};
pub use quality::{
    compute_data_quality, generate_quality_report, DataQuality, QualityReport, QualityThresholds,
};
pub use seasonality::{
    analyze_seasonality, classify_seasonality, detect_amplitude_modulation, detect_seasonality,
    detect_seasonality_changes, instantaneous_period, seasonal_strength,
    seasonal_strength_spectral, seasonal_strength_variance, seasonal_strength_windowed,
    AmplitudeModulationResult, AmplitudeModulationType, ChangeDetectionResult, ChangePointType,
    InstantaneousPeriodResult, SeasonalType, SeasonalityAnalysis, SeasonalityChangePoint,
    SeasonalityClassification, StrengthMethod,
};
pub use stats::{
    compute_ts_stats, compute_ts_stats_with_dates_and_type, stl_strengths, FrequencyType,
    StlStrengths, TsStats,
};
pub use stl::{stl, StlComponents, StlParams};
pub use supsmu::supsmu;

// Statistical validation (Phase 1: STAT-01..03 stationarity, RESID-01..04 residual diagnostics)
pub use validation::{
    adf, classify_stationarity, durbin_watson, jarque_bera, kpss, ljung_box, residual_diagnostics,
    stationarity, CombinedStationarityOut, DurbinWatsonOut, JarqueBeraOut, LjungBoxOut,
    ResidualDiagnosticsOut, StationarityOut,
};
