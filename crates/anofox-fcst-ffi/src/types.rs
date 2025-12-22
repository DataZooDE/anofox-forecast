//! C-compatible type definitions for FFI boundary.
//!
//! These types are designed to be used across the Rust/C++ boundary,
//! following the patterns from anofox-statistics.

use libc::{c_char, c_double, c_int, size_t};

/// Error codes for FFI boundary.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorCode {
    Success = 0,
    NullPointer = 1,
    InvalidInput = 2,
    ComputationError = 3,
    AllocationError = 4,
    InvalidModel = 5,
    InsufficientData = 6,
    InvalidDateFormat = 7,
    InvalidFrequency = 8,
    PanicCaught = 9,
    InternalError = 10,
}

/// Error structure with message buffer for FFI.
#[repr(C)]
pub struct AnofoxError {
    pub code: ErrorCode,
    pub message: [c_char; 256],
}

impl AnofoxError {
    /// Create a success error (no error).
    pub fn success() -> Self {
        Self {
            code: ErrorCode::Success,
            message: [0; 256],
        }
    }

    /// Set an error with code and message.
    pub fn set_error(&mut self, code: ErrorCode, msg: &str) {
        self.code = code;
        let bytes = msg.as_bytes();
        let len = bytes.len().min(255);
        for (i, &b) in bytes[..len].iter().enumerate() {
            self.message[i] = b as c_char;
        }
        self.message[len] = 0; // Null terminator
    }
}

impl Default for AnofoxError {
    fn default() -> Self {
        Self::success()
    }
}

/// Nullable data array for DuckDB integration.
///
/// The validity bitmask follows DuckDB's convention where bit i of validity[i/64]
/// indicates if element i is valid (1) or NULL (0).
#[repr(C)]
pub struct DataArray {
    /// Pointer to the data values
    pub data: *const c_double,
    /// Pointer to validity bitmask (NULL means all valid)
    pub validity: *const u64,
    /// Number of elements
    pub length: size_t,
}

impl DataArray {
    /// Check if element at index is valid (not NULL).
    ///
    /// # Safety
    /// Caller must ensure index < length and validity pointer is valid if not null.
    pub unsafe fn is_valid(&self, index: usize) -> bool {
        if self.validity.is_null() {
            true
        } else {
            let word = *self.validity.add(index / 64);
            (word >> (index % 64)) & 1 == 1
        }
    }
}

/// Date type enumeration for handling different DuckDB date types.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DateType {
    /// Days since epoch (1970-01-01)
    Date = 0,
    /// Microseconds since epoch
    Timestamp = 1,
    /// Generic integer periods
    Integer = 2,
}

/// Date array supporting multiple date types.
#[repr(C)]
pub struct DateArray {
    /// Pointer to date values (interpretation depends on date_type)
    pub data: *const i64,
    /// Pointer to validity bitmask
    pub validity: *const u64,
    /// Number of elements
    pub length: size_t,
    /// Type of date values
    pub date_type: DateType,
}

/// Time series statistics result (24 metrics).
#[repr(C)]
pub struct TsStatsResult {
    /// Total number of observations
    pub length: size_t,
    /// Number of NULL values
    pub n_nulls: size_t,
    /// Number of zero values
    pub n_zeros: size_t,
    /// Number of positive values
    pub n_positive: size_t,
    /// Number of negative values
    pub n_negative: size_t,
    /// Arithmetic mean
    pub mean: c_double,
    /// Median (50th percentile)
    pub median: c_double,
    /// Standard deviation
    pub std_dev: c_double,
    /// Variance
    pub variance: c_double,
    /// Minimum value
    pub min: c_double,
    /// Maximum value
    pub max: c_double,
    /// Range (max - min)
    pub range: c_double,
    /// Sum of all values
    pub sum: c_double,
    /// Skewness
    pub skewness: c_double,
    /// Kurtosis
    pub kurtosis: c_double,
    /// Coefficient of variation
    pub coef_variation: c_double,
    /// First quartile
    pub q1: c_double,
    /// Third quartile
    pub q3: c_double,
    /// Interquartile range
    pub iqr: c_double,
    /// Autocorrelation at lag 1
    pub autocorr_lag1: c_double,
    /// Trend strength (0-1)
    pub trend_strength: c_double,
    /// Seasonality strength (0-1)
    pub seasonality_strength: c_double,
    /// Approximate entropy
    pub entropy: c_double,
    /// Stability measure
    pub stability: c_double,
}

impl Default for TsStatsResult {
    fn default() -> Self {
        Self {
            length: 0,
            n_nulls: 0,
            n_zeros: 0,
            n_positive: 0,
            n_negative: 0,
            mean: f64::NAN,
            median: f64::NAN,
            std_dev: f64::NAN,
            variance: f64::NAN,
            min: f64::NAN,
            max: f64::NAN,
            range: f64::NAN,
            sum: f64::NAN,
            skewness: f64::NAN,
            kurtosis: f64::NAN,
            coef_variation: f64::NAN,
            q1: f64::NAN,
            q3: f64::NAN,
            iqr: f64::NAN,
            autocorr_lag1: f64::NAN,
            trend_strength: f64::NAN,
            seasonality_strength: f64::NAN,
            entropy: f64::NAN,
            stability: f64::NAN,
        }
    }
}

