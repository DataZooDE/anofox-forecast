//! C-compatible type definitions for FFI boundary.
//!
//! These types are designed to be used across the Rust/C++ boundary,
//! following the patterns from anofox-statistics.

// Use core::ffi types for cross-platform compatibility including WASM
use core::ffi::{c_char, c_double, c_int};

// size_t is not in core::ffi, use usize instead
#[allow(non_camel_case_types)]
type size_t = usize;

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

/// Exogenous regressor data for a single regressor.
///
/// Each regressor is a column of values aligned with the time series observations.
#[repr(C)]
pub struct ExogenousRegressor {
    /// Pointer to the regressor values (historical, aligned with y values)
    pub values: *const c_double,
    /// Number of historical values (must match y length)
    pub n_values: size_t,
    /// Pointer to future regressor values (for forecast horizon)
    pub future_values: *const c_double,
    /// Number of future values (must match horizon)
    pub n_future: size_t,
}

/// Exogenous data containing multiple regressors.
///
/// Used to pass historical X and future X values across the FFI boundary.
/// Layout: regressors[i] contains the i-th regressor's historical and future values.
#[repr(C)]
pub struct ExogenousData {
    /// Array of regressors
    pub regressors: *const ExogenousRegressor,
    /// Number of regressors
    pub n_regressors: size_t,
}

impl ExogenousData {
    /// Check if exogenous data is empty (no regressors).
    pub fn is_empty(&self) -> bool {
        self.regressors.is_null() || self.n_regressors == 0
    }
}

/// Forecast options with exogenous variables support.
#[repr(C)]
pub struct ForecastOptionsExog {
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
    /// Exogenous data (may be null if no exogenous variables)
    pub exog: *const ExogenousData,
}

