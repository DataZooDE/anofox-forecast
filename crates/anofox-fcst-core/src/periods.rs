//! Period detection for time series using multiple methods.
//!
//! This module wraps fdars-core's period detection functions for use with
//! time series data in DuckDB.

use crate::error::{ForecastError, Result};
use fdars_core::seasonal::{
    detect_multiple_periods as fdars_detect_multiple_periods, estimate_period_acf,
    estimate_period_fft, estimate_period_regression, DetectedPeriod as FdarsDetectedPeriod,
    PeriodEstimate,
};
use std::str::FromStr;

/// Method for period detection.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PeriodMethod {
    /// FFT-based periodogram analysis
    #[default]
    Fft,
    /// Autocorrelation function peak detection
    Acf,
    /// Fourier regression with grid search
    Regression,
    /// Multiple period detection (iterative residual subtraction)
    Multi,
    /// Auto-select best method
    Auto,
}

impl FromStr for PeriodMethod {
    type Err = std::convert::Infallible;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        Ok(match s.to_lowercase().as_str() {
            "fft" | "periodogram" => Self::Fft,
            "acf" | "autocorrelation" => Self::Acf,
            "regression" | "fourier" => Self::Regression,
            "multi" | "multiple" => Self::Multi,
            "auto" => Self::Auto,
            _ => Self::Fft,
        })
    }
}

/// Result from single period estimation.
#[derive(Debug, Clone)]
pub struct SinglePeriodResult {
    /// Estimated period (in samples)
    pub period: f64,
    /// Dominant frequency (1/period)
    pub frequency: f64,
    /// Power at the dominant frequency
    pub power: f64,
    /// Confidence measure (ratio of peak power to mean power)
    pub confidence: f64,
    /// Method used for estimation
    pub method: String,
}

impl From<(PeriodEstimate, &str)> for SinglePeriodResult {
    fn from((pe, method): (PeriodEstimate, &str)) -> Self {
        Self {
            period: pe.period,
            frequency: pe.frequency,
            power: pe.power,
            confidence: pe.confidence,
            method: method.to_string(),
        }
    }
}

/// A detected period from multiple period detection.
#[derive(Debug, Clone)]
pub struct DetectedPeriod {
    /// Estimated period (in samples)
    pub period: f64,
    /// FFT confidence (ratio of peak power to mean power)
    pub confidence: f64,
    /// Seasonal strength at this period (variance explained)
    pub strength: f64,
    /// Amplitude of the sinusoidal component
    pub amplitude: f64,
    /// Phase of the sinusoidal component (radians)
    pub phase: f64,
    /// Iteration number (1-indexed)
    pub iteration: usize,
}

impl From<FdarsDetectedPeriod> for DetectedPeriod {
    fn from(dp: FdarsDetectedPeriod) -> Self {
        Self {
            period: dp.period,
            confidence: dp.confidence,
            strength: dp.strength,
            amplitude: dp.amplitude,
            phase: dp.phase,
            iteration: dp.iteration,
        }
    }
}

/// Result from multiple period detection.
#[derive(Debug, Clone)]
pub struct MultiPeriodResult {
    /// Detected periods ordered by strength
    pub periods: Vec<DetectedPeriod>,
    /// Primary (strongest) period
    pub primary_period: f64,
    /// Method used for estimation
    pub method: String,
}

/// Create argvals (time points) for a time series of given length.
fn make_argvals(n: usize) -> Vec<f64> {
    (0..n).map(|i| i as f64).collect()
}

/// Estimate period using FFT periodogram.
///
/// Finds the dominant frequency in the periodogram (excluding DC)
/// and returns the corresponding period.
///
/// # Arguments
/// * `values` - Time series values
///
/// # Returns
/// Estimated period result with confidence
pub fn estimate_period_fft_ts(values: &[f64]) -> Result<SinglePeriodResult> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    let argvals = make_argvals(n);
    let result = estimate_period_fft(values, n, 1, &argvals);

    Ok((result, "fft").into())
}

/// Estimate period using autocorrelation function.
///
/// Finds the first significant peak in the ACF after lag 0.
///
/// # Arguments
/// * `values` - Time series values
/// * `max_lag` - Maximum lag to search (None for n/2)
///
/// # Returns
/// Estimated period result with confidence
pub fn estimate_period_acf_ts(values: &[f64], max_lag: Option<usize>) -> Result<SinglePeriodResult> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    let argvals = make_argvals(n);
    let lag = max_lag.unwrap_or(n / 2);
    let result = estimate_period_acf(values, n, 1, &argvals, lag);

    Ok((result, "acf").into())
}

/// Estimate period using Fourier regression grid search.
///
/// Tests multiple candidate periods and selects the one that
/// minimizes the reconstruction error.
///
/// # Arguments
/// * `values` - Time series values
/// * `period_min` - Minimum period to search (None for 2)
/// * `period_max` - Maximum period to search (None for n/2)
/// * `n_candidates` - Number of candidate periods (None for 50)
/// * `n_harmonics` - Number of Fourier harmonics (None for 3)
///
/// # Returns
/// Estimated period result with confidence
pub fn estimate_period_regression_ts(
    values: &[f64],
    period_min: Option<f64>,
    period_max: Option<f64>,
    n_candidates: Option<usize>,
    n_harmonics: Option<usize>,
) -> Result<SinglePeriodResult> {
    let n = values.len();
    if n < 8 {
        return Err(ForecastError::InsufficientData { needed: 8, got: n });
    }

    let argvals = make_argvals(n);
    let p_min = period_min.unwrap_or(2.0);
    let p_max = period_max.unwrap_or(n as f64 / 2.0);
    let candidates = n_candidates.unwrap_or(50);
    let harmonics = n_harmonics.unwrap_or(3);

    let result = estimate_period_regression(values, n, 1, &argvals, p_min, p_max, candidates, harmonics);

    Ok((result, "regression").into())
}

