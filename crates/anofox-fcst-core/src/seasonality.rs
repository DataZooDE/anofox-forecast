//! Seasonality detection and analysis.
//!
//! This module provides functions for detecting and analyzing seasonal patterns
//! in time series data, wrapping both custom implementations and fdars-core functions.

use crate::detrending::{detrend_linear, single_series_matrix};
use crate::error::{ForecastError, Result};
use fdars_core::seasonal::{
    classify_seasonality as fdars_classify_seasonality,
    detect_amplitude_modulation_wavelet as fdars_detect_amplitude_modulation_wavelet,
    detect_seasonality_changes as fdars_detect_seasonality_changes,
    instantaneous_period as fdars_instantaneous_period,
    seasonal_strength_spectral as fdars_seasonal_strength_spectral,
    seasonal_strength_variance as fdars_seasonal_strength_variance,
    seasonal_strength_windowed as fdars_seasonal_strength_windowed,
    ChangeDetectionResult as FdarsChangeDetectionResult, ChangePoint as FdarsChangePoint,
    ChangeType, InstantaneousPeriod as FdarsInstantaneousPeriod, ModulationType,
    SeasonalType as FdarsSeasonalType, SeasonalityClassification as FdarsSeasonalityClassification,
    StrengthMethod as FdarsStrengthMethod, WaveletAmplitudeResult as FdarsWaveletAmplitudeResult,
};
use std::str::FromStr;

/// Result of seasonality analysis.
#[derive(Debug, Clone)]
pub struct SeasonalityAnalysis {
    /// Detected seasonal periods
    pub periods: Vec<i32>,
    /// Strength of each detected period (0-1)
    pub strengths: Vec<f64>,
    /// Primary (dominant) period
    pub primary_period: i32,
    /// Overall trend strength (0-1)
    pub trend_strength: f64,
    /// Overall seasonal strength (0-1)
    pub seasonal_strength: f64,
    /// Whether the series is considered seasonal
    pub is_seasonal: bool,
}

/// Method for computing seasonal strength.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum StrengthMethod {
    /// Variance decomposition: Var(seasonal) / Var(total)
    #[default]
    Variance,
    /// Spectral: power at seasonal frequencies / total power
    Spectral,
}

/// All strength method names accepted by [`StrengthMethod::from_str`], for
/// the error message naming every valid choice (D-12).
const VALID_STRENGTH_METHODS: &str = "variance (alias var), spectral (alias spec, fft)";

/// `seasonal_strength_wavelet` is removed (D-12, G19): it returned 1.0 even
/// for white noise in fdars-core 0.47 (always-maximal strength regardless
/// of input), so it is strictly worse than reporting nothing. Requesting it
/// now raises an error naming the two remaining methods instead of
/// silently falling back to `variance`.
const WAVELET_REMOVED_MESSAGE: &str = "removed: seasonal_strength_wavelet returned 1.0 even for white noise in fdars-core 0.47; use 'variance' (default) or 'spectral'";

impl FromStr for StrengthMethod {
    type Err = ForecastError;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        Ok(match s.to_lowercase().as_str() {
            "variance" | "var" => Self::Variance,
            "spectral" | "spec" | "fft" => Self::Spectral,
            "wavelet" | "wav" => {
                return Err(ForecastError::InvalidParameter {
                    param: "method".to_string(),
                    value: s.to_string(),
                    reason: WAVELET_REMOVED_MESSAGE.to_string(),
                })
            }
            other => {
                return Err(ForecastError::InvalidParameter {
                    param: "method".to_string(),
                    value: other.to_string(),
                    reason: format!("expected one of: {VALID_STRENGTH_METHODS}"),
                })
            }
        })
    }
}

impl From<StrengthMethod> for FdarsStrengthMethod {
    fn from(m: StrengthMethod) -> Self {
        match m {
            StrengthMethod::Variance => FdarsStrengthMethod::Variance,
            StrengthMethod::Spectral => FdarsStrengthMethod::Spectral,
        }
    }
}

