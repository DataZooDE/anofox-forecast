//! Data quality assessment functions.

use crate::error::Result;

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
    /// Number of gaps detected
    pub n_gaps: usize,
    /// Number of missing values
    pub n_missing: usize,
    /// Whether the series is constant
    pub is_constant: bool,
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

/// Compute data quality metrics for a series.
pub fn compute_data_quality(values: &[Option<f64>], dates: Option<&[i64]>) -> Result<DataQuality> {
    let n = values.len();

    if n == 0 {
        return Ok(DataQuality::default());
    }

    // Count missing values
    let n_missing = values.iter().filter(|v| v.is_none()).count();
    let _missing_ratio = n_missing as f64 / n as f64;

    // Count gaps in dates
    let n_gaps = if let Some(d) = dates {
        count_gaps(d)
    } else {
        0
    };
    let _gap_ratio = n_gaps as f64 / n.max(1) as f64;

    // Check if constant
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();
    let is_constant = if non_null.len() < 2 {
        true
    } else {
        let first = non_null[0];
        non_null.iter().all(|v| (v - first).abs() < f64::EPSILON)
    };

    // Calculate scores
    let structural_score = calculate_structural_score(&non_null, n_missing);
    let temporal_score = calculate_temporal_score(n_gaps, n);
    let magnitude_score = calculate_magnitude_score(&non_null);
    let behavioral_score = calculate_behavioral_score(&non_null);

    let overall_score =
        (structural_score + temporal_score + magnitude_score + behavioral_score) / 4.0;

    Ok(DataQuality {
        structural_score,
        temporal_score,
        magnitude_score,
        behavioral_score,
        overall_score,
        n_gaps,
        n_missing,
        is_constant,
    })
}

