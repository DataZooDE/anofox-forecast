//! Core forecasting library for the anofox-forecast DuckDB extension.
//!
//! This crate provides the Rust implementation of time series analysis
//! and forecasting functions.

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
pub mod peaks;
pub mod periods;
pub mod quality;
pub mod seasonality;
pub mod stats;

// Re-exports for convenience
pub use changepoint::{
    detect_changepoints, detect_changepoints_bocpd, BocpdResult, ChangepointResult, CostFunction,
};
pub use conformal::{
    // New Learn/Apply API (v2)
    conformal_apply,
    conformal_coverage,
    conformal_evaluate,
    // Legacy API (still available)
    conformal_intervals,
    conformal_learn,
    conformal_predict,
    conformal_predict_adaptive,
    conformal_predict_asymmetric,
    conformal_predict_multi,
    conformal_quantile,
    conformalize,
    difficulty_score,
    interval_width,
    mean_interval_width,
    winkler_score,
    CalibrationProfile,
    ConformalEvaluation,
    ConformalInterval,
    ConformalMethod,
    ConformalMultiResult,
    ConformalResult,
    ConformalStrategy,
    DifficultyMethod,
    PredictionIntervals,
};
pub use decomposition::{mstl_decompose, InsufficientDataMode, MstlDecomposition};
pub use detrending::{
    decompose, decompose_additive, decompose_multiplicative, detrend, detrend_auto, detrend_diff,
    detrend_linear, detrend_loess, detrend_polynomial, DecomposeMethod, DecomposeResult,
    DetrendMethod, DetrendResult,
};
pub use error::{ForecastError, Result};
pub use features::{extract_features, list_features, validate_feature_params};
pub use filter::{
    diff, drop_edge_zeros, drop_leading_zeros, drop_trailing_zeros, is_constant, is_short,
};
pub use forecast::{
    forecast, forecast_with_exog, list_models, ExogenousData, ForecastOptions, ForecastOptionsExog,
    ForecastOutput, ModelType,
};
pub use gaps::{detect_frequency, fill_forward, fill_gaps};
pub use imputation::{
    fill_nulls_backward, fill_nulls_const, fill_nulls_forward, fill_nulls_interpolate,
    fill_nulls_mean,
};
pub use metrics::{
    bias, coverage, mae, mape, mase, mqloss, mse, quantile_loss, r2, rmae, rmse, smape,
};
pub use peaks::{
    analyze_peak_timing, detect_peaks, detect_peaks_default, get_peak_indices, get_peak_values,
    Peak, PeakDetectionResult, PeakTimingResult,
};
pub use periods::{
    aic_comparison, autoperiod, cfd_autoperiod, detect_multiple_periods_ts, detect_periods,
    detect_periods_with_validation, estimate_period_acf_ts, estimate_period_fft_ts,
    estimate_period_regression_ts, lomb_scargle, matrix_profile_period, sazed_period, ssa_period,
    stl_period, AicPeriodResult, AutoperiodResult, DetectedPeriod, LombScargleResult,
    MatrixProfilePeriodResult, MultiPeriodResult, PeriodMethod, SazedPeriodResult,
    SinglePeriodResult, SsaPeriodResult, StlPeriodResult, DEFAULT_TOLERANCE,
};
pub use quality::{
    compute_data_quality, generate_quality_report, DataQuality, QualityReport, QualityThresholds,
};
pub use seasonality::{
    analyze_seasonality, classify_seasonality, detect_amplitude_modulation, detect_seasonality,
    detect_seasonality_changes, instantaneous_period, seasonal_strength,
    seasonal_strength_spectral, seasonal_strength_variance, seasonal_strength_wavelet,
    seasonal_strength_windowed, AmplitudeModulationResult, AmplitudeModulationType,
    ChangeDetectionResult, ChangePointType, InstantaneousPeriodResult, SeasonalType,
    SeasonalityAnalysis, SeasonalityChangePoint, SeasonalityClassification, StrengthMethod,
};
pub use stats::{
    compute_ts_stats, compute_ts_stats_with_dates, compute_ts_stats_with_dates_and_type,
    FrequencyType, TsStats,
};