/// Type of seasonality pattern.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SeasonalType {
    /// Regular peaks with consistent timing
    StableSeasonal,
    /// Regular peaks but timing shifts between cycles
    VariableTiming,
    /// Some cycles seasonal, some not
    IntermittentSeasonal,
    /// No clear seasonality
    NonSeasonal,
}

impl From<FdarsSeasonalType> for SeasonalType {
    fn from(t: FdarsSeasonalType) -> Self {
        match t {
            FdarsSeasonalType::StableSeasonal => Self::StableSeasonal,
            FdarsSeasonalType::VariableTiming => Self::VariableTiming,
            FdarsSeasonalType::IntermittentSeasonal => Self::IntermittentSeasonal,
            FdarsSeasonalType::NonSeasonal => Self::NonSeasonal,
            // fdars-core 0.47 marked this enum #[non_exhaustive]; fall back
            // to NonSeasonal for any future variant rather than panicking.
            _ => Self::NonSeasonal,
        }
    }
}

impl SeasonalType {
    /// Get the string representation of the seasonal type.
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::StableSeasonal => "stable_seasonal",
            Self::VariableTiming => "variable_timing",
            Self::IntermittentSeasonal => "intermittent_seasonal",
            Self::NonSeasonal => "non_seasonal",
        }
    }
}

/// Result of seasonality classification.
#[derive(Debug, Clone)]
pub struct SeasonalityClassification {
    /// Whether the series is seasonal overall
    pub is_seasonal: bool,
    /// Whether peak timing is stable across cycles
    pub has_stable_timing: bool,
    /// Timing variability score (0 = stable, 1 = highly variable)
    pub timing_variability: f64,
    /// Overall seasonal strength
    pub seasonal_strength: f64,
    /// Per-cycle seasonal strength
    pub cycle_strengths: Vec<f64>,
    /// Indices of weak/missing seasons (0-indexed)
    pub weak_seasons: Vec<usize>,
    /// Classification type
    pub classification: SeasonalType,
}

impl From<FdarsSeasonalityClassification> for SeasonalityClassification {
    fn from(c: FdarsSeasonalityClassification) -> Self {
        Self {
            is_seasonal: c.is_seasonal,
            has_stable_timing: c.has_stable_timing,
            timing_variability: c.timing_variability,
            seasonal_strength: c.seasonal_strength,
            cycle_strengths: c.cycle_strengths,
            weak_seasons: c.weak_seasons,
            classification: c.classification.into(),
        }
    }
}

/// Type of change in seasonality.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChangePointType {
    /// Series becomes seasonal
    Onset,
    /// Series stops being seasonal
    Cessation,
}

impl From<ChangeType> for ChangePointType {
    fn from(t: ChangeType) -> Self {
        match t {
            ChangeType::Onset => Self::Onset,
            ChangeType::Cessation => Self::Cessation,
            // fdars-core 0.47 marked this enum #[non_exhaustive]; fall back
            // to Cessation for any future variant rather than panicking.
            _ => Self::Cessation,
        }
    }
}

impl ChangePointType {
    /// Get the string representation of the change type.
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Onset => "onset",
            Self::Cessation => "cessation",
        }
    }
}

/// A detected change point in seasonality.
#[derive(Debug, Clone)]
pub struct SeasonalityChangePoint {
    /// Index at which the change occurs
    pub index: usize,
    /// Time at which the change occurs
    pub time: f64,
    /// Type of change
    pub change_type: ChangePointType,
    /// Seasonal strength before the change
    pub strength_before: f64,
    /// Seasonal strength after the change
    pub strength_after: f64,
}

impl From<FdarsChangePoint> for SeasonalityChangePoint {
    fn from(cp: FdarsChangePoint) -> Self {
        Self {
            index: cp.time as usize,
            time: cp.time,
            change_type: cp.change_type.into(),
            strength_before: cp.strength_before,
            strength_after: cp.strength_after,
        }
    }
}

