//! Peak detection for time series.
//!
//! `find_peaks`/`local_maxima`/`peak_prominences` are a direct port of
//! `scipy.signal.find_peaks` 1.18.1 (Phase 15 plan 15-02, DET-01, D-05):
//! plateau midpoints, `O(n)` prominences via a monotonic-stack two-pass
//! scan, and height/distance/prominence filters applied in scipy's own
//! order (local maxima -> height -> distance -> prominence). This
//! replaces fdars-core's own derivative zero-crossing peak detector, which
//! the Phase 15 audit found missed peaks, reported local minima, and gave
//! every plateau prominence 0 (51 found vs scipy's 66, only 31 matching on
//! the audited random walk). `analyze_peak_timing` is unaffected and still
//! delegates to fdars-core's own peak-timing analysis.

use crate::detrending::single_series_matrix;
use crate::error::{ForecastError, Result};
use fdars_core::seasonal::{
    analyze_peak_timing as fdars_analyze_peak_timing, PeakTimingResult as FdarsPeakTimingResult,
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

/// Options for [`find_peaks`], mirroring `scipy.signal.find_peaks`'s
/// `height`/`distance`/`prominence` keyword arguments (`wlen=None`, i.e. the
/// unconstrained full-series prominence window -- this crate has no SQL
/// surface for `wlen`). All three default to `None` (no filter).
#[derive(Debug, Clone, Copy, Default)]
pub struct FindPeaksOptions {
    /// Keep only peaks with `x[peak] >= height` (inclusive minimum).
    pub height: Option<f64>,
    /// Minimum index distance between kept peaks (`ceil`'d; scipy requires
    /// `>= 1`). Values `<= 1` / `None` apply no distance filter.
    pub distance: Option<f64>,
    /// Keep only peaks with prominence `>= prominence` (inclusive minimum),
    /// computed on the survivors of the height/distance filters.
    pub prominence: Option<f64>,
}

/// A peak found by [`find_peaks`]: its index into the input slice, its
/// value (`x[index]`), and its scipy-defined prominence.
#[derive(Debug, Clone, Copy)]
pub struct FoundPeak {
    /// Index into the input slice.
    pub index: usize,
    /// `x[index]`.
    pub height: f64,
    /// `scipy.signal.peak_prominences`-equivalent prominence (`wlen=None`).
    pub prominence: f64,
}

/// Local maxima of `x`, as a direct port of scipy's `_local_maxima_1d`
/// (`scipy/signal/_peak_finding_utils.pyx`): a plateau (a run of equal
/// values preceded by a strict increase and followed by a strict decrease)
/// reports its midpoint index `(left_edge + right_edge) / 2`. The first and
/// last sample are never peaks, and a plateau that reaches the last sample
/// is never a peak (the loop's `i_ahead < i_max` bound excludes it). `x.len()
/// < 3` returns no peaks. NaN comparisons are always `false`, so NaN never
/// participates in forming a peak and this function never panics on NaN.
pub fn local_maxima(x: &[f64]) -> Vec<usize> {
    let n = x.len();
    let mut midpoints = Vec::new();
    if n < 3 {
        return midpoints;
    }
    let i_max = n - 1;
    let mut i = 1;
    while i < i_max {
        if x[i - 1] < x[i] {
            let mut i_ahead = i + 1;
            while i_ahead < i_max && x[i_ahead] == x[i] {
                i_ahead += 1;
            }
            if x[i_ahead] < x[i] {
                let left_edge = i;
                let right_edge = i_ahead - 1;
                midpoints.push((left_edge + right_edge) / 2);
                i = i_ahead;
            }
        }
        i += 1;
    }
    midpoints
}

/// For every index `i`, the minimum of `x` over `(L, i]` where `L` is the
/// nearest index to the left of `i` with `x[L] > x[i]` (or the series start
/// if no such index exists). One left-to-right pass with a monotonic stack
/// of `(boundary_value, min_since_boundary)` pairs: amortized `O(n)` because
/// every index is pushed once and popped at most once. Mirrored by
/// `right_mins` for the symmetric right-side scan `peak_prominences` needs.
fn left_mins(x: &[f64]) -> Vec<f64> {
    let n = x.len();
    let mut left_min = vec![0.0; n];
    let mut stack: Vec<(f64, f64)> = Vec::new();
    for (i, &xi) in x.iter().enumerate() {
        let mut cur_min = xi;
        while let Some(&(top_val, top_min)) = stack.last() {
            if top_val <= xi {
                if top_min < cur_min {
                    cur_min = top_min;
                }
                stack.pop();
            } else {
                break;
            }
        }
        left_min[i] = cur_min;
        stack.push((xi, cur_min));
    }
    left_min
}

/// Mirror of `left_mins` scanning right-to-left: for every index `i`, the
/// minimum of `x` over `[i, R)` where `R` is the nearest index to the right
/// of `i` with `x[R] > x[i]` (or the series end).
fn right_mins(x: &[f64]) -> Vec<f64> {
    let n = x.len();
    let mut right_min = vec![0.0; n];
    let mut stack: Vec<(f64, f64)> = Vec::new();
    for i in (0..n).rev() {
        let xi = x[i];
        let mut cur_min = xi;
        while let Some(&(top_val, top_min)) = stack.last() {
            if top_val <= xi {
                if top_min < cur_min {
                    cur_min = top_min;
                }
                stack.pop();
            } else {
                break;
            }
        }
        right_min[i] = cur_min;
        stack.push((xi, cur_min));
    }
    right_min
}

/// Prominence of each index in `peaks`, equivalent to
/// `scipy.signal.peak_prominences(x, peaks, wlen=None)[0]`: for peak `p`,
/// `x[p] - max(left_min[p], right_min[p])` where `left_min`/`right_min` are
/// the nearest-strictly-higher-neighbour segment minima computed by
/// `left_mins`/`right_mins`. Both passes are `O(n)` total (not `O(n)` per
/// peak), so this function is `O(n)` regardless of how many peaks are
/// queried -- unlike scipy's own per-peak linear scan, which is `O(n)` per
/// peak (`O(n^2)` worst case on a trending series with `O(n)` peaks, T-15-05).
pub fn peak_prominences(x: &[f64], peaks: &[usize]) -> Vec<f64> {
    if peaks.is_empty() {
        return Vec::new();
    }
    let left_min = left_mins(x);
    let right_min = right_mins(x);
    peaks
        .iter()
        .map(|&p| x[p] - left_min[p].max(right_min[p]))
        .collect()
}

/// A direct port of `scipy.signal.find_peaks` (`wlen=None`): local maxima
/// (via [`local_maxima`]) filtered by `height`, `distance`, and
/// `prominence` in scipy's own order -- local maxima -> height -> distance
/// -> prominence (computed on the survivors of height+distance).
///
/// Task 15-02/1 wires the no-filter path only: height/distance/prominence
/// filtering and option validation are completed in Task 15-02/2 (TDD).
/// Every returned peak carries its scipy prominence even when no
/// `prominence` filter was requested.
pub fn find_peaks(x: &[f64], _opts: &FindPeaksOptions) -> Result<Vec<FoundPeak>> {
    let maxima = local_maxima(x);
    let proms = peak_prominences(x, &maxima);
    let found: Vec<FoundPeak> = maxima
        .iter()
        .zip(proms.iter())
        .map(|(&index, &prominence)| FoundPeak {
            index,
            height: x[index],
            prominence,
        })
        .collect();
    Ok(found)
}

/// Detect peaks in a time series.
///
/// A direct port of `scipy.signal.find_peaks` (Phase 15 plan 15-02, D-05):
/// plateaus report their midpoint index, prominence is scipy's own
/// definition (`wlen=None`), and the first/last sample are never peaks.
/// This replaces fdars-core's derivative zero-crossing detector, which the
/// Phase 15 audit found missed peaks, reported local minima, and gave
/// every plateau prominence 0.
///
/// # Arguments
/// * `values` - Time series values
/// * `min_distance` - Minimum distance between peaks (None for auto)
/// * `min_prominence` - Minimum peak prominence (None for 0)
/// * `smooth_first` - Whether to smooth the series before peak detection
/// * `smooth_nbasis` - Number of Fourier basis functions for smoothing
///
/// # Returns
/// Peak detection result with peaks and timing information
///
/// Task 15-02/1 runs `find_peaks` on the raw values with no filters wired
/// (`min_distance`/`min_prominence`/`smooth_first`/`smooth_nbasis` are
/// accepted but not yet applied); Task 15-02/2 (TDD) completes the
/// `min_distance`/`min_prominence` -> [`FindPeaksOptions`] mapping and the
/// fdars Fourier-basis smoothing for `smooth_first`.
pub fn detect_peaks(
    values: &[f64],
    _min_distance: Option<f64>,
    _min_prominence: Option<f64>,
    _smooth_first: bool,
    _smooth_nbasis: Option<usize>,
) -> Result<PeakDetectionResult> {
    let n = values.len();
    if n < 3 {
        return Err(ForecastError::InsufficientData { needed: 3, got: n });
    }

    let opts = FindPeaksOptions::default();
    let found = find_peaks(values, &opts)?;

    let peaks: Vec<Peak> = found
        .iter()
        .map(|p| Peak {
            index: p.index,
            time: p.index as f64,
            value: values[p.index],
            prominence: p.prominence,
        })
        .collect();
    let n_peaks = peaks.len();
    let inter_peak_distances: Vec<f64> = peaks
        .windows(2)
        .map(|w| (w[1].index as f64) - (w[0].index as f64))
        .collect();
    let mean_period = if n_peaks >= 2 {
        inter_peak_distances.iter().sum::<f64>() / inter_peak_distances.len() as f64
    } else {
        f64::NAN
    };

    Ok(PeakDetectionResult {
        peaks,
        n_peaks,
        inter_peak_distances,
        mean_period,
    })
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

    let m = n; // m = number of time points
    let argvals = make_argvals(m);

    // fdars 0.47: analyze_peak_timing(data: &FdMatrix, argvals, period, smooth_nbasis)
    let mat = single_series_matrix(values)?;
    let result = fdars_analyze_peak_timing(&mat, &argvals, period, smooth_nbasis);

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
    fn test_issue_59_peaks_detected() {
        // Regression test for GitHub issue #59: verify peaks are actually detected
        // The bug was passing n/m in wrong order to fdars-core
        let values = vec![1.0, 3.0, 2.0, 5.0, 1.0, 4.0, 2.0];
        let result = detect_peaks_default(&values).unwrap();
        assert!(result.n_peaks > 0, "Should detect peaks in {:?}", values);

        // Also test pronounced peaks
        let values2 = vec![0.0, 0.0, 10.0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0];
        let result2 = detect_peaks_default(&values2).unwrap();
        assert!(result2.n_peaks > 0, "Should detect peaks in {:?}", values2);
    }

    #[test]
    fn test_invalid_period() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        assert!(analyze_peak_timing(&values, 0.0, None).is_err());
        assert!(analyze_peak_timing(&values, -5.0, None).is_err());
    }

    #[test]
    fn test_audit_fdars_derivative_detector_regression() {
        // Regression test for the Phase 15 audit (D-05): fdars-core's own
        // derivative zero-crossing peak detector missed peaks, reported
        // local minima, and gave every plateau prominence 0 (51 found vs
        // scipy's 66 on the audited random walk, only 31 matching). The
        // scipy `find_peaks` port fixes all three on this plateau case:
        // the midpoint index is reported (not the plateau's first sample),
        // and its prominence is the real scipy value (1.0), not 0.
        let plateau = vec![0.0, 1.0, 1.0, 1.0, 0.0];
        let indices = local_maxima(&plateau);
        assert_eq!(indices, vec![2], "plateau midpoint should be index 2");
        let proms = peak_prominences(&plateau, &indices);
        assert_eq!(proms, vec![1.0], "plateau prominence should be 1.0, not 0");

        // No local minimum is ever reported: for a trending-up-then-down
        // series with an interior dip, the dip index must never appear.
        let dip = vec![1.0, 5.0, 0.0, 5.0, 1.0];
        let indices = local_maxima(&dip);
        assert!(
            !indices.contains(&2),
            "local_maxima must never report the interior minimum at index 2: {indices:?}"
        );
        assert_eq!(indices, vec![1, 3]);
    }

    #[test]
    fn test_local_maxima_edges_never_peaks() {
        // scipy: the first and last sample are never peaks, and a plateau
        // that touches either end of the series is not a peak.
        assert_eq!(local_maxima(&[1.0, 1.0, 0.0, 2.0, 2.0]), Vec::<usize>::new());
        assert_eq!(local_maxima(&[0.0, 1.0, 1.0]), Vec::<usize>::new());
    }

    #[test]
    fn test_local_maxima_rejects_short_series() {
        assert_eq!(local_maxima(&[]), Vec::<usize>::new());
        assert_eq!(local_maxima(&[1.0, 2.0]), Vec::<usize>::new());
    }
}