/// Generate a quality report for multiple series.
pub fn generate_quality_report(
    series_list: &[Vec<Option<f64>>],
    thresholds: &QualityThresholds,
) -> QualityReport {
    let mut report = QualityReport {
        n_total: series_list.len(),
        ..Default::default()
    };

    for values in series_list {
        let quality = compute_data_quality(values, None).unwrap_or_default();

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

fn count_gaps(dates: &[i64]) -> usize {
    if dates.len() < 2 {
        return 0;
    }

    let mut sorted = dates.to_vec();
    sorted.sort();

    // Detect typical frequency
    let diffs: Vec<i64> = sorted.windows(2).map(|w| w[1] - w[0]).collect();
    let mode = find_mode(&diffs);

    // Count gaps (where diff > 1.5 * mode)
    diffs
        .iter()
        .filter(|&&d| d > (mode as f64 * 1.5) as i64)
        .count()
}

fn find_mode(values: &[i64]) -> i64 {
    if values.is_empty() {
        return 1;
    }

    let mut counts = std::collections::HashMap::new();
    for &v in values {
        *counts.entry(v).or_insert(0) += 1;
    }

    counts
        .into_iter()
        .max_by_key(|(_, count)| *count)
        .map(|(val, _)| val)
        .unwrap_or(1)
}

fn calculate_structural_score(values: &[f64], n_missing: usize) -> f64 {
    if values.is_empty() && n_missing > 0 {
        return 0.0;
    }

    let total = values.len() + n_missing;
    let completeness = values.len() as f64 / total as f64;

    // Penalize for very short series
    let length_factor = (values.len() as f64 / 30.0).min(1.0);

    (completeness * 0.7 + length_factor * 0.3).clamp(0.0, 1.0)
}

fn calculate_temporal_score(n_gaps: usize, n: usize) -> f64 {
    if n == 0 {
        return 0.0;
    }

    let gap_ratio = n_gaps as f64 / n as f64;
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

fn calculate_behavioral_score(values: &[f64]) -> f64 {
    if values.len() < 3 {
        return 0.5;
    }

    // Check for constant series
    let mean: f64 = values.iter().sum::<f64>() / values.len() as f64;
    let variance: f64 =
        values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / values.len() as f64;

    if variance.abs() < f64::EPSILON {
        return 0.0; // Constant series
    }

    // Check for reasonable autocorrelation
    let acf1 = autocorrelation(values, 1);

    // Very high autocorrelation might indicate issues
    let acf_penalty: f64 = if acf1.abs() > 0.95 { 0.2 } else { 0.0 };

    (1.0_f64 - acf_penalty).clamp(0.0, 1.0)
}

fn autocorrelation(values: &[f64], lag: usize) -> f64 {
    if values.len() <= lag {
        return 0.0;
    }

    let n = values.len();
    let mean: f64 = values.iter().sum::<f64>() / n as f64;

    let mut num = 0.0;
    let mut denom = 0.0;

    for (i, &v) in values.iter().enumerate() {
        denom += (v - mean).powi(2);
        if i >= lag {
            num += (v - mean) * (values[i - lag] - mean);
        }
    }

    if denom.abs() < f64::EPSILON {
        0.0
    } else {
        num / denom
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compute_data_quality() {
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), None, Some(4.0), Some(5.0)];

        let quality = compute_data_quality(&values, None).unwrap();
        assert_eq!(quality.n_missing, 1);
        assert!(!quality.is_constant);
    }

    #[test]
    fn test_constant_series() {
        let values: Vec<Option<f64>> = vec![Some(5.0); 10];
        let quality = compute_data_quality(&values, None).unwrap();
        assert!(quality.is_constant);
    }

    #[test]
    fn test_structural_score() {
        // Complete series with good length
        let values: Vec<f64> = (0..50).map(|i| i as f64).collect();
        let score = calculate_structural_score(&values, 0);
        assert!(score > 0.9, "Complete long series should have high score");

        // Series with many missing values
        let values_with_missing: Vec<f64> = vec![1.0, 2.0, 3.0];
        let score_with_missing = calculate_structural_score(&values_with_missing, 10);
        assert!(
            score_with_missing < score,
            "Series with missing values should have lower score"
        );

        // Empty with missing
        let empty_score = calculate_structural_score(&[], 5);
        assert_eq!(
            empty_score, 0.0,
            "Empty series with missing values should have 0 score"
        );
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

        // Empty series
        let score_empty = calculate_temporal_score(0, 0);
        assert_eq!(score_empty, 0.0, "Empty series should have 0 temporal score");
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
        assert_eq!(empty_score, 0.0, "Empty series should have 0 magnitude score");
    }

    #[test]
    fn test_behavioral_score() {
        // Constant series (zero variance)
        let constant_values: Vec<f64> = vec![5.0; 20];
        let constant_score = calculate_behavioral_score(&constant_values);
        assert_eq!(
            constant_score, 0.0,
            "Constant series should have 0 behavioral score"
        );

        // Series with reasonable variation
        let varied_values: Vec<f64> = (0..50).map(|i| (i as f64 * 0.1).sin() * 10.0).collect();
        let varied_score = calculate_behavioral_score(&varied_values);
        assert!(
            varied_score > 0.5,
            "Varied series should have reasonable behavioral score"
        );

        // Short series
        let short_values: Vec<f64> = vec![1.0, 2.0];
        let short_score = calculate_behavioral_score(&short_values);
        assert_eq!(
            short_score, 0.5,
            "Very short series should return default score"
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

        let quality = compute_data_quality(&values, None).unwrap();

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
    fn test_count_gaps() {
        // No gaps - regular intervals
        let dates_regular: Vec<i64> = (0..10).map(|i| i * 1000).collect();
        assert_eq!(count_gaps(&dates_regular), 0);

        // With gaps
        let dates_with_gap: Vec<i64> = vec![0, 1000, 2000, 5000, 6000, 7000];
        assert!(count_gaps(&dates_with_gap) >= 1);

        // Empty and single element
        assert_eq!(count_gaps(&[]), 0);
        assert_eq!(count_gaps(&[1000]), 0);
    }
}