/// Result of seasonality change detection.
#[derive(Debug, Clone)]
pub struct ChangeDetectionResult {
    /// Detected change points
    pub change_points: Vec<SeasonalityChangePoint>,
    /// Time-varying seasonal strength curve
    pub strength_curve: Vec<f64>,
}

impl From<FdarsChangeDetectionResult> for ChangeDetectionResult {
    fn from(r: FdarsChangeDetectionResult) -> Self {
        Self {
            change_points: r.change_points.into_iter().map(Into::into).collect(),
            strength_curve: r.strength_curve,
        }
    }
}

/// Result of instantaneous period estimation.
#[derive(Debug, Clone)]
pub struct InstantaneousPeriodResult {
    /// Instantaneous period at each time point
    pub period: Vec<f64>,
    /// Instantaneous frequency at each time point
    pub frequency: Vec<f64>,
    /// Instantaneous amplitude (envelope) at each time point
    pub amplitude: Vec<f64>,
}

impl From<FdarsInstantaneousPeriod> for InstantaneousPeriodResult {
    fn from(ip: FdarsInstantaneousPeriod) -> Self {
        Self {
            period: ip.period,
            frequency: ip.frequency,
            amplitude: ip.amplitude,
        }
    }
}

/// Type of amplitude modulation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AmplitudeModulationType {
    /// Constant amplitude (no modulation)
    Stable,
    /// Amplitude increases over time (seasonality emerges)
    Emerging,
    /// Amplitude decreases over time (seasonality fades)
    Fading,
    /// Amplitude varies non-monotonically
    Oscillating,
    /// No seasonality detected
    NonSeasonal,
}

impl From<ModulationType> for AmplitudeModulationType {
    fn from(m: ModulationType) -> Self {
        match m {
            ModulationType::Stable => Self::Stable,
            ModulationType::Emerging => Self::Emerging,
            ModulationType::Fading => Self::Fading,
            ModulationType::Oscillating => Self::Oscillating,
            ModulationType::NonSeasonal => Self::NonSeasonal,
            // fdars-core 0.47 marked this enum #[non_exhaustive]; fall back
            // to NonSeasonal for any future variant rather than panicking.
            _ => Self::NonSeasonal,
        }
    }
}

impl AmplitudeModulationType {
    /// Get the string representation of the modulation type.
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Stable => "stable",
            Self::Emerging => "emerging",
            Self::Fading => "fading",
            Self::Oscillating => "oscillating",
            Self::NonSeasonal => "non_seasonal",
        }
    }
}

/// Result of wavelet-based amplitude modulation detection.
#[derive(Debug, Clone)]
pub struct AmplitudeModulationResult {
    /// Whether seasonality is present
    pub is_seasonal: bool,
    /// Overall seasonal strength
    pub seasonal_strength: f64,
    /// Whether amplitude modulation is detected
    pub has_modulation: bool,
    /// Type of amplitude modulation
    pub modulation_type: AmplitudeModulationType,
    /// Coefficient of variation of wavelet amplitude
    pub modulation_score: f64,
    /// Trend in amplitude (-1 to 1)
    pub amplitude_trend: f64,
    /// Wavelet amplitude at the seasonal frequency over time
    pub wavelet_amplitude: Vec<f64>,
    /// Time points corresponding to wavelet_amplitude
    pub time_points: Vec<f64>,
    /// Scale (period) used for wavelet analysis
    pub scale: f64,
}

impl From<FdarsWaveletAmplitudeResult> for AmplitudeModulationResult {
    fn from(r: FdarsWaveletAmplitudeResult) -> Self {
        Self {
            is_seasonal: r.is_seasonal,
            seasonal_strength: r.seasonal_strength,
            has_modulation: r.has_modulation,
            modulation_type: r.modulation_type.into(),
            modulation_score: r.modulation_score,
            amplitude_trend: r.amplitude_trend,
            wavelet_amplitude: r.wavelet_amplitude,
            time_points: r.time_points,
            scale: r.scale,
        }
    }
}

