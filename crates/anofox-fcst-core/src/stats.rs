//! Time series statistics computation.
//!
//! Provides ts_stats functionality that computes 24 metrics per series.

use crate::error::Result;

/// Time series statistics result containing 24 metrics.
#[derive(Debug, Clone, Default)]
pub struct TsStats {
    /// Total number of observations
    pub length: usize,
    /// Number of NULL values
    pub n_nulls: usize,
    /// Number of zero values
    pub n_zeros: usize,
    /// Number of positive values
    pub n_positive: usize,
    /// Number of negative values
    pub n_negative: usize,
    /// Arithmetic mean
    pub mean: f64,
    /// Median (50th percentile)
    pub median: f64,
    /// Standard deviation
    pub std_dev: f64,
    /// Variance
    pub variance: f64,
    /// Minimum value
    pub min: f64,
    /// Maximum value
    pub max: f64,
    /// Range (max - min)
    pub range: f64,
    /// Sum of all values
    pub sum: f64,
    /// Skewness (measure of asymmetry)
    pub skewness: f64,
    /// Kurtosis (measure of tailedness)
    pub kurtosis: f64,
    /// Coefficient of variation (std_dev / mean)
    pub coef_variation: f64,
    /// First quartile (25th percentile)
    pub q1: f64,
    /// Third quartile (75th percentile)
    pub q3: f64,
    /// Interquartile range (q3 - q1)
    pub iqr: f64,
    /// Autocorrelation at lag 1
    pub autocorr_lag1: f64,
    /// Trend strength (0-1)
    pub trend_strength: f64,
    /// Seasonality strength (0-1)
    pub seasonality_strength: f64,
    /// Approximate entropy
    pub entropy: f64,
    /// Stability measure
    pub stability: f64,
}

/// Compute time series statistics for a series with potential NULL values.
///
/// # Arguments
/// * `series` - A slice of optional f64 values (None represents NULL)
///
/// # Returns
/// * `Result<TsStats>` - Statistics for the series
pub fn compute_ts_stats(series: &[Option<f64>]) -> Result<TsStats> {
    let length = series.len();

    if length == 0 {
        return Ok(TsStats::default());
    }

    // Count NULLs and extract valid values
    let mut n_nulls = 0;
    let mut values: Vec<f64> = Vec::with_capacity(length);

    for val in series {
        match val {
            Some(v) => values.push(*v),
            None => n_nulls += 1,
        }
    }

    let n_valid = values.len();

    if n_valid == 0 {
        return Ok(TsStats {
            length,
            n_nulls,
            ..Default::default()
        });
    }

    // Count zeros, positives, negatives
    let n_zeros = values.iter().filter(|&&v| v == 0.0).count();
    let n_positive = values.iter().filter(|&&v| v > 0.0).count();
    let n_negative = values.iter().filter(|&&v| v < 0.0).count();

    // Basic statistics
    let sum: f64 = values.iter().sum();
    let mean = sum / n_valid as f64;

    let min = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let max = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let range = max - min;

    // Variance and standard deviation
    let variance = if n_valid > 1 {
        values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / (n_valid - 1) as f64
    } else {
        0.0
    };
    let std_dev = variance.sqrt();

    // Coefficient of variation
    let coef_variation = if mean.abs() > f64::EPSILON {
        std_dev / mean.abs()
    } else {
        f64::NAN
    };

    // Median and quartiles
    let mut sorted = values.clone();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    let median = percentile(&sorted, 0.5);
    let q1 = percentile(&sorted, 0.25);
    let q3 = percentile(&sorted, 0.75);
    let iqr = q3 - q1;

    // Skewness (Fisher's)
    let skewness = if n_valid > 2 && std_dev > f64::EPSILON {
        let m3 = values.iter().map(|v| (v - mean).powi(3)).sum::<f64>() / n_valid as f64;
        m3 / std_dev.powi(3)
    } else {
        f64::NAN
    };

    // Kurtosis (excess)
    let kurtosis = if n_valid > 3 && std_dev > f64::EPSILON {
        let m4 = values.iter().map(|v| (v - mean).powi(4)).sum::<f64>() / n_valid as f64;
        (m4 / std_dev.powi(4)) - 3.0
    } else {
        f64::NAN
    };

    // Autocorrelation at lag 1
    let autocorr_lag1 = compute_autocorrelation(&values, 1);

    // Trend and seasonality strength (simplified)
    let (trend_strength, seasonality_strength) = compute_strength_metrics(&values);

    // Entropy (approximate)
    let entropy = compute_approximate_entropy(&values);

    // Stability (inverse of coefficient of variation of rolling means)
    let stability = compute_stability(&values);

    Ok(TsStats {
        length,
        n_nulls,
        n_zeros,
        n_positive,
        n_negative,
        mean,
        median,
        std_dev,
        variance,
        min,
        max,
        range,
        sum,
        skewness,
        kurtosis,
        coef_variation,
        q1,
        q3,
        iqr,
        autocorr_lag1,
        trend_strength,
        seasonality_strength,
        entropy,
        stability,
    })
}

/// Compute percentile using linear interpolation.
fn percentile(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() {
        return f64::NAN;
    }
    if sorted.len() == 1 {
        return sorted[0];
    }

    let n = sorted.len() as f64;
    let idx = p * (n - 1.0);
    let lower = idx.floor() as usize;
    let upper = idx.ceil() as usize;
    let frac = idx - lower as f64;

    if upper >= sorted.len() {
        sorted[sorted.len() - 1]
    } else {
        sorted[lower] * (1.0 - frac) + sorted[upper] * frac
    }
}