impl From<anofox_fcst_core::TsStats> for TsStatsResult {
    fn from(stats: anofox_fcst_core::TsStats) -> Self {
        Self {
            length: stats.length,
            n_nulls: stats.n_nulls,
            n_zeros: stats.n_zeros,
            n_positive: stats.n_positive,
            n_negative: stats.n_negative,
            mean: stats.mean,
            median: stats.median,
            std_dev: stats.std_dev,
            variance: stats.variance,
            min: stats.min,
            max: stats.max,
            range: stats.range,
            sum: stats.sum,
            skewness: stats.skewness,
            kurtosis: stats.kurtosis,
            coef_variation: stats.coef_variation,
            q1: stats.q1,
            q3: stats.q3,
            iqr: stats.iqr,
            autocorr_lag1: stats.autocorr_lag1,
            trend_strength: stats.trend_strength,
            seasonality_strength: stats.seasonality_strength,
            entropy: stats.entropy,
            stability: stats.stability,
        }
    }
}

/// Forecast result structure.
#[repr(C)]
pub struct ForecastResult {
    /// Point forecasts array
    pub point_forecasts: *mut c_double,
    /// Lower confidence bounds
    pub lower_bounds: *mut c_double,
    /// Upper confidence bounds
    pub upper_bounds: *mut c_double,
    /// In-sample fitted values
    pub fitted_values: *mut c_double,
    /// Residuals
    pub residuals: *mut c_double,
    /// Number of forecast points
    pub n_forecasts: size_t,
    /// Number of fitted values
    pub n_fitted: size_t,
    /// Model name
    pub model_name: [c_char; 64],
    /// AIC (Akaike Information Criterion)
    pub aic: c_double,
    /// BIC (Bayesian Information Criterion)
    pub bic: c_double,
    /// Mean Squared Error
    pub mse: c_double,
}

impl Default for ForecastResult {
    fn default() -> Self {
        Self {
            point_forecasts: std::ptr::null_mut(),
            lower_bounds: std::ptr::null_mut(),
            upper_bounds: std::ptr::null_mut(),
            fitted_values: std::ptr::null_mut(),
            residuals: std::ptr::null_mut(),
            n_forecasts: 0,
            n_fitted: 0,
            model_name: [0; 64],
            aic: f64::NAN,
            bic: f64::NAN,
            mse: f64::NAN,
        }
    }
}

/// Forecast options.
#[repr(C)]
pub struct ForecastOptions {
    /// Model name (null-terminated string)
    pub model: [c_char; 32],
    /// Forecast horizon
    pub horizon: c_int,
    /// Confidence level (0-1)
    pub confidence_level: c_double,
    /// Seasonal period (0 = auto-detect)
    pub seasonal_period: c_int,
    /// Whether to auto-detect seasonality
    pub auto_detect_seasonality: bool,
    /// Include in-sample fitted values
    pub include_fitted: bool,
    /// Include residuals
    pub include_residuals: bool,
}

impl Default for ForecastOptions {
    fn default() -> Self {
        let mut model = [0 as c_char; 32];
        b"auto"
            .iter()
            .enumerate()
            .for_each(|(i, &b)| model[i] = b as c_char);
        Self {
            model,
            horizon: 12,
            confidence_level: 0.95,
            seasonal_period: 0,
            auto_detect_seasonality: true,
            include_fitted: false,
            include_residuals: false,
        }
    }
}

/// Changepoint detection result (PELT algorithm).
#[repr(C)]
pub struct ChangepointResult {
    /// Array of changepoint indices
    pub changepoints: *mut size_t,
    /// Number of changepoints detected
    pub n_changepoints: size_t,
    /// Total cost of segmentation
    pub cost: c_double,
}

impl Default for ChangepointResult {
    fn default() -> Self {
        Self {
            changepoints: std::ptr::null_mut(),
            n_changepoints: 0,
            cost: f64::NAN,
        }
    }
}

/// BOCPD changepoint detection result.
/// C++ API compatible: per-point is_changepoint and changepoint_probability.
#[repr(C)]
pub struct BocpdResult {
    /// Array of is_changepoint flags (one per input point)
    pub is_changepoint: *mut bool,
    /// Array of changepoint probabilities (one per input point)
    pub changepoint_probability: *mut c_double,
    /// Number of input points
    pub n_points: size_t,
    /// Array of changepoint indices (convenience)
    pub changepoint_indices: *mut size_t,
    /// Number of detected changepoints
    pub n_changepoints: size_t,
}