/// Create argvals (time points) for a time series of given length.
fn make_argvals(n: usize) -> Vec<f64> {
    (0..n).map(|i| i as f64).collect()
}

/// Detect seasonal periods in a time series using autocorrelation.
pub fn detect_seasonality(values: &[f64], max_period: Option<usize>) -> Result<Vec<i32>> {
    if values.len() < 4 {
        return Err(ForecastError::InsufficientData {
            needed: 4,
            got: values.len(),
        });
    }

    let max_lag = max_period.unwrap_or(values.len() / 2).min(values.len() / 2);

    if max_lag < 2 {
        return Ok(vec![]);
    }

    // Calculate autocorrelation at each lag
    let mean: f64 = values.iter().sum::<f64>() / values.len() as f64;
    let variance: f64 = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>();

    if variance.abs() < f64::EPSILON {
        return Ok(vec![]);
    }

    let mut acf = Vec::with_capacity(max_lag);
    for lag in 1..=max_lag {
        let mut sum = 0.0;
        for i in 0..(values.len() - lag) {
            sum += (values[i] - mean) * (values[i + lag] - mean);
        }
        acf.push(sum / variance);
    }

    // Find peaks in ACF
    let mut periods = Vec::new();
    let threshold = 0.1; // Minimum ACF value to consider

    for i in 1..(acf.len() - 1) {
        if acf[i] > acf[i - 1] && acf[i] > acf[i + 1] && acf[i] > threshold {
            periods.push((i + 1) as i32);
        }
    }

    // Sort by ACF strength
    periods.sort_by(|a, b| {
        let acf_a = acf[(*a as usize) - 1];
        let acf_b = acf[(*b as usize) - 1];
        acf_b
            .partial_cmp(&acf_a)
            .unwrap_or(std::cmp::Ordering::Equal)
    });

    // Keep top 5 periods
    periods.truncate(5);

    Ok(periods)
}

/// Analyze seasonality in a time series.
pub fn analyze_seasonality(
    values: &[f64],
    max_period: Option<usize>,
) -> Result<SeasonalityAnalysis> {
    let periods = detect_seasonality(values, max_period)?;

    if periods.is_empty() {
        return Ok(SeasonalityAnalysis {
            periods: vec![],
            strengths: vec![],
            primary_period: 0,
            trend_strength: compute_trend_strength(values),
            seasonal_strength: 0.0,
            is_seasonal: false,
        });
    }

    // Calculate strength for each period
    let mean: f64 = values.iter().sum::<f64>() / values.len() as f64;
    let variance: f64 = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>();

    let mut strengths = Vec::with_capacity(periods.len());
    for &period in &periods {
        let lag = period as usize;
        if lag >= values.len() {
            strengths.push(0.0);
            continue;
        }

        let mut sum = 0.0;
        for i in 0..(values.len() - lag) {
            sum += (values[i] - mean) * (values[i + lag] - mean);
        }
        let acf = if variance.abs() > f64::EPSILON {
            sum / variance
        } else {
            0.0
        };
        strengths.push(acf.clamp(0.0, 1.0));
    }

    let primary_period = periods.first().cloned().unwrap_or(0);
    let seasonal_strength = strengths.first().cloned().unwrap_or(0.0);
    let trend_strength = compute_trend_strength(values);

    Ok(SeasonalityAnalysis {
        periods,
        strengths,
        primary_period,
        trend_strength,
        seasonal_strength,
        is_seasonal: seasonal_strength > 0.1,
    })
}