/// Compute autocorrelation at a given lag.
fn compute_autocorrelation(values: &[f64], lag: usize) -> f64 {
    if values.len() <= lag {
        return f64::NAN;
    }

    let n = values.len();
    let mean: f64 = values.iter().sum::<f64>() / n as f64;

    let mut numerator = 0.0;
    let mut denominator = 0.0;

    for (i, &v) in values.iter().enumerate() {
        denominator += (v - mean).powi(2);
        if i >= lag {
            numerator += (v - mean) * (values[i - lag] - mean);
        }
    }

    if denominator.abs() < f64::EPSILON {
        0.0
    } else {
        numerator / denominator
    }
}

/// Compute trend and seasonality strength (simplified version).
fn compute_strength_metrics(values: &[f64]) -> (f64, f64) {
    if values.len() < 4 {
        return (0.0, 0.0);
    }

    // Simple trend strength: based on linear regression R-squared
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

    let trend_strength = if ss_xx.abs() > f64::EPSILON && ss_yy.abs() > f64::EPSILON {
        (ss_xy.powi(2) / (ss_xx * ss_yy)).sqrt().clamp(0.0, 1.0)
    } else {
        0.0
    };

    // Simple seasonality strength: based on autocorrelation at common lags
    let acf_2 = compute_autocorrelation(values, 2).abs();
    let acf_4 = compute_autocorrelation(values, 4).abs();
    let acf_7 = compute_autocorrelation(values, 7).abs();
    let acf_12 = compute_autocorrelation(values, 12).abs();

    let seasonality_strength = [acf_2, acf_4, acf_7, acf_12]
        .iter()
        .filter(|v| v.is_finite())
        .cloned()
        .fold(0.0_f64, f64::max)
        .clamp(0.0, 1.0);

    (trend_strength, seasonality_strength)
}

/// Compute approximate entropy (simplified version).
fn compute_approximate_entropy(values: &[f64]) -> f64 {
    if values.len() < 10 {
        return f64::NAN;
    }

    // Use a histogram-based entropy approximation
    let min = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let max = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let range = max - min;

    if range.abs() < f64::EPSILON {
        return 0.0; // Constant series has zero entropy
    }

    let n_bins = 10;
    let mut bins = vec![0usize; n_bins];

    for &v in values {
        let bin = (((v - min) / range) * (n_bins - 1) as f64).round() as usize;
        let bin = bin.min(n_bins - 1);
        bins[bin] += 1;
    }

    let n = values.len() as f64;
    let mut entropy = 0.0;
    for &count in &bins {
        if count > 0 {
            let p = count as f64 / n;
            entropy -= p * p.ln();
        }
    }

    entropy
}

/// Compute stability as inverse of coefficient of variation of rolling means.
fn compute_stability(values: &[f64]) -> f64 {
    if values.len() < 10 {
        return f64::NAN;
    }

    let window = (values.len() / 5).max(3);
    let mut rolling_means = Vec::new();

    for i in 0..=(values.len() - window) {
        let mean: f64 = values[i..i + window].iter().sum::<f64>() / window as f64;
        rolling_means.push(mean);
    }

    if rolling_means.is_empty() {
        return f64::NAN;
    }

    let rm_mean: f64 = rolling_means.iter().sum::<f64>() / rolling_means.len() as f64;
    let rm_std = (rolling_means
        .iter()
        .map(|v| (v - rm_mean).powi(2))
        .sum::<f64>()
        / rolling_means.len() as f64)
        .sqrt();

    if rm_mean.abs() > f64::EPSILON {
        1.0 / (rm_std / rm_mean.abs() + 0.01) // Add small constant to avoid division by zero
    } else {
        f64::NAN
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use approx::assert_relative_eq;

    #[test]
    fn test_basic_stats() {
        let series: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0), Some(4.0), Some(5.0)];
        let stats = compute_ts_stats(&series).unwrap();

        assert_eq!(stats.length, 5);
        assert_eq!(stats.n_nulls, 0);
        assert_relative_eq!(stats.mean, 3.0, epsilon = 1e-10);
        assert_relative_eq!(stats.median, 3.0, epsilon = 1e-10);
        assert_relative_eq!(stats.min, 1.0, epsilon = 1e-10);
        assert_relative_eq!(stats.max, 5.0, epsilon = 1e-10);
        assert_relative_eq!(stats.sum, 15.0, epsilon = 1e-10);
    }

    #[test]
    fn test_with_nulls() {
        let series: Vec<Option<f64>> = vec![Some(1.0), None, Some(3.0), None, Some(5.0)];
        let stats = compute_ts_stats(&series).unwrap();

        assert_eq!(stats.length, 5);
        assert_eq!(stats.n_nulls, 2);
        assert_relative_eq!(stats.mean, 3.0, epsilon = 1e-10);
    }

    #[test]
    fn test_empty_series() {
        let series: Vec<Option<f64>> = vec![];
        let stats = compute_ts_stats(&series).unwrap();
        assert_eq!(stats.length, 0);
    }

    #[test]
    fn test_zeros_and_signs() {
        let series: Vec<Option<f64>> =
            vec![Some(-2.0), Some(-1.0), Some(0.0), Some(1.0), Some(2.0)];
        let stats = compute_ts_stats(&series).unwrap();

        assert_eq!(stats.n_zeros, 1);
        assert_eq!(stats.n_positive, 2);
        assert_eq!(stats.n_negative, 2);
    }
}
