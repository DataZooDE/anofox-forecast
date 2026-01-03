//! Peak detection for time series.
//!
//! This module wraps fdars-core's peak detection functions for use with
//! time series data in DuckDB.

use crate::error::{ForecastError, Result};
use fdars_core::seasonal::{
    analyze_peak_timing as fdars_analyze_peak_timing, detect_peaks as fdars_detect_peaks,
    Peak as FdarsPeak, PeakDetectionResult as FdarsPeakDetectionResult,
    PeakTimingResult as FdarsPeakTimingResult,
};

/// A detected peak in the time series.
#[derive(Debug, Clone)]
pub struct Peak {
    /// Index (time point) at which the peak occurs
    pub index: usize,
    /// Time at which the peak occurs (as f64)
    pub time: f64,
    /// Value at the peak
    pub value: f64,
    /// Prominence of the peak (height relative to surrounding valleys)
    pub prominence: f64,
}

impl From<FdarsPeak> for Peak {
    fn from(p: FdarsPeak) -> Self {
        Self {
            index: p.time as usize,
            time: p.time,
            value: p.value,
            prominence: p.prominence,
        }
    }
}

/// Result of peak detection.
#[derive(Debug, Clone)]
pub struct PeakDetectionResult {
    /// Detected peaks
    pub peaks: Vec<Peak>,
    /// Number of peaks detected
    pub n_peaks: usize,
    /// Inter-peak distances
    pub inter_peak_distances: Vec<f64>,
    /// Mean period estimated from inter-peak distances
    pub mean_period: f64,
}

impl From<FdarsPeakDetectionResult> for PeakDetectionResult {
    fn from(r: FdarsPeakDetectionResult) -> Self {
        // For time series (m=1), we have one sample at index 0
        let peaks: Vec<Peak> = r
            .peaks
            .first()
            .map(|sample_peaks| sample_peaks.iter().cloned().map(Peak::from).collect())
            .unwrap_or_default();

        let n_peaks = peaks.len();

        let inter_peak_distances: Vec<f64> = r
            .inter_peak_distances
            .first()
            .cloned()
            .unwrap_or_default();

        Self {
            peaks,
            n_peaks,
            inter_peak_distances,
            mean_period: r.mean_period,
        }
    }
}

/// Result of peak timing variability analysis.
#[derive(Debug, Clone)]
pub struct PeakTimingResult {
    /// Peak times for each cycle
    pub peak_times: Vec<f64>,
    /// Peak values
    pub peak_values: Vec<f64>,
    /// Within-period timing (0-1 scale)
    pub normalized_timing: Vec<f64>,
    /// Mean normalized timing
    pub mean_timing: f64,
    /// Standard deviation of normalized timing
    pub std_timing: f64,
    /// Range of normalized timing (max - min)
    pub range_timing: f64,
    /// Variability score (0 = stable, 1 = highly variable)
    pub variability_score: f64,
    /// Trend in timing (positive = peaks getting later)
    pub timing_trend: f64,
    /// Cycle indices (1-indexed)
    pub cycle_indices: Vec<usize>,
    /// Whether timing is considered stable (variability_score < 0.3)
    pub is_stable: bool,
}

impl From<FdarsPeakTimingResult> for PeakTimingResult {
    fn from(r: FdarsPeakTimingResult) -> Self {
        let is_stable = r.variability_score < 0.3;
        Self {
            peak_times: r.peak_times,
            peak_values: r.peak_values,
            normalized_timing: r.normalized_timing,
            mean_timing: r.mean_timing,
            std_timing: r.std_timing,
            range_timing: r.range_timing,
            variability_score: r.variability_score,
            timing_trend: r.timing_trend,
            cycle_indices: r.cycle_indices,
            is_stable,
        }
    }
}

/// Create argvals (time points) for a time series of given length.
fn make_argvals(n: usize) -> Vec<f64> {
    (0..n).map(|i| i as f64).collect()
}