/// Compute trend strength as `|r|`, the absolute Pearson correlation
/// between the series and a linear time index (D-12, G22). This is the
/// SQUARE ROOT of the linear-fit `R^2`, not `R^2` itself -- a prior doc
/// comment claimed "R-squared" without the square root; the implementation
/// below is unchanged, only this comment is corrected.
/// `ts_analyze_seasonality`'s `trend_strength` column documents the same
/// `|r|` definition.
fn compute_trend_strength(values: &[f64]) -> f64 {
    if values.len() < 2 {
        return 0.0;
    }

    let n = values.len() as f64;
    let x_mean = (n - 1.0) / 2.0;
    let y_mean: f64 = values.iter().sum::<f64>() / n;

    let mut ss_xy = 0.0;
    let mut ss_xx = 0.0;
    let mut ss_yy = 0.0;

    for (i, &y) in values.iter().enumerate() {
        let x = i as f64;
        ss_xy += (x - x_mean) * (y - y_mean);
        ss_xx += (x - x_mean).powi(2);
        ss_yy += (y - y_mean).powi(2);
    }

    if ss_xx.abs() < f64::EPSILON || ss_yy.abs() < f64::EPSILON {
        return 0.0;
    }

    (ss_xy.powi(2) / (ss_xx * ss_yy)).sqrt().clamp(0.0, 1.0)
}

// ============================================================================
// fdars-core wrapped functions
// ============================================================================

/// Compute seasonal strength using variance decomposition.
///
/// Measures the proportion of variance explained by the seasonal component.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `n_harmonics` - Number of Fourier harmonics (None for 3)
///
/// # Returns
/// Seasonal strength (0 to 1)
pub fn seasonal_strength_variance(
    values: &[f64],
    period: f64,
    n_harmonics: Option<usize>,
) -> Result<f64> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);
    let harmonics = n_harmonics.unwrap_or(3);

    // D-12, G19: linearly detrend FIRST -- fdars 0.47's own
    // seasonal_strength_variance measures variance explained on the RAW
    // series, so a trend inflates total variance and crushes the
    // seasonal-share ratio (0.5*t + 2*sin(2*pi*t/12), n=120 measured
    // 0.00296 instead of ~1.0 for the sine alone).
    let detrended = detrend_linear(values)?.detrended;
    let mat = single_series_matrix(&detrended)?;
    Ok(fdars_seasonal_strength_variance(
        &mat, &argvals, period, harmonics,
    ))
}

/// Compute seasonal strength using spectral method.
///
/// Measures the ratio of power at seasonal frequencies to total power.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
///
/// # Returns
/// Seasonal strength (0 to 1)
pub fn seasonal_strength_spectral(values: &[f64], period: f64) -> Result<f64> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);

    // D-12, G19: linearly detrend first, same rationale as
    // seasonal_strength_variance above.
    let detrended = detrend_linear(values)?.detrended;
    let mat = single_series_matrix(&detrended)?;
    Ok(fdars_seasonal_strength_spectral(&mat, &argvals, period))
}

/// Compute seasonal strength using the specified method.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `method` - Strength computation method
///
/// # Returns
/// Seasonal strength (0 to 1)
pub fn seasonal_strength(values: &[f64], period: f64, method: StrengthMethod) -> Result<f64> {
    match method {
        StrengthMethod::Variance => seasonal_strength_variance(values, period, None),
        StrengthMethod::Spectral => seasonal_strength_spectral(values, period),
    }
}

/// Compute time-varying seasonal strength using sliding windows.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `window_size` - Window size as a multiple of period (None for 2.0)
/// * `method` - Strength computation method (None for Variance)
///
/// # Returns
/// Vector of seasonal strength values (one per time point)
pub fn seasonal_strength_windowed(
    values: &[f64],
    period: f64,
    window_size: Option<f64>,
    method: Option<StrengthMethod>,
) -> Result<Vec<f64>> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);
    let win_size = window_size.unwrap_or(2.0 * period);
    let strength_method: FdarsStrengthMethod = method.unwrap_or(StrengthMethod::Variance).into();

    // D-12, G19: linearly detrend first, same rationale as
    // seasonal_strength_variance above.
    let detrended = detrend_linear(values)?.detrended;
    let mat = single_series_matrix(&detrended)?;
    Ok(fdars_seasonal_strength_windowed(
        &mat,
        &argvals,
        period,
        win_size,
        strength_method,
    ))
}