/// Detect multiple concurrent periodicities.
///
/// Uses iterative residual subtraction to find multiple periods.
///
/// # Arguments
/// * `values` - Time series values
/// * `max_periods` - Maximum number of periods to detect (None for 5)
/// * `min_confidence` - Minimum confidence threshold (None for 2.0)
/// * `min_strength` - Minimum strength threshold (None for 0.1)
///
/// # Returns
/// Result with all detected periods
pub fn detect_multiple_periods_ts(
    values: &[f64],
    max_periods: Option<usize>,
    min_confidence: Option<f64>,
    min_strength: Option<f64>,
) -> Result<MultiPeriodResult> {
    let n = values.len();
    if n < 8 {
        return Err(ForecastError::InsufficientData { needed: 8, got: n });
    }

    let argvals = make_argvals(n);
    let max_p = max_periods.unwrap_or(5);
    let min_conf = min_confidence.unwrap_or(2.0);
    let min_str = min_strength.unwrap_or(0.1);

    let detected = fdars_detect_multiple_periods(values, n, 1, &argvals, max_p, min_conf, min_str);

    let periods: Vec<DetectedPeriod> = detected.into_iter().map(Into::into).collect();
    let primary = periods.first().map(|p| p.period).unwrap_or(0.0);

    Ok(MultiPeriodResult {
        periods,
        primary_period: primary,
        method: "multi".to_string(),
    })
}

/// Detect periods using the specified method.
///
/// # Arguments
/// * `values` - Time series values
/// * `method` - Detection method to use
///
/// # Returns
/// For single-period methods: a result with one period
/// For multi-period method: a result with multiple periods
pub fn detect_periods(values: &[f64], method: PeriodMethod) -> Result<MultiPeriodResult> {
    match method {
        PeriodMethod::Fft => {
            let single = estimate_period_fft_ts(values)?;
            Ok(MultiPeriodResult {
                periods: vec![DetectedPeriod {
                    period: single.period,
                    confidence: single.confidence,
                    strength: single.power,
                    amplitude: 0.0,
                    phase: 0.0,
                    iteration: 1,
                }],
                primary_period: single.period,
                method: single.method,
            })
        }
        PeriodMethod::Acf => {
            let single = estimate_period_acf_ts(values, None)?;
            Ok(MultiPeriodResult {
                periods: vec![DetectedPeriod {
                    period: single.period,
                    confidence: single.confidence,
                    strength: single.power,
                    amplitude: 0.0,
                    phase: 0.0,
                    iteration: 1,
                }],
                primary_period: single.period,
                method: single.method,
            })
        }
        PeriodMethod::Regression => {
            let single = estimate_period_regression_ts(values, None, None, None, None)?;
            Ok(MultiPeriodResult {
                periods: vec![DetectedPeriod {
                    period: single.period,
                    confidence: single.confidence,
                    strength: single.power,
                    amplitude: 0.0,
                    phase: 0.0,
                    iteration: 1,
                }],
                primary_period: single.period,
                method: single.method,
            })
        }
        PeriodMethod::Multi => detect_multiple_periods_ts(values, None, None, None),
        PeriodMethod::Auto => {
            // Use FFT first, then validate with ACF
            let fft_result = estimate_period_fft_ts(values)?;
            let acf_result = estimate_period_acf_ts(values, None)?;

            // Use FFT result if it agrees with ACF (within 10%)
            let agreement =
                (fft_result.period - acf_result.period).abs() / fft_result.period < 0.1;

            let (best, method_name) = if agreement || fft_result.confidence > acf_result.confidence
            {
                (fft_result, "auto(fft)")
            } else {
                (acf_result, "auto(acf)")
            };

            Ok(MultiPeriodResult {
                periods: vec![DetectedPeriod {
                    period: best.period,
                    confidence: best.confidence,
                    strength: best.power,
                    amplitude: 0.0,
                    phase: 0.0,
                    iteration: 1,
                }],
                primary_period: best.period,
                method: method_name.to_string(),
            })
        }
    }
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
    fn test_estimate_period_fft() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = estimate_period_fft_ts(&values);

        // Verify function runs without error
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.method, "fft");
    }

    #[test]
    fn test_estimate_period_acf() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = estimate_period_acf_ts(&values, None);

        // Verify function runs without error
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.method, "acf");
    }

    #[test]
    fn test_estimate_period_regression() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = estimate_period_regression_ts(&values, Some(6.0), Some(24.0), None, None);

        // Verify function runs without error
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(result.method, "regression");
    }

    #[test]
    fn test_detect_multiple_periods() {
        // Create series with two periods: 12 and 52
        let values: Vec<f64> = (0..520)
            .map(|i| {
                5.0 * (2.0 * PI * i as f64 / 12.0).sin()
                    + 3.0 * (2.0 * PI * i as f64 / 52.0).sin()
            })
            .collect();

        let result = detect_multiple_periods_ts(&values, Some(3), Some(1.5), Some(0.05)).unwrap();

        // Verify function runs (may return empty if detection thresholds not met)
        assert_eq!(result.method, "multi");
    }

    #[test]
    fn test_detect_periods_auto() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = detect_periods(&values, PeriodMethod::Auto);

        // Verify function runs without error
        assert!(result.is_ok());
        let result = result.unwrap();
        assert!(result.method.starts_with("auto"));
    }

    #[test]
    fn test_insufficient_data() {
        let values = vec![1.0, 2.0, 3.0];
        assert!(estimate_period_fft_ts(&values).is_err());
    }
}
