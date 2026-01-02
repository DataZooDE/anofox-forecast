//! Core forecasting library for the anofox-forecast DuckDB extension.
//!
//! This crate provides the Rust implementation of time series analysis
//! and forecasting functions.

pub mod changepoint;
pub mod decomposition;
pub mod error;
pub mod features;
pub mod filter;
pub mod forecast;
pub mod gaps;
pub mod imputation;
pub mod metrics;
pub mod quality;
pub mod seasonality;
pub mod stats;

// Re-exports for convenience
pub use changepoint::{
    detect_changepoints, detect_changepoints_bocpd, BocpdResult, ChangepointResult, CostFunction,
};
pub use decomposition::{mstl_decompose, InsufficientDataMode, MstlDecomposition};
pub use error::{ForecastError, Result};
pub use features::{extract_features, list_features, validate_feature_params};
pub use filter::{
    diff, drop_edge_zeros, drop_leading_zeros, drop_trailing_zeros, is_constant, is_short,
};
pub use forecast::{forecast, list_models, ForecastOptions, ForecastOutput, ModelType};
pub use gaps::{detect_frequency, fill_forward, fill_gaps};
pub use imputation::{
    fill_nulls_backward, fill_nulls_const, fill_nulls_forward, fill_nulls_interpolate,
    fill_nulls_mean,
};
pub use metrics::{
    bias, coverage, mae, mape, mase, mqloss, mse, quantile_loss, r2, rmae, rmse, smape,
};
pub use quality::{
    compute_data_quality, generate_quality_report, DataQuality, QualityReport, QualityThresholds,
};
pub use seasonality::{analyze_seasonality, detect_seasonality, SeasonalityAnalysis};
pub use stats::{compute_ts_stats, TsStats};