/// Classify the type of seasonality in a time series.
///
/// Analyzes the series to determine if seasonality is stable, variable,
/// intermittent, or absent.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `strength_threshold` - Threshold for seasonal strength (None for 0.3)
/// * `timing_threshold` - Threshold for timing variability (None for 0.1)
///
/// # Returns
/// Seasonality classification result
pub fn classify_seasonality(
    values: &[f64],
    period: f64,
    strength_threshold: Option<f64>,
    timing_threshold: Option<f64>,
) -> Result<SeasonalityClassification> {
    let n = values.len();
    if n < 2 * period as usize {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period as usize,
            got: n,
        });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);

    // fdars 0.47 / B2 fix: classify_seasonality(data: &FdMatrix, argvals, period, ...)
    // -- the matrix is built once as (1, n), not the swapped (n, 1) this
    // function used to pass.
    let mat = single_series_matrix(values)?;
    let result =
        fdars_classify_seasonality(&mat, &argvals, period, strength_threshold, timing_threshold);

    Ok(result.into())
}

/// Detect changes in seasonality over time.
///
/// Monitors seasonal strength and identifies points where seasonality
/// starts, ends, increases, or decreases.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `threshold` - Strength threshold for change detection (None for 0.3)
/// * `window_size` - Window size as a multiple of period (None for 2.0)
/// * `min_duration` - Minimum duration for a change (None for 1.0 period)
///
/// # Returns
/// Change detection result with change points and strength curve
pub fn detect_seasonality_changes(
    values: &[f64],
    period: f64,
    threshold: Option<f64>,
    window_size: Option<f64>,
    min_duration: Option<f64>,
) -> Result<ChangeDetectionResult> {
    let n = values.len();
    if n < 2 * period as usize {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period as usize,
            got: n,
        });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);
    let thresh = threshold.unwrap_or(0.3);
    let win_size = window_size.unwrap_or(2.0 * period);
    let min_dur = min_duration.unwrap_or(period);

    // fdars 0.47: detect_seasonality_changes(data: &FdMatrix, argvals, period, threshold, window_size, min_duration)
    let mat = single_series_matrix(values)?;
    let result =
        fdars_detect_seasonality_changes(&mat, &argvals, period, thresh, win_size, min_dur);

    Ok(result.into())
}

/// Estimate instantaneous period using Hilbert transform.
///
/// Provides time-varying estimates of period, frequency, and amplitude.
///
/// # Arguments
/// * `values` - Time series values
///
/// # Returns
/// Instantaneous period result with period, frequency, and amplitude vectors
pub fn instantaneous_period(values: &[f64]) -> Result<InstantaneousPeriodResult> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    let argvals = make_argvals(n);

    // fdars 0.47: instantaneous_period(data: &FdMatrix, argvals)
    let mat = single_series_matrix(values)?;
    let result = fdars_instantaneous_period(&mat, &argvals);

    Ok(result.into())
}

