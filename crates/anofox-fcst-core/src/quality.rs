//! Data quality assessment functions.

use crate::error::{ForecastError, Result};
use crate::gaps::{gap_counts, resolve_gap_step};
use crate::stats::FrequencyType;

/// Data quality result for a single series.
#[derive(Debug, Clone, Default)]
pub struct DataQuality {
    /// Structural dimension score (0-1)
    pub structural_score: f64,
    /// Temporal dimension score (0-1)
    pub temporal_score: f64,
    /// Magnitude dimension score (0-1)
    pub magnitude_score: f64,
    /// Behavioral dimension score (0-1)
    pub behavioral_score: f64,
    /// Overall quality score (0-1)
    pub overall_score: f64,
    /// Number of missing periods (D-04/D-05): expected timestamps absent under the
    /// declared/detected frequency between the first and last observed date. This is
    /// the "missing periods" meaning, matching `ts_stats`'s `n_gaps` (not the
    /// pre-v0.10.0 "gap occurrence count" -- see [`DataQuality::n_gap_runs`]). Zero
    /// when no `dates` were supplied or no step could be resolved.
    pub n_gaps: usize,
    /// Number of gap occurrences/runs (D-05): the pre-v0.10.0 `n_gaps` meaning --
    /// count of consecutive-pair transitions with at least one missing period, not
    /// the number of missing periods itself. Zero when no `dates` were supplied or
    /// no step could be resolved.
    pub n_gap_runs: usize,
    /// Number of missing values (NULL or NaN, Phase 12 missing-data policy).
    pub n_missing: usize,
    /// Whether the series is constant (computed on present values only).
    pub is_constant: bool,
    /// Whether the number of present values is below the caller's `n_short`
    /// threshold (D-05, WRAP-02).
    pub is_short: bool,
}

/// Quality report thresholds.
#[derive(Debug, Clone)]
pub struct QualityThresholds {
    /// Maximum allowed gap ratio
    pub max_gap_ratio: f64,
    /// Maximum allowed missing ratio
    pub max_missing_ratio: f64,
    /// Minimum series length
    pub min_length: usize,
    /// Minimum non-zero ratio
    pub min_nonzero_ratio: f64,
}

impl Default for QualityThresholds {
    fn default() -> Self {
        Self {
            max_gap_ratio: 0.1,
            max_missing_ratio: 0.2,
            min_length: 10,
            min_nonzero_ratio: 0.5,
        }
    }
}

/// Quality report result.
#[derive(Debug, Clone, Default)]
pub struct QualityReport {
    /// Number of series passing all checks
    pub n_passed: usize,
    /// Number of series with gap issues
    pub n_gap_issues: usize,
    /// Number of series with missing value issues
    pub n_missing_issues: usize,
    /// Number of constant series
    pub n_constant: usize,
    /// Total series analyzed
    pub n_total: usize,
}