impl Default for BocpdResult {
    fn default() -> Self {
        Self {
            is_changepoint: std::ptr::null_mut(),
            changepoint_probability: std::ptr::null_mut(),
            n_points: 0,
            changepoint_indices: std::ptr::null_mut(),
            n_changepoints: 0,
        }
    }
}

/// Feature extraction result.
#[repr(C)]
pub struct FeaturesResult {
    /// Array of feature values
    pub features: *mut c_double,
    /// Array of feature name pointers
    pub feature_names: *mut *mut c_char,
    /// Number of features
    pub n_features: size_t,
}

impl Default for FeaturesResult {
    fn default() -> Self {
        Self {
            features: std::ptr::null_mut(),
            feature_names: std::ptr::null_mut(),
            n_features: 0,
        }
    }
}

/// Seasonality analysis result.
/// C++ API compatible field names.
#[repr(C)]
pub struct SeasonalityResult {
    /// Array of detected periods (C++ name: detected_periods)
    pub detected_periods: *mut c_int,
    /// Number of detected periods
    pub n_periods: size_t,
    /// Primary (dominant) period (C++ name: primary_period)
    pub primary_period: c_int,
    /// Seasonal strength (0-1)
    pub seasonal_strength: c_double,
    /// Trend strength (0-1)
    pub trend_strength: c_double,
}

impl Default for SeasonalityResult {
    fn default() -> Self {
        Self {
            detected_periods: std::ptr::null_mut(),
            n_periods: 0,
            primary_period: 0,
            seasonal_strength: 0.0,
            trend_strength: 0.0,
        }
    }
}

/// MSTL decomposition result.
#[repr(C)]
pub struct MstlResult {
    /// Trend component
    pub trend: *mut c_double,
    /// Array of seasonal component arrays (one per period)
    pub seasonal_components: *mut *mut c_double,
    /// Remainder (residual) component
    pub remainder: *mut c_double,
    /// Number of observations
    pub n_observations: size_t,
    /// Number of seasonal components
    pub n_seasonal: size_t,
    /// Array of seasonal periods
    pub seasonal_periods: *mut c_int,
}

impl Default for MstlResult {
    fn default() -> Self {
        Self {
            trend: std::ptr::null_mut(),
            seasonal_components: std::ptr::null_mut(),
            remainder: std::ptr::null_mut(),
            n_observations: 0,
            n_seasonal: 0,
            seasonal_periods: std::ptr::null_mut(),
        }
    }
}

/// Data quality result (per-series).
#[repr(C)]
pub struct DataQualityResult {
    /// Structural dimension score
    pub structural_score: c_double,
    /// Temporal dimension score
    pub temporal_score: c_double,
    /// Magnitude dimension score
    pub magnitude_score: c_double,
    /// Behavioral dimension score
    pub behavioral_score: c_double,
    /// Overall quality score
    pub overall_score: c_double,
    /// Number of gaps
    pub n_gaps: size_t,
    /// Number of missing values
    pub n_missing: size_t,
    /// Is constant series
    pub is_constant: bool,
}

impl Default for DataQualityResult {
    fn default() -> Self {
        Self {
            structural_score: f64::NAN,
            temporal_score: f64::NAN,
            magnitude_score: f64::NAN,
            behavioral_score: f64::NAN,
            overall_score: f64::NAN,
            n_gaps: 0,
            n_missing: 0,
            is_constant: false,
        }
    }
}

/// Quality report result.
#[repr(C)]
#[derive(Default)]
pub struct QualityReportResult {
    /// Number of series passing all checks
    pub n_passed: size_t,
    /// Number of series with gap issues
    pub n_gap_issues: size_t,
    /// Number of series with missing value issues
    pub n_missing_issues: size_t,
    /// Number of constant series
    pub n_constant: size_t,
    /// Total series analyzed
    pub n_total: size_t,
}

/// Gap fill result containing dates and values with filled gaps.
#[repr(C)]
pub struct GapFillResult {
    /// Array of filled date values
    pub dates: *mut i64,
    /// Array of filled values
    pub values: *mut c_double,
    /// Validity bitmask for filled values (bit i indicates if values[i] is valid)
    pub validity: *mut u64,
    /// Number of observations after gap filling
    pub length: size_t,
}

impl Default for GapFillResult {
    fn default() -> Self {
        Self {
            dates: std::ptr::null_mut(),
            values: std::ptr::null_mut(),
            validity: std::ptr::null_mut(),
            length: 0,
        }
    }
}

/// Filled values result (for imputation functions that return values with validity).
#[repr(C)]
pub struct FilledValuesResult {
    /// Array of filled values
    pub values: *mut c_double,
    /// Validity bitmask (bit i indicates if values[i] is valid)
    pub validity: *mut u64,
    /// Number of values
    pub length: size_t,
}

impl Default for FilledValuesResult {
    fn default() -> Self {
        Self {
            values: std::ptr::null_mut(),
            validity: std::ptr::null_mut(),
            length: 0,
        }
    }
}