impl Default for ForecastOptionsExog {
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
            exog: std::ptr::null(),
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
    /// Trend component (may be NULL if decomposition was skipped)
    pub trend: *mut c_double,
    /// Array of seasonal component arrays (one per period)
    pub seasonal_components: *mut *mut c_double,
    /// Remainder (residual) component (may be NULL if decomposition was skipped)
    pub remainder: *mut c_double,
    /// Number of observations
    pub n_observations: size_t,
    /// Number of seasonal components
    pub n_seasonal: size_t,
    /// Array of seasonal periods
    pub seasonal_periods: *mut c_int,
    /// Whether decomposition was actually applied
    pub decomposition_applied: bool,
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
            decomposition_applied: false,
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
    /// Validity bitmask for filled values (bit `i` indicates if `values[i]` is valid)
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
    /// Validity bitmask (bit `i` indicates if `values[i]` is valid)
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

// ============================================================================
// Period Detection Types (fdars-core integration)
// ============================================================================

/// Result from single period estimation.
#[repr(C)]
pub struct SinglePeriodResult {
    /// Estimated period (in samples)
    pub period: c_double,
    /// Dominant frequency (1/period)
    pub frequency: c_double,
    /// Power at the dominant frequency
    pub power: c_double,
    /// Confidence measure
    pub confidence: c_double,
    /// Method used for estimation
    pub method: [c_char; 32],
}

impl Default for SinglePeriodResult {
    fn default() -> Self {
        Self {
            period: 0.0,
            frequency: 0.0,
            power: 0.0,
            confidence: 0.0,
            method: [0; 32],
        }
    }
}

/// A detected period from multiple period detection.
#[repr(C)]
pub struct DetectedPeriodFFI {
    /// Estimated period (in samples)
    pub period: c_double,
    /// Confidence measure
    pub confidence: c_double,
    /// Seasonal strength at this period
    pub strength: c_double,
    /// Amplitude of the sinusoidal component
    pub amplitude: c_double,
    /// Phase of the sinusoidal component (radians)
    pub phase: c_double,
    /// Iteration number (1-indexed)
    pub iteration: size_t,
}

/// Result from multiple period detection.
#[repr(C)]
pub struct MultiPeriodResult {
    /// Array of detected periods
    pub periods: *mut DetectedPeriodFFI,
    /// Number of detected periods
    pub n_periods: size_t,
    /// Primary (strongest) period
    pub primary_period: c_double,
    /// Method used for estimation
    pub method: [c_char; 32],
}

impl Default for MultiPeriodResult {
    fn default() -> Self {
        Self {
            periods: std::ptr::null_mut(),
            n_periods: 0,
            primary_period: 0.0,
            method: [0; 32],
        }
    }
}

/// Flattened result from multiple period detection.
///
/// Uses parallel arrays instead of nested struct array for safer FFI.
/// This avoids memory management issues when crossing the Rust/C++ boundary,
/// particularly with R's DuckDB bindings.
#[repr(C)]
pub struct FlatMultiPeriodResult {
    /// Array of period values (in samples)
    pub period_values: *mut c_double,
    /// Array of confidence values
    pub confidence_values: *mut c_double,
    /// Array of strength values
    pub strength_values: *mut c_double,
    /// Array of amplitude values
    pub amplitude_values: *mut c_double,
    /// Array of phase values (radians)
    pub phase_values: *mut c_double,
    /// Array of iteration values (1-indexed)
    pub iteration_values: *mut size_t,
    /// Number of detected periods
    pub n_periods: size_t,
    /// Primary (strongest) period
    pub primary_period: c_double,
    /// Method used for estimation
    pub method: [c_char; 32],
}

impl Default for FlatMultiPeriodResult {
    fn default() -> Self {
        Self {
            period_values: std::ptr::null_mut(),
            confidence_values: std::ptr::null_mut(),
            strength_values: std::ptr::null_mut(),
            amplitude_values: std::ptr::null_mut(),
            phase_values: std::ptr::null_mut(),
            iteration_values: std::ptr::null_mut(),
            n_periods: 0,
            primary_period: 0.0,
            method: [0; 32],
        }
    }
}

/// Result from autoperiod detection.
///
/// Combines FFT period estimation with ACF validation.
#[repr(C)]
pub struct AutoperiodResultFFI {
    /// Detected period (in samples)
    pub period: c_double,
    /// FFT confidence (ratio of peak power to mean power)
    pub fft_confidence: c_double,
    /// ACF validation score (correlation at the detected period)
    pub acf_validation: c_double,
    /// Whether the period was detected (acf_validation > threshold)
    pub detected: bool,
    /// Method used ("autoperiod" or "cfd_autoperiod")
    pub method: [c_char; 32],
}

impl Default for AutoperiodResultFFI {
    fn default() -> Self {
        Self {
            period: 0.0,
            fft_confidence: 0.0,
            acf_validation: 0.0,
            detected: false,
            method: [0; 32],
        }
    }
}

/// Result from Lomb-Scargle periodogram.
///
/// Lomb-Scargle is optimal for detecting periodic signals in unevenly
/// sampled data and provides statistical significance via false alarm probability.
#[repr(C)]
pub struct LombScargleResultFFI {
    /// Detected period (in samples)
    pub period: c_double,
    /// Frequency corresponding to the peak
    pub frequency: c_double,
    /// Power at the peak frequency (normalized)
    pub power: c_double,
    /// False alarm probability (lower = more significant)
    pub false_alarm_prob: c_double,
    /// Method identifier
    pub method: [c_char; 32],
}

impl Default for LombScargleResultFFI {
    fn default() -> Self {
        Self {
            period: 0.0,
            frequency: 0.0,
            power: 0.0,
            false_alarm_prob: 1.0,
            method: [0; 32],
        }
    }
}

/// Result from AIC-based period comparison.
#[repr(C)]
pub struct AicPeriodResultFFI {
    /// Best period according to AIC
    pub period: c_double,
    /// AIC value for the best model
    pub aic: c_double,
    /// BIC value for the best model
    pub bic: c_double,
    /// Residual sum of squares for the best model
    pub rss: c_double,
    /// R-squared for the best model
    pub r_squared: c_double,
    /// Method identifier
    pub method: [c_char; 32],
}

impl Default for AicPeriodResultFFI {
    fn default() -> Self {
        Self {
            period: 0.0,
            aic: 0.0,
            bic: 0.0,
            rss: 0.0,
            r_squared: 0.0,
            method: [0; 32],
        }
    }
}

/// Result from SSA period detection.
#[repr(C)]
pub struct SsaPeriodResultFFI {
    /// Primary detected period
    pub period: c_double,
    /// Variance explained by the primary periodic component
    pub variance_explained: c_double,
    /// Number of eigenvalues returned
    pub n_eigenvalues: size_t,
    /// Method identifier
    pub method: [c_char; 32],
}

impl Default for SsaPeriodResultFFI {
    fn default() -> Self {
        Self {
            period: 0.0,
            variance_explained: 0.0,
            n_eigenvalues: 0,
            method: [0; 32],
        }
    }
}

/// Result from STL-based period detection.
#[repr(C)]
pub struct StlPeriodResultFFI {
    /// Best detected period
    pub period: c_double,
    /// Seasonal strength at the best period (0-1)
    pub seasonal_strength: c_double,
    /// Trend strength (0-1)
    pub trend_strength: c_double,
    /// Method identifier
    pub method: [c_char; 32],
}

impl Default for StlPeriodResultFFI {
    fn default() -> Self {
        Self {
            period: 0.0,
            seasonal_strength: 0.0,
            trend_strength: 0.0,
            method: [0; 32],
        }
    }
}

/// Result from Matrix Profile period detection.
#[repr(C)]
pub struct MatrixProfilePeriodResultFFI {
    /// Detected period (most common motif distance)
    pub period: c_double,
    /// Confidence based on peak prominence in lag histogram
    pub confidence: c_double,
    /// Number of motif pairs found
    pub n_motifs: size_t,
    /// Subsequence length used
    pub subsequence_length: size_t,
    /// Method identifier
    pub method: [c_char; 32],
}

impl Default for MatrixProfilePeriodResultFFI {
    fn default() -> Self {
        Self {
            period: 0.0,
            confidence: 0.0,
            n_motifs: 0,
            subsequence_length: 0,
            method: [0; 32],
        }
    }
}

/// Result from SAZED period detection.
#[repr(C)]
pub struct SazedPeriodResultFFI {
    /// Primary detected period
    pub period: c_double,
    /// Spectral power at the detected period
    pub power: c_double,
    /// Signal-to-noise ratio
    pub snr: c_double,
    /// Method identifier
    pub method: [c_char; 32],
}

impl Default for SazedPeriodResultFFI {
    fn default() -> Self {
        Self {
            period: 0.0,
            power: 0.0,
            snr: 0.0,
            method: [0; 32],
        }
    }
}

// ============================================================================
// Peak Detection Types (fdars-core integration)
// ============================================================================

/// A detected peak in the time series.
#[repr(C)]
pub struct PeakFFI {
    /// Index at which the peak occurs
    pub index: size_t,
    /// Time at which the peak occurs
    pub time: c_double,
    /// Value at the peak
    pub value: c_double,
    /// Prominence of the peak
    pub prominence: c_double,
}

/// Result of peak detection.
#[repr(C)]
pub struct PeakDetectionResultFFI {
    /// Array of detected peaks
    pub peaks: *mut PeakFFI,
    /// Number of peaks detected
    pub n_peaks: size_t,
    /// Inter-peak distances
    pub inter_peak_distances: *mut c_double,
    /// Number of inter-peak distances
    pub n_distances: size_t,
    /// Mean period estimated from inter-peak distances
    pub mean_period: c_double,
}

impl Default for PeakDetectionResultFFI {
    fn default() -> Self {
        Self {
            peaks: std::ptr::null_mut(),
            n_peaks: 0,
            inter_peak_distances: std::ptr::null_mut(),
            n_distances: 0,
            mean_period: 0.0,
        }
    }
}

/// Result of peak timing variability analysis.
#[repr(C)]
pub struct PeakTimingResultFFI {
    /// Peak times for each cycle
    pub peak_times: *mut c_double,
    /// Peak values
    pub peak_values: *mut c_double,
    /// Normalized timing (0-1 scale)
    pub normalized_timing: *mut c_double,
    /// Number of peaks
    pub n_peaks: size_t,
    /// Mean normalized timing
    pub mean_timing: c_double,
    /// Standard deviation of normalized timing
    pub std_timing: c_double,
    /// Range of normalized timing
    pub range_timing: c_double,
    /// Variability score (0 = stable, 1 = highly variable)
    pub variability_score: c_double,
    /// Trend in timing
    pub timing_trend: c_double,
    /// Whether timing is considered stable
    pub is_stable: bool,
}

impl Default for PeakTimingResultFFI {
    fn default() -> Self {
        Self {
            peak_times: std::ptr::null_mut(),
            peak_values: std::ptr::null_mut(),
            normalized_timing: std::ptr::null_mut(),
            n_peaks: 0,
            mean_timing: 0.0,
            std_timing: 0.0,
            range_timing: 0.0,
            variability_score: 0.0,
            timing_trend: 0.0,
            is_stable: false,
        }
    }
}

// ============================================================================
// Detrending Types (fdars-core integration)
// ============================================================================

/// Result of detrending operation.
#[repr(C)]
pub struct DetrendResultFFI {
    /// Estimated trend values
    pub trend: *mut c_double,
    /// Detrended data
    pub detrended: *mut c_double,
    /// Number of values
    pub length: size_t,
    /// Method used for detrending
    pub method: [c_char; 32],
    /// Polynomial coefficients (may be NULL)
    pub coefficients: *mut c_double,
    /// Number of coefficients
    pub n_coefficients: size_t,
    /// Residual sum of squares
    pub rss: c_double,
    /// Number of parameters
    pub n_params: size_t,
}

impl Default for DetrendResultFFI {
    fn default() -> Self {
        Self {
            trend: std::ptr::null_mut(),
            detrended: std::ptr::null_mut(),
            length: 0,
            method: [0; 32],
            coefficients: std::ptr::null_mut(),
            n_coefficients: 0,
            rss: 0.0,
            n_params: 0,
        }
    }
}

/// Result of seasonal decomposition.
#[repr(C)]
pub struct DecomposeResultFFI {
    /// Trend component
    pub trend: *mut c_double,
    /// Seasonal component
    pub seasonal: *mut c_double,
    /// Remainder/residual component
    pub remainder: *mut c_double,
    /// Number of observations
    pub length: size_t,
    /// Period used for decomposition
    pub period: c_double,
    /// Decomposition method ("additive" or "multiplicative")
    pub method: [c_char; 32],
}

impl Default for DecomposeResultFFI {
    fn default() -> Self {
        Self {
            trend: std::ptr::null_mut(),
            seasonal: std::ptr::null_mut(),
            remainder: std::ptr::null_mut(),
            length: 0,
            period: 0.0,
            method: [0; 32],
        }
    }
}

// ============================================================================
// Extended Seasonality Types (fdars-core integration)
// ============================================================================

/// Result of seasonality classification.
#[repr(C)]
pub struct SeasonalityClassificationFFI {
    /// Whether series is classified as seasonal
    pub is_seasonal: bool,
    /// Whether timing is stable across cycles
    pub has_stable_timing: bool,
    /// Timing variability measure
    pub timing_variability: c_double,
    /// Overall seasonal strength
    pub seasonal_strength: c_double,
    /// Per-cycle seasonal strength
    pub cycle_strengths: *mut c_double,
    /// Number of cycle strengths
    pub n_cycle_strengths: size_t,
    /// Indices of weak/missing seasons
    pub weak_seasons: *mut size_t,
    /// Number of weak seasons
    pub n_weak_seasons: size_t,
    /// Classification type (stable, variable, intermittent, absent)
    pub classification: [c_char; 32],
}

impl Default for SeasonalityClassificationFFI {
    fn default() -> Self {
        Self {
            is_seasonal: false,
            has_stable_timing: false,
            timing_variability: 0.0,
            seasonal_strength: 0.0,
            cycle_strengths: std::ptr::null_mut(),
            n_cycle_strengths: 0,
            weak_seasons: std::ptr::null_mut(),
            n_weak_seasons: 0,
            classification: [0; 32],
        }
    }
}

/// Seasonality change point.
#[repr(C)]
pub struct SeasonalityChangePointFFI {
    /// Index at which change occurs
    pub index: size_t,
    /// Time at which change occurs
    pub time: c_double,
    /// Type of change (onset, cessation)
    pub change_type: [c_char; 32],
    /// Strength before change
    pub strength_before: c_double,
    /// Strength after change
    pub strength_after: c_double,
}

/// Result of seasonality change detection.
#[repr(C)]
pub struct ChangeDetectionResultFFI {
    /// Array of detected change points
    pub change_points: *mut SeasonalityChangePointFFI,
    /// Number of change points
    pub n_changes: size_t,
    /// Time-varying seasonal strength curve
    pub strength_curve: *mut c_double,
    /// Number of strength curve values
    pub n_strength_curve: size_t,
}

impl Default for ChangeDetectionResultFFI {
    fn default() -> Self {
        Self {
            change_points: std::ptr::null_mut(),
            n_changes: 0,
            strength_curve: std::ptr::null_mut(),
            n_strength_curve: 0,
        }
    }
}

/// Result of instantaneous period estimation.
#[repr(C)]
pub struct InstantaneousPeriodResultFFI {
    /// Instantaneous period at each time point
    pub periods: *mut c_double,
    /// Instantaneous frequency at each time point
    pub frequencies: *mut c_double,
    /// Instantaneous amplitude (envelope) at each time point
    pub amplitudes: *mut c_double,
    /// Number of values
    pub length: size_t,
}

impl Default for InstantaneousPeriodResultFFI {
    fn default() -> Self {
        Self {
            periods: std::ptr::null_mut(),
            frequencies: std::ptr::null_mut(),
            amplitudes: std::ptr::null_mut(),
            length: 0,
        }
    }
}

/// Result of amplitude modulation detection.
#[repr(C)]
pub struct AmplitudeModulationResultFFI {
    /// Whether seasonality is present
    pub is_seasonal: bool,
    /// Overall seasonal strength
    pub seasonal_strength: c_double,
    /// Whether amplitude modulation is detected
    pub has_modulation: bool,
    /// Modulation type (stable, emerging, fading, oscillating, non_seasonal)
    pub modulation_type: [c_char; 32],
    /// Coefficient of variation of wavelet amplitude
    pub modulation_score: c_double,
    /// Trend in amplitude (-1 to 1)
    pub amplitude_trend: c_double,
    /// Wavelet amplitude at the seasonal frequency over time
    pub wavelet_amplitude: *mut c_double,
    /// Time points corresponding to wavelet_amplitude
    pub time_points: *mut c_double,
    /// Number of amplitude/time values
    pub n_points: size_t,
    /// Scale (period) used for wavelet analysis
    pub scale: c_double,
}

impl Default for AmplitudeModulationResultFFI {
    fn default() -> Self {
        Self {
            is_seasonal: false,
            seasonal_strength: 0.0,
            has_modulation: false,
            modulation_type: [0; 32],
            modulation_score: 0.0,
            amplitude_trend: 0.0,
            wavelet_amplitude: std::ptr::null_mut(),
            time_points: std::ptr::null_mut(),
            n_points: 0,
            scale: 0.0,
        }
    }
}

// ============================================================================
// Conformal Prediction Types
// ============================================================================

/// Result of conformal prediction with prediction intervals.
#[repr(C)]
pub struct ConformalResultFFI {
    /// Point forecasts
    pub point: *mut c_double,
    /// Lower bounds of prediction intervals
    pub lower: *mut c_double,
    /// Upper bounds of prediction intervals
    pub upper: *mut c_double,
    /// Number of forecasts
    pub n_forecasts: size_t,
    /// Nominal coverage level (1 - alpha)
    pub coverage: c_double,
    /// The computed conformity score (quantile threshold)
    pub conformity_score: c_double,
    /// Method used for conformal prediction
    pub method: [c_char; 32],
}

impl Default for ConformalResultFFI {
    fn default() -> Self {
        Self {
            point: std::ptr::null_mut(),
            lower: std::ptr::null_mut(),
            upper: std::ptr::null_mut(),
            n_forecasts: 0,
            coverage: 0.0,
            conformity_score: 0.0,
            method: [0; 32],
        }
    }
}

/// A single prediction interval at a specific coverage level (for multi-level results).
#[repr(C)]
pub struct ConformalIntervalFFI {
    /// Nominal coverage level (1 - alpha)
    pub coverage: c_double,
    /// Lower bounds
    pub lower: *mut c_double,
    /// Upper bounds
    pub upper: *mut c_double,
    /// Conformity score used
    pub conformity_score: c_double,
}

/// Result of conformal prediction with multiple coverage levels.
///
/// Uses a flattened structure for safer FFI. All arrays have length
/// `n_forecasts * n_levels`, with intervals stored level-by-level.
#[repr(C)]
pub struct ConformalMultiResultFFI {
    /// Point forecasts
    pub point: *mut c_double,
    /// Number of point forecasts
    pub n_forecasts: size_t,
    /// Coverage levels (one per level)
    pub coverage_levels: *mut c_double,
    /// Conformity scores (one per level)
    pub conformity_scores: *mut c_double,
    /// Number of coverage levels
    pub n_levels: size_t,
    /// Flattened lower bounds (n_forecasts * n_levels, level-major order)
    pub lower: *mut c_double,
    /// Flattened upper bounds (n_forecasts * n_levels, level-major order)
    pub upper: *mut c_double,
}

impl Default for ConformalMultiResultFFI {
    fn default() -> Self {
        Self {
            point: std::ptr::null_mut(),
            n_forecasts: 0,
            coverage_levels: std::ptr::null_mut(),
            conformity_scores: std::ptr::null_mut(),
            n_levels: 0,
            lower: std::ptr::null_mut(),
            upper: std::ptr::null_mut(),
        }
    }
}