/// Compute data quality metrics for a series (D-05, WRAP-02).
///
/// - `dates`: optional per-row timestamps (microseconds since epoch), required to be
///   the same length as `values` when present. `None` means no gap/temporal
///   information is available: `n_gaps`/`n_gap_runs` are `0` and `temporal_score` is
///   `1.0`.
/// - `frequency_micros`/`frequency_type`: resolved via [`resolve_gap_step`] -- a
///   `Fixed` frequency with `frequency_micros <= 0` auto-detects the step from
///   `dates`.
/// - `n_short`: the "not short" present-value threshold; must be `>= 1`
///   (`InvalidInput` otherwise). Drives the structural length factor and
///   [`DataQuality::is_short`].
/// - `period_count`: user-declared calendar multiplier for Monthly/Quarterly/Yearly
///   frequencies (e.g. `2` for `'2mo'`); see [`resolve_gap_step`]'s doc comment
///   (CR-01 fix, 13-03 deferred item). Ignored for `Fixed`.
///
/// `n_missing` counts both `None` and `Some(NaN)` entries (Phase 12 missing-data
/// policy); every other score is computed on the remaining "present" values, in
/// their original order.
pub fn compute_data_quality(
    values: &[Option<f64>],
    dates: Option<&[i64]>,
    frequency_micros: i64,
    frequency_type: FrequencyType,
    n_short: usize,
    period_count: u32,
) -> Result<DataQuality> {
    let n = values.len();

    if n_short == 0 {
        return Err(ForecastError::InvalidInput(
            "n_short must be >= 1".to_string(),
        ));
    }

    if n == 0 {
        return Ok(DataQuality::default());
    }

    if let Some(d) = dates {
        if d.len() != n {
            return Err(ForecastError::InvalidInput(format!(
                "dates and values must have the same length: {} vs {}",
                d.len(),
                n
            )));
        }
    }

    // n_missing counts NULL and NaN (Phase 12 missing-data policy).
    let n_missing = values.iter().filter(|v| is_missing(**v)).count();

    // Present values, in original order (excludes NULL and NaN).
    let present: Vec<f64> = values
        .iter()
        .filter_map(|v| match v {
            Some(x) if !x.is_nan() => Some(*x),
            _ => None,
        })
        .collect();
    let n_present = present.len();
    let is_short = n_present < n_short;

    // Check if constant (on present values only, scale-relative, D-13/G25
    // -- shared with filter::is_constant and stats::compute_ts_stats).
    let is_constant = crate::filter::is_constant_slice(&present);

    // Gap/temporal scoring via the shared missing-period gap counter (13-01, D-04/D-05).
    let (n_gaps, n_gap_runs, temporal_score) = match dates {
        Some(d) => match resolve_gap_step(d, frequency_micros, frequency_type, period_count)? {
            Some(step) => {
                let counts = gap_counts(d, step)?;
                let temporal =
                    calculate_temporal_score(counts.n_missing_periods, counts.expected_length);
                (counts.n_missing_periods, counts.n_gap_runs, temporal)
            }
            None => (0, 0, 1.0),
        },
        None => (0, 0, 1.0),
    };

    // Calculate scores
    let structural_score = calculate_structural_score(n_present, n, n_short);
    let magnitude_score = calculate_magnitude_score(&present);
    let behavioral_score = calculate_behavioral_score(values, &present);

    let overall_score =
        (structural_score + temporal_score + magnitude_score + behavioral_score) / 4.0;

    Ok(DataQuality {
        structural_score,
        temporal_score,
        magnitude_score,
        behavioral_score,
        overall_score,
        n_gaps,
        n_gap_runs,
        n_missing,
        is_constant,
        is_short,
    })
}

/// Generate a quality report for multiple series.
///
/// No date information is available at this level, so gap/temporal scoring always
/// reports "no gaps" (`n_gaps = 0`, `temporal_score = 1.0`); `thresholds.min_length`
/// is used as `n_short`.
pub fn generate_quality_report(
    series_list: &[Vec<Option<f64>>],
    thresholds: &QualityThresholds,
) -> QualityReport {
    let mut report = QualityReport {
        n_total: series_list.len(),
        ..Default::default()
    };

    let n_short = thresholds.min_length.max(1);

    for values in series_list {
        let quality = compute_data_quality(values, None, 0, FrequencyType::Fixed, n_short, 1)
            .unwrap_or_default();

        let n = values.len();
        let gap_ratio = quality.n_gaps as f64 / n.max(1) as f64;
        let missing_ratio = quality.n_missing as f64 / n.max(1) as f64;

        let has_gap_issues = gap_ratio > thresholds.max_gap_ratio;
        let has_missing_issues = missing_ratio > thresholds.max_missing_ratio;

        if has_gap_issues {
            report.n_gap_issues += 1;
        }
        if has_missing_issues {
            report.n_missing_issues += 1;
        }
        if quality.is_constant {
            report.n_constant += 1;
        }

        if !has_gap_issues
            && !has_missing_issues
            && !quality.is_constant
            && n >= thresholds.min_length
        {
            report.n_passed += 1;
        }
    }

    report
}

// Helper functions

/// Returns `true` if the value is missing: `None`, or `Some(NaN)` (Phase 12
/// missing-data policy -- NaN is a second missing-value sentinel).
#[inline]
fn is_missing(value: Option<f64>) -> bool {
    match value {
        None => true,
        Some(v) => v.is_nan(),
    }
}