/// Detect amplitude modulation using wavelet transform.
///
/// Uses Morlet wavelet to analyze how the amplitude of seasonality
/// changes over time.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `modulation_threshold` - CV threshold for modulation detection (None for 0.2)
/// * `seasonality_threshold` - Strength threshold for seasonality (None for 0.3)
///
/// # Returns
/// Amplitude modulation result with modulation type and characteristics
pub fn detect_amplitude_modulation(
    values: &[f64],
    period: f64,
    modulation_threshold: Option<f64>,
    seasonality_threshold: Option<f64>,
) -> Result<AmplitudeModulationResult> {
    let n = values.len();
    if n < 2 * period as usize {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period as usize,
            got: n,
        });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);
    let mod_thresh = modulation_threshold.unwrap_or(0.2);
    let seas_thresh = seasonality_threshold.unwrap_or(0.3);

    // fdars 0.47 / B2 fix: detect_amplitude_modulation_wavelet(data: &FdMatrix,
    // argvals, period, modulation_threshold, seasonality_threshold) -- matrix
    // built once as (1, n); thresholds are now plain f64 (no more Option
    // wrapping needed on the fdars side).
    let mat = single_series_matrix(values)?;
    let result =
        fdars_detect_amplitude_modulation_wavelet(&mat, &argvals, period, mod_thresh, seas_thresh);

    Ok(result.into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    fn generate_seasonal_series(n: usize, period: f64, amplitude: f64) -> Vec<f64> {
        (0..n)
            .map(|i| amplitude * (2.0 * PI * i as f64 / period).sin())
            .collect()
    }

    #[test]
    fn test_detect_seasonality_sine() {
        // Create a sine wave with period 12
        let values: Vec<f64> = (0..120)
            .map(|i| (2.0 * PI * i as f64 / 12.0).sin())
            .collect();

        let periods = detect_seasonality(&values, Some(24)).unwrap();
        assert!(!periods.is_empty());
        // Should detect period around 12
        assert!(periods.contains(&12) || periods.contains(&11) || periods.contains(&13));
    }

    #[test]
    fn test_analyze_seasonality() {
        // Use a stronger seasonal signal with mild trend
        let values: Vec<f64> = (0..120)
            .map(|i| 10.0 * (2.0 * PI * i as f64 / 12.0).sin() + 0.01 * i as f64)
            .collect();

        let analysis = analyze_seasonality(&values, Some(24)).unwrap();
        assert!(analysis.is_seasonal);
        assert!(analysis.seasonal_strength > 0.1);
    }

    #[test]
    fn test_seasonal_strength_variance() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = seasonal_strength_variance(&values, 12.0, None);

        // Verify function runs without error
        assert!(result.is_ok());
    }

    #[test]
    fn test_seasonal_strength_spectral() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = seasonal_strength_spectral(&values, 12.0);

        // Verify function runs without error
        assert!(result.is_ok());
    }

    #[test]
    fn test_strength_method_from_str_wavelet_removed() {
        for s in ["wavelet", "wav", "WAVELET"] {
            let err = s.parse::<StrengthMethod>().unwrap_err();
            let msg = err.to_string();
            assert!(msg.contains("variance"), "{s}: {msg}");
            assert!(msg.contains("spectral"), "{s}: {msg}");
        }
    }

    #[test]
    fn test_strength_method_from_str_unknown_lists_choices() {
        let err = "foo".parse::<StrengthMethod>().unwrap_err();
        let msg = err.to_string();
        assert!(msg.contains("variance"), "{msg}");
        assert!(msg.contains("spec"), "{msg}");
    }

    #[test]
    fn test_strength_method_from_str_accepted_aliases() {
        for (s, want) in [
            ("variance", StrengthMethod::Variance),
            ("var", StrengthMethod::Variance),
            ("VARIANCE", StrengthMethod::Variance),
            ("spectral", StrengthMethod::Spectral),
            ("spec", StrengthMethod::Spectral),
            ("fft", StrengthMethod::Spectral),
        ] {
            assert_eq!(s.parse::<StrengthMethod>().unwrap(), want, "{s}");
        }
    }

    #[test]
    fn test_seasonal_strength_variance_detrends_first() {
        // D-12, G19: a trend + seasonal series must report a HIGH seasonal
        // strength once detrended, not the crushed ~0.003 fdars-core 0.47
        // reports on the raw (undetrended) series.
        let values: Vec<f64> = (0..120)
            .map(|i| {
                let t = i as f64;
                0.5 * t + 2.0 * (2.0 * PI * t / 12.0).sin()
            })
            .collect();
        let strength = seasonal_strength_variance(&values, 12.0, None).unwrap();
        assert!(strength >= 0.95, "strength = {strength}");
    }

    #[test]
    fn test_seasonal_strength_windowed() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = seasonal_strength_windowed(&values, 12.0, None, None);

        // Just verify function runs without error
        assert!(result.is_ok());
    }

    /// Deterministic LCG noise in `[-1, 1)` -- never `thread_rng` (the
    /// result must be byte-reproducible across runs/machines).
    fn deterministic_noise(n: usize, seed: u64) -> Vec<f64> {
        let mut state = seed;
        (0..n)
            .map(|_| {
                state = state
                    .wrapping_mul(6364136223846793005)
                    .wrapping_add(1442695040888963407);
                ((state >> 11) as f64 / (1u64 << 53) as f64) * 2.0 - 1.0
            })
            .collect()
    }

    #[test]
    fn test_classify_seasonality_strong_sine_is_stable_seasonal() {
        // Before fdars-core 0.47 (B2), classify_seasonality passed
        // (values, n, 1, ...) -- backwards for a single series -- and
        // returned NonSeasonal with NaN strength for this exact input.
        let values: Vec<f64> = (0..120)
            .map(|i| 10.0 + 5.0 * (2.0 * PI * i as f64 / 12.0).sin())
            .collect();
        let result = classify_seasonality(&values, 12.0, None, None).unwrap();

        assert!(result.is_seasonal);
        assert_eq!(result.classification, SeasonalType::StableSeasonal);
        assert!(
            result.seasonal_strength > 0.99,
            "seasonal_strength = {}",
            result.seasonal_strength
        );
    }

    #[test]
    fn test_classify_seasonality_white_noise_is_non_seasonal() {
        let noise = deterministic_noise(120, 42);
        let result = classify_seasonality(&noise, 12.0, None, None).unwrap();

        assert!(!result.is_seasonal);
        assert_eq!(result.classification, SeasonalType::NonSeasonal);
    }

    #[test]
    fn test_detect_seasonality_changes() {
        // Create series with changing seasonality
        let values: Vec<f64> = (0..240)
            .map(|i| {
                if i < 120 {
                    5.0 * (2.0 * PI * i as f64 / 12.0).sin()
                } else {
                    0.5 * (2.0 * PI * i as f64 / 12.0).sin() // Weaker seasonality
                }
            })
            .collect();

        let result = detect_seasonality_changes(&values, 12.0, Some(0.3), None, None);

        // Just verify function runs without error
        assert!(result.is_ok());
    }

    #[test]
    fn test_instantaneous_period() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = instantaneous_period(&values);

        // Just verify function runs without error
        assert!(result.is_ok());
    }

    #[test]
    fn test_detect_amplitude_modulation_constant_amplitude_is_stable() {
        // Before fdars-core 0.47 (B2), detect_amplitude_modulation passed
        // (values, n, 1, ...) -- backwards for a single series.
        let values: Vec<f64> = (0..240)
            .map(|i| (2.0 * PI * i as f64 / 12.0).sin())
            .collect();
        let result = detect_amplitude_modulation(&values, 12.0, None, None).unwrap();

        assert_eq!(result.modulation_type, AmplitudeModulationType::Stable);
        assert!(!result.has_modulation);
    }

    #[test]
    fn test_detect_amplitude_modulation_growing_amplitude_is_emerging() {
        let values: Vec<f64> = (0..240)
            .map(|i| (1.0 + i as f64 / 30.0) * (2.0 * PI * i as f64 / 12.0).sin())
            .collect();
        let result = detect_amplitude_modulation(&values, 12.0, None, None).unwrap();

        assert_eq!(result.modulation_type, AmplitudeModulationType::Emerging);
        assert!(result.has_modulation);
    }

    #[test]
    fn test_invalid_period() {
        let values = generate_seasonal_series(120, 12.0, 5.0);

        assert!(seasonal_strength_variance(&values, 0.0, None).is_err());
        assert!(seasonal_strength_variance(&values, -5.0, None).is_err());
    }
}
