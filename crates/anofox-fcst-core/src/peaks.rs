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
use fdars_core::basis::{fourier_fit, select_fourier_nbasis_gcv};
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

/// Validate `FindPeaksOptions` the way scipy's own `find_peaks` does:
/// every set option must be finite, and `distance` must additionally be
/// `>= 1` (scipy raises `ValueError` for `distance < 1`).
fn validate_find_peaks_options(opts: &FindPeaksOptions) -> Result<()> {
    if let Some(h) = opts.height {
        if !h.is_finite() {
            return Err(ForecastError::InvalidParameter {
                param: "height".to_string(),
                value: h.to_string(),
                reason: "must be finite".to_string(),
            });
        }
    }
    if let Some(d) = opts.distance {
        if !d.is_finite() {
            return Err(ForecastError::InvalidParameter {
                param: "distance".to_string(),
                value: d.to_string(),
                reason: "must be finite".to_string(),
            });
        }
        if d < 1.0 {
            return Err(ForecastError::InvalidParameter {
                param: "distance".to_string(),
                value: d.to_string(),
                reason: "must be >= 1".to_string(),
            });
        }
    }
    if let Some(p) = opts.prominence {
        if !p.is_finite() {
            return Err(ForecastError::InvalidParameter {
                param: "prominence".to_string(),
                value: p.to_string(),
                reason: "must be finite".to_string(),
            });
        }
    }
    Ok(())
}

/// scipy's `_select_by_peak_distance`: a stable ascending sort of `peaks`
/// by `x[peaks]` (ties keep their original, index-ascending order -- so
/// among equal heights the rightmost-processed-last-wins, matching numpy's
/// own stable `argsort`), then suppressing lower-priority neighbours
/// within `d` samples, processed from the highest priority down.
fn suppress_by_distance(x: &[f64], peaks: &[usize], d: usize) -> Vec<usize> {
    let n = peaks.len();
    let mut order: Vec<usize> = (0..n).collect();
    order.sort_by(|&a, &b| {
        x[peaks[a]]
            .partial_cmp(&x[peaks[b]])
            .unwrap_or(std::cmp::Ordering::Equal)
    });

    let mut keep = vec![true; n];
    for &j in order.iter().rev() {
        if !keep[j] {
            continue;
        }
        if j > 0 {
            let mut k = j - 1;
            while peaks[j] - peaks[k] < d {
                keep[k] = false;
                if k == 0 {
                    break;
                }
                k -= 1;
            }
        }
        let mut k = j + 1;
        while k < n && peaks[k] - peaks[j] < d {
            keep[k] = false;
            k += 1;
        }
    }

    peaks
        .iter()
        .zip(keep.iter())
        .filter(|(_, &k)| k)
        .map(|(&p, _)| p)
        .collect()
}

/// A direct port of `scipy.signal.find_peaks` (`wlen=None`): local maxima
/// (via [`local_maxima`]) filtered by `height`, `distance`, and
/// `prominence` in scipy's own order -- local maxima -> height -> distance
/// -> prominence (computed on the survivors of height+distance). Every
/// returned peak carries its scipy prominence even when no `prominence`
/// filter was requested.
pub fn find_peaks(x: &[f64], opts: &FindPeaksOptions) -> Result<Vec<FoundPeak>> {
    validate_find_peaks_options(opts)?;

    let mut candidates = local_maxima(x);

    if let Some(hmin) = opts.height {
        candidates.retain(|&idx| x[idx] >= hmin);
    }

    if let Some(distance) = opts.distance {
        if candidates.len() > 1 {
            let d = distance.ceil() as usize;
            candidates = suppress_by_distance(x, &candidates, d);
        }
    }

    let proms = peak_prominences(x, &candidates);
    let mut found: Vec<FoundPeak> = candidates
        .iter()
        .zip(proms.iter())
        .map(|(&index, &prominence)| FoundPeak {
            index,
            height: x[index],
            prominence,
        })
        .collect();

    if let Some(pmin) = opts.prominence {
        found.retain(|p| p.prominence >= pmin);
    }

    Ok(found)
}