fn calculate_structural_score(n_present: usize, n: usize, n_short: usize) -> f64 {
    if n == 0 {
        return 0.0;
    }

    let completeness = n_present as f64 / n as f64;

    // Penalize series shorter than the caller's n_short threshold.
    let length_factor = (n_present as f64 / n_short as f64).min(1.0);

    (completeness * 0.7 + length_factor * 0.3).clamp(0.0, 1.0)
}

/// `expected_length == 0` (no dates, or no resolvable step) gives `1.0` -- "no
/// information" is not the same as "many gaps".
fn calculate_temporal_score(n_missing_periods: usize, expected_length: usize) -> f64 {
    if expected_length == 0 {
        return 1.0;
    }

    let gap_ratio = n_missing_periods as f64 / expected_length as f64;
    (1.0 - gap_ratio * 5.0).clamp(0.0, 1.0)
}

fn calculate_magnitude_score(values: &[f64]) -> f64 {
    if values.is_empty() {
        return 0.0;
    }

    let n = values.len() as f64;

    // Check for outliers using IQR
    let mut sorted = values.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    let q1 = sorted[(n * 0.25) as usize];
    let q3 = sorted[(n * 0.75) as usize];
    let iqr = q3 - q1;

    let lower = q1 - 1.5 * iqr;
    let upper = q3 + 1.5 * iqr;

    let outliers = values.iter().filter(|&&v| v < lower || v > upper).count();
    let outlier_ratio = outliers as f64 / n;

    // Check for extreme values
    let mean: f64 = values.iter().sum::<f64>() / n;
    let std: f64 = (values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n).sqrt();

    let extreme_count = values
        .iter()
        .filter(|&&v| (v - mean).abs() > 4.0 * std)
        .count();
    let extreme_ratio = extreme_count as f64 / n;

    (1.0 - outlier_ratio * 2.0 - extreme_ratio * 3.0).clamp(0.0, 1.0)
}

/// Behavioral score (D-06, STAT-04): constancy, robust outliers (Iglewicz-Hoaglin
/// modified z-score) and long zero-runs. No autocorrelation penalty -- a
/// predictable, smooth series is easy to forecast and must not be penalised;
/// constancy carries no signal of its own (handled separately below); robust
/// outliers distort model fits; long zero runs indicate inactive/intermittent
/// periods.
///
/// `original` is the full original (pre-filter) series, used only for the
/// zero-run scan so a NULL/NaN entry correctly breaks a run instead of silently
/// joining two zero runs that straddle a gap. `present` is `original`'s non-
/// missing values, in order (used for every other computation).
fn calculate_behavioral_score(original: &[Option<f64>], present: &[f64]) -> f64 {
    let n_present = present.len();
    if n_present < 3 {
        return 0.5;
    }

    // Constant series: no signal.
    let mean: f64 = present.iter().sum::<f64>() / n_present as f64;
    let variance: f64 = present.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n_present as f64;
    if variance.abs() < f64::EPSILON {
        return 0.0;
    }

    // Robust outlier ratio via the Iglewicz-Hoaglin modified z-score:
    // MADN = 1.482602218505602 * median(|x - median(x)|); flag |x - median| / MADN > 3.5.
    // outlier_ratio = 0 when MADN is 0 (e.g. more than half the present values share
    // the median) so an intermittent series is not flagged wholesale.
    const MADN_CONST: f64 = 1.482602218505602;
    let median = median_of(present);
    let abs_dev: Vec<f64> = present.iter().map(|v| (v - median).abs()).collect();
    let mad = median_of(&abs_dev);
    let madn = MADN_CONST * mad;
    let outlier_ratio = if madn.abs() < f64::EPSILON {
        0.0
    } else {
        let n_outliers = present
            .iter()
            .filter(|&&v| ((v - median) / madn).abs() > 3.5)
            .count();
        n_outliers as f64 / n_present as f64
    };

    // Longest run of consecutive exact zeros in the ORIGINAL series; a NULL/NaN
    // entry breaks the run (does not count toward it, and does not merge two runs
    // across the gap). Ratio is against n_present, not the original length.
    let mut longest = 0usize;
    let mut current = 0usize;
    for v in original {
        match v {
            Some(x) if !x.is_nan() && *x == 0.0 => {
                current += 1;
                longest = longest.max(current);
            }
            _ => current = 0,
        }
    }
    let zero_run_ratio = longest as f64 / n_present as f64;

    (1.0 - 2.0 * outlier_ratio - zero_run_ratio).clamp(0.0, 1.0)
}

