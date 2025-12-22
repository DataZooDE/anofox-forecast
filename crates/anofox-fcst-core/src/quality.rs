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
}