/// Detect peaks in a time series.
///
/// Finds local maxima using derivative zero-crossing analysis.
///
/// # Arguments
/// * `values` - Time series values
/// * `min_distance` - Minimum distance between peaks (None for auto)
/// * `min_prominence` - Minimum peak prominence (None for 0)
/// * `smooth_first` - Whether to smooth the series before peak detection
/// * `smooth_nbasis` - Number of B-spline basis functions for smoothing
///
/// # Returns
/// Peak detection result with peaks and timing information
pub fn detect_peaks(
    values: &[f64],
    min_distance: Option<f64>,
    min_prominence: Option<f64>,
    smooth_first: bool,
    smooth_nbasis: Option<usize>,
) -> Result<PeakDetectionResult> {
    let n = values.len();
    if n < 3 {
        return Err(ForecastError::InsufficientData { needed: 3, got: n });
    }

    let argvals = make_argvals(n);

    let result = fdars_detect_peaks(
        values,
        n,
        1,
        &argvals,
        min_distance,
        min_prominence,
        smooth_first,
        smooth_nbasis,
    );

    Ok(result.into())
}

/// Detect peaks with default parameters.
///
/// Convenience function with sensible defaults for time series.
///
/// # Arguments
/// * `values` - Time series values
///
/// # Returns
/// Peak detection result
pub fn detect_peaks_default(values: &[f64]) -> Result<PeakDetectionResult> {
    detect_peaks(values, None, None, false, None)
}

/// Analyze peak timing variability across seasonal cycles.
///
/// Analyzes the timing of peaks across cycles to assess
/// timing stability.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Expected seasonal period
/// * `smooth_nbasis` - Number of Fourier basis functions for smoothing (None for auto)
///
/// # Returns
/// Peak timing analysis result
pub fn analyze_peak_timing(
    values: &[f64],
    period: f64,
    smooth_nbasis: Option<usize>,
) -> Result<PeakTimingResult> {
    let n = values.len();
    if n < 3 {
        return Err(ForecastError::InsufficientData { needed: 3, got: n });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);

    let result = fdars_analyze_peak_timing(values, n, 1, &argvals, period, smooth_nbasis);

    Ok(result.into())
}

/// Get peak indices from a time series.
///
/// Simplified interface that returns just the peak positions.
///
/// # Arguments
/// * `values` - Time series values
/// * `min_prominence` - Minimum peak prominence (None for 0)
///
/// # Returns
/// Vector of peak indices
pub fn get_peak_indices(values: &[f64], min_prominence: Option<f64>) -> Result<Vec<usize>> {
    let result = detect_peaks(values, None, min_prominence, false, None)?;
    Ok(result.peaks.iter().map(|p| p.index).collect())
}

/// Get peak values from a time series.
///
/// Simplified interface that returns the values at peak positions.
///
/// # Arguments
/// * `values` - Time series values
/// * `min_prominence` - Minimum peak prominence (None for 0)
///
/// # Returns
/// Vector of peak values
pub fn get_peak_values(values: &[f64], min_prominence: Option<f64>) -> Result<Vec<f64>> {
    let result = detect_peaks(values, None, min_prominence, false, None)?;
    Ok(result.peaks.iter().map(|p| p.value).collect())
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
    fn test_detect_peaks_basic() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = detect_peaks_default(&values);

        // Verify function runs without error
        assert!(result.is_ok());
    }

    #[test]
    fn test_detect_peaks_with_prominence() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = detect_peaks(&values, None, Some(3.0), false, None).unwrap();

        // All detected peaks should have prominence >= 3.0 (if any detected)
        for peak in &result.peaks {
            assert!(peak.prominence >= 3.0);
        }
    }

    #[test]
    fn test_analyze_peak_timing() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = analyze_peak_timing(&values, 12.0, None);

        // Verify function runs without error
        assert!(result.is_ok());
    }

    #[test]
    fn test_get_peak_indices() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = get_peak_indices(&values, None);

        // Just verify function doesn't error
        assert!(result.is_ok());
    }

    #[test]
    fn test_insufficient_data() {
        let values = vec![1.0, 2.0];
        assert!(detect_peaks_default(&values).is_err());
    }

    #[test]
    fn test_invalid_period() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        assert!(analyze_peak_timing(&values, 0.0, None).is_err());
        assert!(analyze_peak_timing(&values, -5.0, None).is_err());
    }
}