/// Median of a slice (sorts a copy; average of the two middle elements for an
/// even-length input). Returns `0.0` for an empty slice.
fn median_of(values: &[f64]) -> f64 {
    if values.is_empty() {
        return 0.0;
    }
    let mut sorted = values.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let n = sorted.len();
    if n % 2 == 1 {
        sorted[n / 2]
    } else {
        (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::calendar::CalendarStep;
    use crate::gaps::detect_frequency;

    const ONE_DAY_MICROS: i64 = 86_400_000_000;

    fn daily_dates(n: usize) -> Vec<i64> {
        (0..n as i64).map(|i| i * ONE_DAY_MICROS).collect()
    }

    #[test]
    fn test_compute_data_quality() {
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), None, Some(4.0), Some(5.0)];

        let quality = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 1, 1).unwrap();
        assert_eq!(quality.n_missing, 1);
        assert!(!quality.is_constant);
    }

    #[test]
    fn test_n_missing_counts_nan_and_null() {
        let values: Vec<Option<f64>> = vec![Some(1.0), None, Some(f64::NAN), Some(4.0), Some(5.0)];
        let quality = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 1, 1).unwrap();
        assert_eq!(quality.n_missing, 2, "NULL and NaN both count as missing");
    }

    #[test]
    fn test_n_short_zero_is_invalid_input() {
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0)];
        let err = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 0, 1).unwrap_err();
        assert!(matches!(err, ForecastError::InvalidInput(_)));
    }

    #[test]
    fn test_dates_length_mismatch_is_invalid_input() {
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];
        let dates = daily_dates(2);
        let err =
            compute_data_quality(&values, Some(&dates), 0, FrequencyType::Fixed, 1, 1).unwrap_err();
        assert!(matches!(err, ForecastError::InvalidInput(_)));
    }

    #[test]
    fn test_is_short_flag() {
        let values: Vec<Option<f64>> = (0..5).map(|i| Some(i as f64)).collect();
        let short = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 10, 1).unwrap();
        assert!(short.is_short);

        let not_short = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 3, 1).unwrap();
        assert!(!not_short.is_short);
    }

    #[test]
    fn test_real_gap_count_and_temporal_below_one() {
        // 60 daily dates with a 20-day hole (days 20..39 missing): 40 present values,
        // n_gaps (missing periods) = 20, temporal_score below 1.0 (D-05).
        let mut dates: Vec<i64> = Vec::new();
        for i in 0..20 {
            dates.push(i * ONE_DAY_MICROS);
        }
        for i in 40..60 {
            dates.push(i * ONE_DAY_MICROS);
        }
        let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();

        let quality =
            compute_data_quality(&values, Some(&dates), 0, FrequencyType::Fixed, 1, 1).unwrap();
        assert_eq!(quality.n_gaps, 20);
        assert_eq!(quality.n_gap_runs, 1);
        assert!(quality.temporal_score < 1.0);
    }

    #[test]
    fn test_no_dates_gives_perfect_temporal_score() {
        let values: Vec<Option<f64>> = (0..10).map(|i| Some(i as f64)).collect();
        let quality = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 1, 1).unwrap();
        assert_eq!(quality.n_gaps, 0);
        assert_eq!(quality.n_gap_runs, 0);
        assert_eq!(quality.temporal_score, 1.0);
    }

    #[test]
    fn test_determinism_on_tied_step_data() {
        // diffs: 1, 2 (tie) -> smaller (1 day) wins, every time (detect_step's tie rule).
        let dates = vec![0, ONE_DAY_MICROS, 3 * ONE_DAY_MICROS];
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];
        let mut results = Vec::new();
        for _ in 0..20 {
            let q =
                compute_data_quality(&values, Some(&dates), 0, FrequencyType::Fixed, 1, 1).unwrap();
            results.push((q.n_gaps, q.n_gap_runs));
        }
        assert!(results.iter().all(|r| *r == results[0]));
        // detect_frequency on this pattern resolves to the smaller (1-day) step, so the
        // 2-day jump is 1 missing period.
        assert_eq!(detect_frequency(&dates).unwrap(), ONE_DAY_MICROS);
        assert_eq!(results[0], (1, 1));
    }

    #[test]
    fn test_constant_series() {
        let values: Vec<Option<f64>> = vec![Some(5.0); 10];
        let quality = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 1, 1).unwrap();
        assert!(quality.is_constant);
    }

    /// D-13, G25: compute_data_quality's is_constant uses the SAME
    /// scale-relative tolerance as filter::is_constant_slice -- a large
    /// value plus a tiny absolute wiggle is now constant.
    #[test]
    fn test_constant_series_scale_relative() {
        let values: Vec<Option<f64>> = vec![Some(1e12), Some(1e12 + 0.001)];
        let quality = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 1, 1).unwrap();
        assert!(quality.is_constant);
        assert!(crate::filter::is_constant_slice(&[1e12, 1e12 + 0.001]));
    }

    #[test]
    fn test_structural_score() {
        // Complete, not-short series
        let score = calculate_structural_score(50, 50, 10);
        assert!(score > 0.9, "Complete long series should have high score");

        // Series with many missing values (30 present out of 100 total)
        let score_with_missing = calculate_structural_score(3, 13, 10);
        assert!(
            score_with_missing < score,
            "Series with missing values should have lower score"
        );

        // Empty
        let empty_score = calculate_structural_score(0, 0, 10);
        assert_eq!(empty_score, 0.0, "Empty series should have 0 score");
    }

    #[test]
    fn test_temporal_score() {
        // No gaps
        let score_no_gaps = calculate_temporal_score(0, 100);
        assert_eq!(score_no_gaps, 1.0, "No gaps should give perfect score");

        // Some gaps
        let score_some_gaps = calculate_temporal_score(5, 100);
        assert!(
            score_some_gaps < 1.0 && score_some_gaps > 0.0,
            "Some gaps should reduce score"
        );

        // Many gaps
        let score_many_gaps = calculate_temporal_score(50, 100);
        assert!(
            score_many_gaps < score_some_gaps,
            "More gaps should give lower score"
        );

        // No expected_length (no dates / no resolvable step) is "no information", not
        // "many gaps".
        let score_no_info = calculate_temporal_score(0, 0);
        assert_eq!(
            score_no_info, 1.0,
            "No expected_length should give perfect score (no information)"
        );
    }

    #[test]
    fn test_magnitude_score() {
        // Normal distribution - no outliers
        let normal_values: Vec<f64> = (0..100).map(|i| 50.0 + (i % 10) as f64 - 5.0).collect();
        let normal_score = calculate_magnitude_score(&normal_values);
        assert!(
            normal_score > 0.8,
            "Normal data should have high magnitude score"
        );

        // Data with outliers
        let mut outlier_values: Vec<f64> = (0..100).map(|i| i as f64).collect();
        outlier_values.push(1000.0); // Add extreme outlier
        outlier_values.push(-500.0); // Add another extreme outlier
        let outlier_score = calculate_magnitude_score(&outlier_values);
        assert!(
            outlier_score < normal_score,
            "Data with outliers should have lower score"
        );

        // Empty series
        let empty_score = calculate_magnitude_score(&[]);
        assert_eq!(
            empty_score, 0.0,
            "Empty series should have 0 magnitude score"
        );
    }

    #[test]
    fn test_behavioral_score() {
        // Constant series (zero variance)
        let constant_values: Vec<f64> = vec![5.0; 20];
        let constant_original: Vec<Option<f64>> =
            constant_values.iter().map(|&v| Some(v)).collect();
        let constant_score = calculate_behavioral_score(&constant_original, &constant_values);
        assert_eq!(
            constant_score, 0.0,
            "Constant series should have 0 behavioral score"
        );

        // Smooth, predictable series (|ACF1| > 0.95): no penalty (D-06 removes the old
        // ACF1 penalty entirely).
        let varied_values: Vec<f64> = (0..50)
            .map(|i| (i as f64 * 0.1).sin() * 10.0 + 20.0)
            .collect();
        let varied_original: Vec<Option<f64>> = varied_values.iter().map(|&v| Some(v)).collect();
        let varied_score = calculate_behavioral_score(&varied_original, &varied_values);
        assert!(
            varied_score > 0.5,
            "Varied series should have reasonable behavioral score"
        );

        // Short series
        let short_values: Vec<f64> = vec![1.0, 2.0];
        let short_original: Vec<Option<f64>> = short_values.iter().map(|&v| Some(v)).collect();
        let short_score = calculate_behavioral_score(&short_original, &short_values);
        assert_eq!(
            short_score, 0.5,
            "Very short series should return default score"
        );
    }

    #[test]
    fn test_behavioral_score_two_outliers_in_100() {
        let mut values: Vec<f64> = (0..100)
            .map(|i| 10.0 + 0.1 * ((i % 7) as f64 - 3.0))
            .collect();
        values[10] = 1000.0;
        values[55] = -800.0;
        let original: Vec<Option<f64>> = values.iter().map(|&v| Some(v)).collect();
        let score = calculate_behavioral_score(&original, &values);
        assert!(
            (score - 0.96).abs() < 1e-9,
            "expected 2 outliers in 100 -> 0.96, got {score}"
        );
    }

    #[test]
    fn test_behavioral_score_zero_run_breaks_on_null() {
        // A 30-long run of zeros, split in the middle by a NULL entry: the two
        // sub-runs (15 each) must NOT merge into a 30-run across the gap.
        let mut original: Vec<Option<f64>> = vec![Some(1.0); 35];
        original.extend(std::iter::repeat_n(Some(0.0), 15));
        original.push(None);
        original.extend(std::iter::repeat_n(Some(0.0), 14));
        original.extend(std::iter::repeat_n(Some(1.0), 35));
        let present: Vec<f64> = original.iter().filter_map(|v| *v).collect();
        let score = calculate_behavioral_score(&original, &present);
        // longest run is 15 (not 29/30), n_present = 99 -> zero_run_ratio = 15/99
        let expected = (1.0 - 15.0 / 99.0_f64).clamp(0.0, 1.0);
        assert!(
            (score - expected).abs() < 1e-9,
            "expected {expected}, got {score}"
        );
    }

    #[test]
    fn test_generate_quality_report() {
        let series_list: Vec<Vec<Option<f64>>> = vec![
            // Good series
            (0..50).map(|i| Some(i as f64)).collect(),
            // Series with many missing values (50% missing)
            (0..50)
                .map(|i| if i % 2 == 0 { Some(i as f64) } else { None })
                .collect(),
            // Constant series
            vec![Some(10.0); 50],
            // Short series
            vec![Some(1.0), Some(2.0), Some(3.0)],
        ];

        let thresholds = QualityThresholds::default();
        let report = generate_quality_report(&series_list, &thresholds);

        assert_eq!(report.n_total, 4);
        assert!(report.n_constant >= 1, "Should detect constant series");
        assert!(report.n_missing_issues >= 1, "Should detect missing issues");
        // At least one series should pass
        assert!(report.n_passed >= 1, "At least one series should pass");
    }

    #[test]
    fn test_data_quality_scores_in_range() {
        // All scores should be between 0 and 1
        let values: Vec<Option<f64>> = (0..100)
            .map(|i| {
                if i % 5 == 0 {
                    None
                } else {
                    Some(i as f64 + (i % 7) as f64)
                }
            })
            .collect();

        let quality = compute_data_quality(&values, None, 0, FrequencyType::Fixed, 10, 1).unwrap();

        assert!(
            quality.structural_score >= 0.0 && quality.structural_score <= 1.0,
            "Structural score should be in [0, 1]"
        );
        assert!(
            quality.temporal_score >= 0.0 && quality.temporal_score <= 1.0,
            "Temporal score should be in [0, 1]"
        );
        assert!(
            quality.magnitude_score >= 0.0 && quality.magnitude_score <= 1.0,
            "Magnitude score should be in [0, 1]"
        );
        assert!(
            quality.behavioral_score >= 0.0 && quality.behavioral_score <= 1.0,
            "Behavioral score should be in [0, 1]"
        );
        assert!(
            quality.overall_score >= 0.0 && quality.overall_score <= 1.0,
            "Overall score should be in [0, 1]"
        );
    }

    #[test]
    fn test_frequency_changes_gap_count() {
        // 60 daily dates with the same 20-day hole as test_real_gap_count_and_temporal_below_one,
        // but declared frequency '2 days' instead of auto-detected '1 day': the 21-day jump
        // across the hole is now 11 steps of 2 days (10 missing periods), per the plan's D-05
        // acceptance criterion.
        let mut dates: Vec<i64> = Vec::new();
        for i in 0..20 {
            dates.push(i * ONE_DAY_MICROS);
        }
        for i in 40..60 {
            dates.push(i * ONE_DAY_MICROS);
        }
        let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();

        let quality = compute_data_quality(
            &values,
            Some(&dates),
            2 * ONE_DAY_MICROS,
            FrequencyType::Fixed,
            1,
            1,
        )
        .unwrap();
        assert_eq!(quality.n_gaps, 10);
    }

    #[test]
    fn test_calendar_period_count_honored_in_data_quality_gap_counting() {
        // CR-01 regression (13-03 deferred item): a correctly-spaced bi-monthly
        // series declared with period_count=2 (i.e. '2mo') must report n_gaps=0,
        // not misclassify every consecutive pair as "1 missing month".
        let dates: Vec<i64> = vec![
            1672531200_i64 * 1_000_000, // 2023-01-01
            1677628800_i64 * 1_000_000, // 2023-03-01
            1682899200_i64 * 1_000_000, // 2023-05-01
            1688169600_i64 * 1_000_000, // 2023-07-01
        ];
        let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();

        let quality =
            compute_data_quality(&values, Some(&dates), 0, FrequencyType::Monthly, 1, 2).unwrap();
        assert_eq!(quality.n_gaps, 0);
        assert_eq!(quality.n_gap_runs, 0);
        assert_eq!(quality.temporal_score, 1.0);

        // Sanity: the same series WITHOUT the multiplier (period_count=1, i.e. the
        // pre-fix hard-coded behavior) correctly reports 3 missing months.
        let quality_unmultiplied =
            compute_data_quality(&values, Some(&dates), 0, FrequencyType::Monthly, 1, 1).unwrap();
        assert_eq!(quality_unmultiplied.n_gaps, 3);
    }

    #[test]
    fn test_n_short_drives_structural_score_and_is_short() {
        let mut dates: Vec<i64> = Vec::new();
        for i in 0..20 {
            dates.push(i * ONE_DAY_MICROS);
        }
        for i in 40..60 {
            dates.push(i * ONE_DAY_MICROS);
        }
        let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();

        let q10 =
            compute_data_quality(&values, Some(&dates), 0, FrequencyType::Fixed, 10, 1).unwrap();
        assert!(!q10.is_short);
        assert_eq!(q10.structural_score, 1.0);

        let q100 =
            compute_data_quality(&values, Some(&dates), 0, FrequencyType::Fixed, 100, 1).unwrap();
        assert!(q100.is_short);
        assert!(
            (q100.structural_score - 0.82).abs() < 1e-10,
            "expected structural_score 0.82, got {}",
            q100.structural_score
        );
    }

    #[test]
    fn test_calendar_step_is_respected() {
        // Sanity: a Months(1) step is usable here too (D-04 shares the same gap
        // counter as ts_stats).
        let dates = vec![0, ONE_DAY_MICROS];
        let step = CalendarStep::Fixed(ONE_DAY_MICROS);
        let counts = gap_counts(&dates, step).unwrap();
        assert_eq!(counts.n_missing_periods, 0);
    }
}