/// Smooth `values` with fdars-core's Fourier basis fit, exactly as the
/// project's pre-scipy-port peak detector did when `smooth_first` was set
/// (`fdars-core-0.47.0/src/seasonal/peak.rs::smooth_for_peaks`): `nbasis`
/// defaults to a GCV-selected value in `5..=25` when `smooth_nbasis` is
/// `None`, and a failed fit silently falls back to the raw values (never
/// an error -- smoothing is a best-effort pre-step, not a correctness
/// requirement).
fn smooth_for_peaks(values: &[f64], smooth_nbasis: Option<usize>) -> Result<Vec<f64>> {
    let argvals = make_argvals(values.len());
    let mat = single_series_matrix(values)?;
    let nbasis = smooth_nbasis.unwrap_or_else(|| select_fourier_nbasis_gcv(&mat, &argvals, 5, 25));
    match fourier_fit(&mat, &argvals, nbasis) {
        Ok(result) => Ok(result.fitted.row(0)),
        Err(_) => Ok(values.to_vec()),
    }
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
/// * `min_distance` - Minimum index distance between peaks. Values `<= 1.0`
///   (including the `ts_detect_peaks_by` macro's default of `1.0`) apply no
///   distance filter -- scipy's own distance semantics are already a no-op
///   at `distance == 1` (no two local maxima are ever index-adjacent), so
///   this is a description of existing behaviour, not a special case.
/// * `min_prominence` - Minimum peak prominence. Values `<= 0.0` apply no
///   prominence filter.
/// * `smooth_first` - Apply fdars' Fourier-basis smoothing before peak
///   detection; reported peak values are the smoothed values.
/// * `smooth_nbasis` - Number of Fourier basis functions for smoothing
///   (`None` selects one via GCV in `5..=25`).
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

    let smoothed = if smooth_first {
        smooth_for_peaks(values, smooth_nbasis)?
    } else {
        values.to_vec()
    };

    let opts = FindPeaksOptions {
        height: None,
        distance: min_distance.filter(|&d| d > 1.0),
        prominence: min_prominence.filter(|&p| p > 0.0),
    };
    let found = find_peaks(&smoothed, &opts)?;

    let peaks: Vec<Peak> = found
        .iter()
        .map(|p| Peak {
            index: p.index,
            time: p.index as f64,
            value: smoothed[p.index],
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

    if !period.is_finite() || period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be a positive finite number".to_string(),
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
        let result = result.unwrap();
        // Closed form: 5*sin(2*pi*i/12) over i=0..119 (10 full periods) peaks
        // (value exactly 5.0) at i = 3, 15, 27, ..., 111 -- 10 peaks, spaced
        // exactly 12 apart.
        assert_eq!(result.n_peaks, 10);
        let expected: Vec<usize> = (0..10).map(|k| 3 + 12 * k).collect();
        let got: Vec<usize> = result.peaks.iter().map(|p| p.index).collect();
        assert_eq!(got, expected);
        assert!(
            (result.mean_period - 12.0).abs() < 1e-9,
            "{}",
            result.mean_period
        );
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
        let result = result.unwrap();
        // Value check (deterministic for this exact formula-generated input):
        // normalized_timing takes only two distinct values (0.25 and 1/6)
        // across the 10 cycles -- an integer-binning artifact of how
        // peak-to-period phase is computed when the series length isn't an
        // exact multiple of the smoothing basis -- giving mean_timing
        // 0.20833..., std_timing 0.041666..., variability_score 0.41666....
        let expected_timing = [
            0.25,
            1.0 / 6.0,
            1.0 / 6.0,
            0.25,
            0.25,
            1.0 / 6.0,
            1.0 / 6.0,
            0.25,
            0.25,
            1.0 / 6.0,
        ];
        for (got, want) in result.normalized_timing.iter().zip(&expected_timing) {
            assert!(
                (got - want).abs() < 1e-9,
                "{:?} vs {:?}",
                result.normalized_timing,
                expected_timing
            );
        }
        assert!((result.mean_timing - 0.20833333333333334).abs() < 1e-9);
        assert!((result.std_timing - 0.04166666666666667).abs() < 1e-9);
        assert!((result.variability_score - 0.4166666666666667).abs() < 1e-9);
    }

    #[test]
    fn test_get_peak_indices() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let result = get_peak_indices(&values, None);

        // Just verify function doesn't error
        assert!(result.is_ok());
        // Closed form (same reasoning as test_detect_peaks_basic): peaks at
        // i = 3, 15, 27, ..., 111.
        let expected: Vec<usize> = (0..10).map(|k| 3 + 12 * k).collect();
        assert_eq!(result.unwrap(), expected);
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
        // CR-03: NaN <= 0.0 is false in IEEE-754, so a naive `period <= 0.0`
        // guard previously let a NaN period sail through.
        assert!(analyze_peak_timing(&values, f64::NAN, None).is_err());
        assert!(analyze_peak_timing(&values, f64::INFINITY, None).is_err());
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
        assert_eq!(
            local_maxima(&[1.0, 1.0, 0.0, 2.0, 2.0]),
            Vec::<usize>::new()
        );
        assert_eq!(local_maxima(&[0.0, 1.0, 1.0]), Vec::<usize>::new());
    }

    #[test]
    fn test_local_maxima_rejects_short_series() {
        assert_eq!(local_maxima(&[]), Vec::<usize>::new());
        assert_eq!(local_maxima(&[1.0, 2.0]), Vec::<usize>::new());
    }

    // -----------------------------------------------------------------
    // Task 15-02/2 (TDD): height/distance/prominence filters, option
    // validation, smoothing, O(n)-vs-naive cross-check, performance.
    // -----------------------------------------------------------------

    #[test]
    fn test_find_peaks_height_filter_keeps_only_tall_peaks() {
        let x = vec![1.0, 5.0, 1.0, 2.0, 1.0, 8.0, 1.0];
        let opts = FindPeaksOptions {
            height: Some(6.0),
            distance: None,
            prominence: None,
        };
        let found = find_peaks(&x, &opts).unwrap();
        let indices: Vec<usize> = found.iter().map(|p| p.index).collect();
        assert_eq!(
            indices,
            vec![5],
            "height filter should keep only x[5]=8.0 >= 6.0"
        );
    }

    #[test]
    fn test_find_peaks_distance_filter_rightmost_tie_wins() {
        // ties_distance fixture case (scipy_peaks/cases.json): distance 3
        // keeps only index 3, the rightmost of the two tied height-5 peaks.
        let x = vec![0.0, 5.0, 0.0, 5.0, 0.0, 3.0, 0.0];
        let opts = FindPeaksOptions {
            height: None,
            distance: Some(3.0),
            prominence: None,
        };
        let found = find_peaks(&x, &opts).unwrap();
        let indices: Vec<usize> = found.iter().map(|p| p.index).collect();
        assert_eq!(indices, vec![3]);
    }

    #[test]
    fn test_find_peaks_prominence_filter_applied_after_distance() {
        let x = vec![0.0, 10.0, 0.0, 1.0, 0.0, 1.0, 0.0, 10.0, 0.0];
        let opts = FindPeaksOptions {
            height: None,
            distance: None,
            prominence: Some(5.0),
        };
        let found = find_peaks(&x, &opts).unwrap();
        let indices: Vec<usize> = found.iter().map(|p| p.index).collect();
        assert_eq!(indices, vec![1, 7]);
    }

    #[test]
    fn test_find_peaks_rejects_distance_below_one() {
        let x = vec![1.0, 2.0, 1.0, 2.0, 1.0];
        let opts = FindPeaksOptions {
            height: None,
            distance: Some(0.5),
            prominence: None,
        };
        assert!(matches!(
            find_peaks(&x, &opts),
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_find_peaks_rejects_non_finite_options() {
        let x = vec![1.0, 2.0, 1.0, 2.0, 1.0];
        assert!(matches!(
            find_peaks(
                &x,
                &FindPeaksOptions {
                    height: Some(f64::NAN),
                    distance: None,
                    prominence: None
                }
            ),
            Err(ForecastError::InvalidParameter { .. })
        ));
        assert!(matches!(
            find_peaks(
                &x,
                &FindPeaksOptions {
                    height: None,
                    distance: Some(f64::INFINITY),
                    prominence: None
                }
            ),
            Err(ForecastError::InvalidParameter { .. })
        ));
        assert!(matches!(
            find_peaks(
                &x,
                &FindPeaksOptions {
                    height: None,
                    distance: None,
                    prominence: Some(f64::NEG_INFINITY)
                }
            ),
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_find_peaks_never_panics_on_nan_input() {
        let x = vec![1.0, f64::NAN, 3.0, f64::NAN, 2.0, 5.0, 1.0];
        let result = find_peaks(&x, &FindPeaksOptions::default());
        assert!(
            result.is_ok(),
            "find_peaks must not panic/error on NaN input"
        );
        // Value check: NaN positions (1, 3) are skipped as candidates; the
        // only local maximum among the finite values (1.0, 3.0, 2.0, 5.0,
        // 1.0 at indices 0, 2, 4, 5, 6) is index 5 (value 5.0).
        let peaks = result.unwrap();
        assert_eq!(peaks.len(), 1);
        assert_eq!(peaks[0].index, 5);
        assert_eq!(peaks[0].height, 5.0);
        assert_eq!(peaks[0].prominence, 3.0);
    }

    /// scipy's own linear (`wlen=None`) prominence scan, used only to
    /// cross-check the `O(n)` monotonic-stack port (`peak_prominences`) --
    /// NOT a re-export of production code.
    fn naive_peak_prominence(x: &[f64], peak: usize) -> f64 {
        let n = x.len();
        let mut left_min = x[peak];
        let mut i = peak;
        loop {
            if x[i] <= x[peak] {
                if x[i] < left_min {
                    left_min = x[i];
                }
            } else {
                break;
            }
            if i == 0 {
                break;
            }
            i -= 1;
        }
        let mut right_min = x[peak];
        let mut j = peak;
        while j < n && x[j] <= x[peak] {
            if x[j] < right_min {
                right_min = x[j];
            }
            j += 1;
        }
        x[peak] - left_min.max(right_min)
    }

    /// Deterministic xorshift64 PRNG so the cross-check below needs no
    /// extra crate dependency and is byte-reproducible across runs.
    struct Xorshift64(u64);
    impl Xorshift64 {
        fn next_f64_0_5(&mut self) -> f64 {
            let mut x = self.0;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            self.0 = x;
            (x % 51) as f64 / 10.0 // 0.0..=5.0 in steps of 0.1 -> plateaus/ties likely
        }
    }

    #[test]
    fn test_peak_prominences_matches_naive_scan_on_random_series() {
        let mut rng = Xorshift64(0x9E3779B97F4A7C15);
        for trial in 0..300u32 {
            let n = 2 + (trial as usize % 199); // lengths 2..=200
            let x: Vec<f64> = (0..n).map(|_| rng.next_f64_0_5()).collect();
            let peaks = local_maxima(&x);
            if peaks.is_empty() {
                continue;
            }
            let fast = peak_prominences(&x, &peaks);
            for (k, &p) in peaks.iter().enumerate() {
                let naive = naive_peak_prominence(&x, p);
                assert!(
                    (fast[k] - naive).abs() <= 1e-12,
                    "trial {trial} n {n} peak {p}: fast {} naive {} x {x:?}",
                    fast[k],
                    naive
                );
            }
        }
    }

    #[test]
    fn test_prominence_performance_on_long_trending_sawtooth() {
        let n = 200_000usize;
        let x: Vec<f64> = (0..n)
            .map(|i| {
                if i % 2 == 1 {
                    0.01 * i as f64 + 0.5
                } else {
                    0.01 * i as f64
                }
            })
            .collect();
        let start = std::time::Instant::now();
        let found = find_peaks(&x, &FindPeaksOptions::default()).unwrap();
        let elapsed = start.elapsed();
        assert!(
            elapsed.as_secs_f64() < 5.0,
            "find_peaks on a 200000-point trending sawtooth took {elapsed:?}, expected < 5s \
             (the naive O(n) per-peak scipy-style scan is quadratic here)"
        );
        assert!(
            found.len() > 90_000,
            "expected roughly 100000 peaks on an every-other-sample sawtooth, got {}",
            found.len()
        );
    }

    #[test]
    fn test_detect_peaks_min_distance_macro_default_is_noop() {
        // ts_detect_peaks_by's macro default min_distance is 1.0; scipy
        // requires distance >= 1 and ceil(1.0) == 1 never suppresses a
        // local-maxima sequence (consecutive local maxima are always more
        // than 1 apart), so Some(1.0) must behave identically to None.
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let with_default = detect_peaks(&values, Some(1.0), None, false, None).unwrap();
        let without = detect_peaks(&values, None, None, false, None).unwrap();
        assert_eq!(with_default.n_peaks, without.n_peaks);
    }

    #[test]
    fn test_detect_peaks_min_distance_filters_close_peaks() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let filtered = detect_peaks(&values, Some(20.0), None, false, None).unwrap();
        let unfiltered = detect_peaks(&values, None, None, false, None).unwrap();
        assert!(
            filtered.n_peaks < unfiltered.n_peaks,
            "min_distance=20 should suppress some of the period-12 peaks"
        );
    }

    #[test]
    fn test_detect_peaks_min_prominence_filters_all_peaks() {
        let values = generate_seasonal_series(120, 12.0, 5.0);
        let filtered = detect_peaks(&values, None, Some(100.0), false, None).unwrap();
        assert_eq!(
            filtered.n_peaks, 0,
            "a prominence threshold far above the series' own amplitude swing should drop every peak"
        );
    }

    #[test]
    fn test_detect_peaks_smooth_first_reports_smoothed_values() {
        let mut values = generate_seasonal_series(96, 12.0, 5.0);
        for (i, v) in values.iter_mut().enumerate() {
            if i % 7 == 0 {
                *v += 0.3; // deterministic jitter a Fourier smooth should remove
            }
        }
        let smoothed = detect_peaks(&values, None, None, true, None).unwrap();
        let raw = detect_peaks(&values, None, None, false, None).unwrap();
        assert_ne!(
            smoothed.peaks.iter().map(|p| p.value).collect::<Vec<_>>(),
            raw.peaks.iter().map(|p| p.value).collect::<Vec<_>>(),
            "smooth_first=true should report smoothed values, not raw ones"
        );
        let idxs: Vec<usize> = smoothed.peaks.iter().map(|p| p.index).collect();
        assert!(
            idxs.windows(2).all(|w| w[0] < w[1]),
            "smoothed peak indices must be strictly increasing: {idxs:?}"
        );
    }
}
