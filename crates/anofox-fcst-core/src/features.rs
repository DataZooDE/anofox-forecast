//! Time series feature extraction (tsfresh-compatible).

use crate::error::{ForecastError, Result};
use std::collections::{HashMap, HashSet};

/// Extract all available features from a time series.
pub fn extract_features(values: &[f64]) -> Result<HashMap<String, f64>> {
    if values.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    let mut features = HashMap::new();
    let n = values.len() as f64;

    // Basic statistics
    let sum: f64 = values.iter().sum();
    let mean = sum / n;
    features.insert("length".to_string(), n);
    features.insert("sum".to_string(), sum);
    features.insert("mean".to_string(), mean);

    let min = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let max = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    features.insert("minimum".to_string(), min);
    features.insert("maximum".to_string(), max);
    features.insert("range".to_string(), max - min);

    // Variance and standard deviation
    let variance = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n;
    let std_dev = variance.sqrt();
    features.insert("variance".to_string(), variance);
    features.insert("standard_deviation".to_string(), std_dev);

    // Coefficient of variation
    if mean.abs() > f64::EPSILON {
        features.insert("variation_coefficient".to_string(), std_dev / mean.abs());
    } else {
        features.insert("variation_coefficient".to_string(), f64::NAN);
    }

    // Large standard deviation (std > r * range)
    let r = 0.25; // typical threshold
    let range_val = max - min;
    features.insert(
        "large_standard_deviation".to_string(),
        if std_dev > r * range_val { 1.0 } else { 0.0 },
    );

    // Median
    let mut sorted = values.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    // Note: is_multiple_of() is unstable and breaks WASM builds
    #[allow(clippy::manual_is_multiple_of)]
    let median = if values.len() % 2 == 0 {
        (sorted[values.len() / 2 - 1] + sorted[values.len() / 2]) / 2.0
    } else {
        sorted[values.len() / 2]
    };
    features.insert("median".to_string(), median);

    // Quantiles - extended set
    features.insert("quantile_0.1".to_string(), quantile(&sorted, 0.1));
    features.insert("quantile_0.25".to_string(), quantile(&sorted, 0.25));
    features.insert("quantile_0.75".to_string(), quantile(&sorted, 0.75));
    features.insert("quantile_0.9".to_string(), quantile(&sorted, 0.9));

    // Skewness and Kurtosis
    if std_dev > f64::EPSILON {
        let skewness = values
            .iter()
            .map(|v| ((v - mean) / std_dev).powi(3))
            .sum::<f64>()
            / n;
        features.insert("skewness".to_string(), skewness);

        let kurtosis = values
            .iter()
            .map(|v| ((v - mean) / std_dev).powi(4))
            .sum::<f64>()
            / n
            - 3.0;
        features.insert("kurtosis".to_string(), kurtosis);
    }

    // Counting features
    let count_above_mean = values.iter().filter(|&&v| v > mean).count() as f64;
    let count_below_mean = values.iter().filter(|&&v| v < mean).count() as f64;
    features.insert("count_above_mean".to_string(), count_above_mean);
    features.insert("count_below_mean".to_string(), count_below_mean);
    features.insert("percentage_above_mean".to_string(), count_above_mean / n);

    // Zero crossings
    let zero_crossings = values
        .windows(2)
        .filter(|w| w[0].signum() != w[1].signum() && w[0] != 0.0 && w[1] != 0.0)
        .count() as f64;
    features.insert(
        "zero_crossing_rate".to_string(),
        zero_crossings / (n - 1.0).max(1.0),
    );

    // Mean change
    if values.len() > 1 {
        let changes: Vec<f64> = values.windows(2).map(|w| w[1] - w[0]).collect();
        let mean_change = changes.iter().sum::<f64>() / changes.len() as f64;
        let mean_abs_change = changes.iter().map(|c| c.abs()).sum::<f64>() / changes.len() as f64;
        features.insert("mean_change".to_string(), mean_change);
        features.insert("mean_abs_change".to_string(), mean_abs_change);
    }

    // Extended Autocorrelation - lags 1-10
    for lag in 1..=10 {
        if values.len() > lag {
            features.insert(format!("autocorrelation_lag{}", lag), autocorr(values, lag));
        }
    }

    // Extended Partial autocorrelation - lags 1-5
    for lag in 1..=5 {
        if values.len() > lag + 1 {
            features.insert(
                format!("partial_autocorrelation_lag{}", lag),
                partial_autocorr(values, lag),
            );
        }
    }

    // First and last values
    features.insert("first_value".to_string(), values[0]);
    features.insert("last_value".to_string(), values[values.len() - 1]);

    // Location of min/max
    let (first_max_idx, last_max_idx) = first_last_location_of_value(values, max);
    let (first_min_idx, last_min_idx) = first_last_location_of_value(values, min);
    features.insert("first_location_of_maximum".to_string(), first_max_idx / n);
    features.insert("last_location_of_maximum".to_string(), last_max_idx / n);
    features.insert("first_location_of_minimum".to_string(), first_min_idx / n);
    features.insert("last_location_of_minimum".to_string(), last_min_idx / n);

    // Absolute energy
    let abs_energy: f64 = values.iter().map(|v| v.powi(2)).sum();
    features.insert("abs_energy".to_string(), abs_energy);

    // Root mean square
    features.insert("root_mean_square".to_string(), (abs_energy / n).sqrt());

    // Mean second derivative central
    if values.len() > 2 {
        let second_deriv: f64 = values
            .windows(3)
            .map(|w| w[2] - 2.0 * w[1] + w[0])
            .sum::<f64>()
            / (values.len() - 2) as f64;
        features.insert("mean_second_derivative_central".to_string(), second_deriv);
    }

    // Complexity estimate (CID)
    if values.len() > 1 {
        let cid: f64 = values
            .windows(2)
            .map(|w| (w[1] - w[0]).powi(2))
            .sum::<f64>()
            .sqrt();
        features.insert("cid_ce".to_string(), cid);
    }

    // Absolute sum of changes
    if values.len() > 1 {
        let abs_sum_changes: f64 = values.windows(2).map(|w| (w[1] - w[0]).abs()).sum();
        features.insert("absolute_sum_of_changes".to_string(), abs_sum_changes);
    }

    // Longest strike above/below mean
    features.insert(
        "longest_strike_above_mean".to_string(),
        longest_strike(values, mean, true),
    );
    features.insert(
        "longest_strike_below_mean".to_string(),
        longest_strike(values, mean, false),
    );

    // Number of peaks
    let peaks = count_peaks(values);
    features.insert("number_peaks".to_string(), peaks as f64);

    // Peak count with different thresholds
    features.insert(
        "number_peaks_threshold_1".to_string(),
        count_peaks_threshold(values, 1.0 * std_dev) as f64,
    );
    features.insert(
        "number_peaks_threshold_2".to_string(),
        count_peaks_threshold(values, 2.0 * std_dev) as f64,
    );

    // Benford correlation (first digit distribution)
    features.insert(
        "benford_correlation".to_string(),
        benford_correlation(values),
    );

    // Linear trend
    let (slope, intercept, r_squared) = linear_trend(values);
    features.insert("linear_trend_slope".to_string(), slope);
    features.insert("linear_trend_intercept".to_string(), intercept);
    features.insert("linear_trend_r_squared".to_string(), r_squared);

    // Entropy features
    features.insert("binned_entropy".to_string(), binned_entropy(values, 10));
    features.insert(
        "sample_entropy".to_string(),
        sample_entropy(values, 2, 0.2 * std_dev),
    );
    features.insert(
        "approximate_entropy".to_string(),
        approximate_entropy(values, 2, 0.2 * std_dev),
    );
    features.insert(
        "permutation_entropy".to_string(),
        permutation_entropy(values, 3),
    );

    // Ratio beyond r sigma
    for r in 1..=3 {
        let threshold = r as f64 * std_dev;
        let count = values
            .iter()
            .filter(|&&v| (v - mean).abs() > threshold)
            .count() as f64;
        features.insert(format!("ratio_beyond_r_sigma_{}", r), count / n);
    }

    // Ratio of unique values to length
    let unique_count = count_unique(values) as f64;
    features.insert("count_unique".to_string(), unique_count);
    features.insert("ratio_value_number_to_length".to_string(), unique_count / n);

    // Has duplicate features
    features.insert(
        "has_duplicate".to_string(),
        if has_duplicate(values) { 1.0 } else { 0.0 },
    );
    features.insert(
        "has_duplicate_max".to_string(),
        if has_duplicate_value(values, max) {
            1.0
        } else {
            0.0
        },
    );
    features.insert(
        "has_duplicate_min".to_string(),
        if has_duplicate_value(values, min) {
            1.0
        } else {
            0.0
        },
    );

    // Reoccurring values/datapoints
    let (pct_reoccur_dp, pct_reoccur_val, sum_reoccur_val, sum_reoccur_dp) =
        reoccurring_stats(values);
    features.insert(
        "percentage_of_reoccurring_datapoints_to_all_datapoints".to_string(),
        pct_reoccur_dp,
    );
    features.insert(
        "percentage_of_reoccurring_values_to_all_values".to_string(),
        pct_reoccur_val,
    );
    features.insert("sum_of_reoccurring_values".to_string(), sum_reoccur_val);
    features.insert("sum_of_reoccurring_datapoints".to_string(), sum_reoccur_dp);

    // Time reversal asymmetry
    for lag in 1..=3 {
        if values.len() > 2 * lag {
            features.insert(
                format!("time_reversal_asymmetry_stat_{}", lag),
                time_reversal_asymmetry(values, lag),
            );
        }
    }

    // C3 statistic (nonlinearity measure)
    for lag in 1..=3 {
        if values.len() > 2 * lag {
            features.insert(format!("c3_lag{}", lag), c3(values, lag));
        }
    }

    // Lempel-Ziv complexity
    features.insert(
        "lempel_ziv_complexity".to_string(),
        lempel_ziv_complexity(values, mean),
    );

    // Simplified FFT features (using DFT for small series)
    let fft_coeffs = simple_dft(values);
    for (i, coeff) in fft_coeffs.iter().enumerate().take(10) {
        features.insert(format!("fft_coefficient_{}_real", i), coeff.0);
        features.insert(format!("fft_coefficient_{}_imag", i), coeff.1);
        features.insert(
            format!("fft_coefficient_{}_abs", i),
            (coeff.0.powi(2) + coeff.1.powi(2)).sqrt(),
        );
    }

    // Spectral features
    let (spectral_centroid, spectral_variance) = spectral_features(&fft_coeffs);
    features.insert("spectral_centroid".to_string(), spectral_centroid);
    features.insert("spectral_variance".to_string(), spectral_variance);

    // Aggregated linear trend (chunked)
    let chunk_len = (values.len() / 10).max(2);
    let (agg_slope, agg_intercept, agg_rvalue, agg_stderr) =
        aggregated_linear_trend(values, chunk_len);
    features.insert("agg_linear_trend_slope".to_string(), agg_slope);
    features.insert("agg_linear_trend_intercept".to_string(), agg_intercept);
    features.insert("agg_linear_trend_rvalue".to_string(), agg_rvalue);
    features.insert("agg_linear_trend_stderr".to_string(), agg_stderr);

    Ok(features)
}

/// List all available feature names.
pub fn list_features() -> Vec<String> {
    let mut features = vec![
        // Basic statistics (10)
        "length".to_string(),
        "sum".to_string(),
        "mean".to_string(),
        "minimum".to_string(),
        "maximum".to_string(),
        "range".to_string(),
        "variance".to_string(),
        "standard_deviation".to_string(),
        "variation_coefficient".to_string(),
        "large_standard_deviation".to_string(),
        // Median and quantiles (5)
        "median".to_string(),
        "quantile_0.1".to_string(),
        "quantile_0.25".to_string(),
        "quantile_0.75".to_string(),
        "quantile_0.9".to_string(),
        // Distribution (2)
        "skewness".to_string(),
        "kurtosis".to_string(),
        // Counting features (3)
        "count_above_mean".to_string(),
        "count_below_mean".to_string(),
        "percentage_above_mean".to_string(),
        // Changes (3)
        "zero_crossing_rate".to_string(),
        "mean_change".to_string(),
        "mean_abs_change".to_string(),
        // Values and locations (6)
        "first_value".to_string(),
        "last_value".to_string(),
        "first_location_of_maximum".to_string(),
        "last_location_of_maximum".to_string(),
        "first_location_of_minimum".to_string(),
        "last_location_of_minimum".to_string(),
        // Energy (2)
        "abs_energy".to_string(),
        "root_mean_square".to_string(),
        // Derivatives and complexity (4)
        "mean_second_derivative_central".to_string(),
        "cid_ce".to_string(),
        "absolute_sum_of_changes".to_string(),
        "lempel_ziv_complexity".to_string(),
        // Strikes and peaks (5)
        "longest_strike_above_mean".to_string(),
        "longest_strike_below_mean".to_string(),
        "number_peaks".to_string(),
        "number_peaks_threshold_1".to_string(),
        "number_peaks_threshold_2".to_string(),
        // Correlation and trend (4)
        "benford_correlation".to_string(),
        "linear_trend_slope".to_string(),
        "linear_trend_intercept".to_string(),
        "linear_trend_r_squared".to_string(),
        // Entropy (4)
        "binned_entropy".to_string(),
        "sample_entropy".to_string(),
        "approximate_entropy".to_string(),
        "permutation_entropy".to_string(),
        // Unique and duplicates (7)
        "count_unique".to_string(),
        "ratio_value_number_to_length".to_string(),
        "has_duplicate".to_string(),
        "has_duplicate_max".to_string(),
        "has_duplicate_min".to_string(),
        "percentage_of_reoccurring_datapoints_to_all_datapoints".to_string(),
        "percentage_of_reoccurring_values_to_all_values".to_string(),
        "sum_of_reoccurring_values".to_string(),
        "sum_of_reoccurring_datapoints".to_string(),
        // Spectral (2)
        "spectral_centroid".to_string(),
        "spectral_variance".to_string(),
        // Aggregated trend (4)
        "agg_linear_trend_slope".to_string(),
        "agg_linear_trend_intercept".to_string(),
        "agg_linear_trend_rvalue".to_string(),
        "agg_linear_trend_stderr".to_string(),
    ];

    // Autocorrelation lags 1-10 (10)
    for lag in 1..=10 {
        features.push(format!("autocorrelation_lag{}", lag));
    }

    // Partial autocorrelation lags 1-5 (5)
    for lag in 1..=5 {
        features.push(format!("partial_autocorrelation_lag{}", lag));
    }

    // Ratio beyond r sigma (3)
    for r in 1..=3 {
        features.push(format!("ratio_beyond_r_sigma_{}", r));
    }

    // Time reversal asymmetry (3)
    for lag in 1..=3 {
        features.push(format!("time_reversal_asymmetry_stat_{}", lag));
    }

    // C3 lags (3)
    for lag in 1..=3 {
        features.push(format!("c3_lag{}", lag));
    }

    // FFT coefficients (30)
    for i in 0..10 {
        features.push(format!("fft_coefficient_{}_real", i));
        features.push(format!("fft_coefficient_{}_imag", i));
        features.push(format!("fft_coefficient_{}_abs", i));
    }

    features
}

// Helper functions

/// Validate feature parameters and return warnings for unknown parameter keys.
///
/// Returns a vector of warning messages for each unknown feature parameter key.
/// This allows queries to continue with a warning instead of failing.
pub fn validate_feature_params(feature_params: &[String]) -> Vec<String> {
    let available_features: HashSet<String> = list_features().into_iter().collect();
    let mut warnings = Vec::new();

    for param_key in feature_params {
        if !available_features.contains(param_key) {
            warnings.push(format!(
                "Unknown feature parameter key '{}' - this parameter will be ignored",
                param_key
            ));
        }
    }

    warnings
}

fn quantile(sorted: &[f64], q: f64) -> f64 {
    if sorted.is_empty() {
        return f64::NAN;
    }
    let idx = q * (sorted.len() - 1) as f64;
    let lower = idx.floor() as usize;
    let upper = idx.ceil() as usize;
    let frac = idx - lower as f64;

    if upper >= sorted.len() {
        sorted[sorted.len() - 1]
    } else {
        sorted[lower] * (1.0 - frac) + sorted[upper] * frac
    }
}

fn autocorr(values: &[f64], lag: usize) -> f64 {
    if values.len() <= lag {
        return f64::NAN;
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

fn partial_autocorr(values: &[f64], lag: usize) -> f64 {
    // Simplified PACF using Yule-Walker
    if lag == 1 {
        return autocorr(values, 1);
    }

    let acf1 = autocorr(values, 1);
    let acf2 = autocorr(values, 2);

    if (1.0 - acf1.powi(2)).abs() < f64::EPSILON {
        return 0.0;
    }

    (acf2 - acf1.powi(2)) / (1.0 - acf1.powi(2))
}

fn longest_strike(values: &[f64], threshold: f64, above: bool) -> f64 {
    let mut max_strike = 0;
    let mut current_strike = 0;

    for &v in values {
        let condition = if above { v > threshold } else { v < threshold };
        if condition {
            current_strike += 1;
            max_strike = max_strike.max(current_strike);
        } else {
            current_strike = 0;
        }
    }

    max_strike as f64
}

fn count_peaks(values: &[f64]) -> usize {
    if values.len() < 3 {
        return 0;
    }

    values
        .windows(3)
        .filter(|w| w[1] > w[0] && w[1] > w[2])
        .count()
}

fn benford_correlation(values: &[f64]) -> f64 {
    // Expected Benford distribution
    let expected: [f64; 9] = [
        0.301, 0.176, 0.125, 0.097, 0.079, 0.067, 0.058, 0.051, 0.046,
    ];

    let mut counts = [0usize; 9];
    let mut total = 0;

    for &v in values {
        let abs_v = v.abs();
        if abs_v >= 1.0 {
            let first_digit = abs_v
                .to_string()
                .chars()
                .find(|c| c.is_ascii_digit() && *c != '0');
            if let Some(d) = first_digit {
                if let Some(digit) = d.to_digit(10) {
                    if (1..=9).contains(&digit) {
                        counts[(digit - 1) as usize] += 1;
                        total += 1;
                    }
                }
            }
        }
    }

    if total == 0 {
        return 0.0;
    }

    let observed: Vec<f64> = counts.iter().map(|&c| c as f64 / total as f64).collect();

    // Pearson correlation
    let mean_exp: f64 = expected.iter().sum::<f64>() / 9.0;
    let mean_obs: f64 = observed.iter().sum::<f64>() / 9.0;

    let mut num = 0.0;
    let mut denom_exp = 0.0;
    let mut denom_obs = 0.0;

    for i in 0..9 {
        num += (expected[i] - mean_exp) * (observed[i] - mean_obs);
        denom_exp += (expected[i] - mean_exp).powi(2);
        denom_obs += (observed[i] - mean_obs).powi(2);
    }

    let denom = (denom_exp * denom_obs).sqrt();
    if denom.abs() < f64::EPSILON {
        0.0
    } else {
        num / denom
    }
}

fn linear_trend(values: &[f64]) -> (f64, f64, f64) {
    if values.len() < 2 {
        return (0.0, values.first().cloned().unwrap_or(0.0), 0.0);
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

    let slope = if ss_xx.abs() > f64::EPSILON {
        ss_xy / ss_xx
    } else {
        0.0
    };

    let intercept = y_mean - slope * x_mean;

    let r_squared = if ss_xx.abs() > f64::EPSILON && ss_yy.abs() > f64::EPSILON {
        (ss_xy.powi(2) / (ss_xx * ss_yy)).clamp(0.0, 1.0)
    } else {
        0.0
    };

    (slope, intercept, r_squared)
}

fn binned_entropy(values: &[f64], n_bins: usize) -> f64 {
    if values.is_empty() || n_bins == 0 {
        return 0.0;
    }

    let min = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let max = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let range = max - min;

    if range.abs() < f64::EPSILON {
        return 0.0;
    }

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

// New helper functions for extended features

fn first_last_location_of_value(values: &[f64], target: f64) -> (f64, f64) {
    let mut first = 0.0;
    let mut last = 0.0;
    let mut found_first = false;

    for (i, &v) in values.iter().enumerate() {
        if (v - target).abs() < f64::EPSILON {
            if !found_first {
                first = i as f64;
                found_first = true;
            }
            last = i as f64;
        }
    }

    (first, last)
}

fn count_peaks_threshold(values: &[f64], threshold: f64) -> usize {
    if values.len() < 3 {
        return 0;
    }

    let mean: f64 = values.iter().sum::<f64>() / values.len() as f64;

    values
        .windows(3)
        .filter(|w| w[1] > w[0] && w[1] > w[2] && (w[1] - mean).abs() > threshold)
        .count()
}

fn sample_entropy(values: &[f64], m: usize, r: f64) -> f64 {
    if values.len() < m + 1 || r <= 0.0 {
        return f64::NAN;
    }

    let n = values.len();

    // Count matches for template length m
    let count_m = count_template_matches(values, m, r);
    let count_m1 = count_template_matches(values, m + 1, r);

    if count_m == 0 || count_m1 == 0 {
        return f64::NAN;
    }

    // Normalize by number of comparisons
    let norm_m = (n - m) * (n - m - 1) / 2;
    let norm_m1 = (n - m - 1) * (n - m - 2) / 2;

    if norm_m == 0 || norm_m1 == 0 {
        return f64::NAN;
    }

    let phi_m = count_m as f64 / norm_m as f64;
    let phi_m1 = count_m1 as f64 / norm_m1 as f64;

    if phi_m1 <= 0.0 || phi_m <= 0.0 {
        return f64::NAN;
    }

    -(phi_m1 / phi_m).ln()
}

fn count_template_matches(values: &[f64], m: usize, r: f64) -> usize {
    let n = values.len();
    if n < m {
        return 0;
    }

    let mut count = 0;
    for i in 0..n - m {
        for j in i + 1..n - m {
            let mut is_match = true;
            for k in 0..m {
                if (values[i + k] - values[j + k]).abs() > r {
                    is_match = false;
                    break;
                }
            }
            if is_match {
                count += 1;
            }
        }
    }
    count
}

fn approximate_entropy(values: &[f64], m: usize, r: f64) -> f64 {
    if values.len() < m + 1 || r <= 0.0 {
        return f64::NAN;
    }

    let phi_m = phi_function(values, m, r);
    let phi_m1 = phi_function(values, m + 1, r);

    phi_m - phi_m1
}

fn phi_function(values: &[f64], m: usize, r: f64) -> f64 {
    let n = values.len();
    if n < m {
        return 0.0;
    }

    let mut c_sum = 0.0;
    for i in 0..=n - m {
        let mut count = 0;
        for j in 0..=n - m {
            let mut is_match = true;
            for k in 0..m {
                if (values[i + k] - values[j + k]).abs() > r {
                    is_match = false;
                    break;
                }
            }
            if is_match {
                count += 1;
            }
        }
        if count > 0 {
            c_sum += (count as f64 / (n - m + 1) as f64).ln();
        }
    }

    c_sum / (n - m + 1) as f64
}

fn permutation_entropy(values: &[f64], order: usize) -> f64 {
    if values.len() < order || order < 2 {
        return f64::NAN;
    }

    let n = values.len();
    let mut pattern_counts: HashMap<Vec<usize>, usize> = HashMap::new();

    for i in 0..=n - order {
        let window = &values[i..i + order];
        let mut indices: Vec<usize> = (0..order).collect();
        indices.sort_by(|&a, &b| {
            window[a]
                .partial_cmp(&window[b])
                .unwrap_or(std::cmp::Ordering::Equal)
        });
        *pattern_counts.entry(indices).or_insert(0) += 1;
    }

    let total = (n - order + 1) as f64;
    let mut entropy = 0.0;

    for &count in pattern_counts.values() {
        let p = count as f64 / total;
        if p > 0.0 {
            entropy -= p * p.ln();
        }
    }

    // Normalize by log of factorial of order
    let max_entropy = (1..=order)
        .map(|i| i as f64)
        .fold(0.0, |acc, x| acc + x.ln());
    if max_entropy > 0.0 {
        entropy / max_entropy
    } else {
        entropy
    }
}

fn count_unique(values: &[f64]) -> usize {
    let unique: HashSet<u64> = values.iter().map(|&v| v.to_bits()).collect();
    unique.len()
}

fn has_duplicate(values: &[f64]) -> bool {
    let unique: HashSet<u64> = values.iter().map(|&v| v.to_bits()).collect();
    unique.len() < values.len()
}

fn has_duplicate_value(values: &[f64], target: f64) -> bool {
    let count = values
        .iter()
        .filter(|&&v| (v - target).abs() < f64::EPSILON)
        .count();
    count > 1
}

fn reoccurring_stats(values: &[f64]) -> (f64, f64, f64, f64) {
    let mut value_counts: HashMap<u64, usize> = HashMap::new();
    for &v in values {
        *value_counts.entry(v.to_bits()).or_insert(0) += 1;
    }

    let n = values.len() as f64;
    let unique_values = value_counts.len() as f64;

    // Reoccurring datapoints: datapoints that appear more than once
    let reoccurring_dp_count: usize = value_counts.values().filter(|&&c| c > 1).copied().sum();
    let reoccurring_val_count: usize = value_counts.values().filter(|&&c| c > 1).count();

    // Sum of values that reoccur
    let sum_reoccur_val: f64 = value_counts
        .iter()
        .filter(|(_, &c)| c > 1)
        .map(|(&bits, _)| f64::from_bits(bits))
        .sum();

    let sum_reoccur_dp: f64 = value_counts
        .iter()
        .filter(|(_, &c)| c > 1)
        .map(|(&bits, &c)| f64::from_bits(bits) * c as f64)
        .sum();

    (
        reoccurring_dp_count as f64 / n,
        reoccurring_val_count as f64 / unique_values.max(1.0),
        sum_reoccur_val,
        sum_reoccur_dp,
    )
}

fn time_reversal_asymmetry(values: &[f64], lag: usize) -> f64 {
    if values.len() <= 2 * lag {
        return f64::NAN;
    }

    let n = values.len();
    let mut sum = 0.0;

    for i in lag..n - lag {
        let x_lag = values[i - lag];
        let x = values[i];
        let x_lead = values[i + lag];
        sum += x_lead.powi(2) * x - x_lag * x.powi(2);
    }

    sum / (n - 2 * lag) as f64
}

fn c3(values: &[f64], lag: usize) -> f64 {
    if values.len() <= 2 * lag {
        return f64::NAN;
    }

    let n = values.len();
    let mut sum = 0.0;

    for i in 0..n - 2 * lag {
        sum += values[i] * values[i + lag] * values[i + 2 * lag];
    }

    sum / (n - 2 * lag) as f64
}

fn lempel_ziv_complexity(values: &[f64], threshold: f64) -> f64 {
    // Convert to binary string based on threshold
    let binary: Vec<bool> = values.iter().map(|&v| v >= threshold).collect();
    if binary.is_empty() {
        return 0.0;
    }

    let n = binary.len();
    let mut complexity = 1;
    let mut l = 1;
    let mut k = 1;
    let mut k_max = 1;

    while l + k <= n {
        // Check if substring [l..l+k] appears in [0..l+k-1]
        let mut found = false;
        let substr = &binary[l..l + k];

        for start in 0..l + k - k {
            if &binary[start..start + k] == substr {
                found = true;
                break;
            }
        }

        if found {
            k += 1;
            if k > k_max {
                k_max = k;
            }
        } else {
            complexity += 1;
            l += k_max;
            k = 1;
            k_max = 1;
        }
    }

    // Normalize
    let b = (n as f64).log2();
    if b > 0.0 {
        complexity as f64 * b / n as f64
    } else {
        0.0
    }
}

fn simple_dft(values: &[f64]) -> Vec<(f64, f64)> {
    let n = values.len();
    let mut coeffs = Vec::with_capacity(n);

    for k in 0..n {
        let mut real = 0.0;
        let mut imag = 0.0;

        for (t, &x) in values.iter().enumerate() {
            let angle = -2.0 * std::f64::consts::PI * k as f64 * t as f64 / n as f64;
            real += x * angle.cos();
            imag += x * angle.sin();
        }

        coeffs.push((real / n as f64, imag / n as f64));
    }

    coeffs
}

fn spectral_features(fft_coeffs: &[(f64, f64)]) -> (f64, f64) {
    if fft_coeffs.is_empty() {
        return (0.0, 0.0);
    }

    // Compute power spectrum
    let power: Vec<f64> = fft_coeffs
        .iter()
        .map(|(r, i)| r.powi(2) + i.powi(2))
        .collect();

    let total_power: f64 = power.iter().sum();

    if total_power <= f64::EPSILON {
        return (0.0, 0.0);
    }

    // Spectral centroid: weighted mean of frequencies
    let centroid: f64 = power
        .iter()
        .enumerate()
        .map(|(i, &p)| i as f64 * p)
        .sum::<f64>()
        / total_power;

    // Spectral variance
    let variance: f64 = power
        .iter()
        .enumerate()
        .map(|(i, &p)| (i as f64 - centroid).powi(2) * p)
        .sum::<f64>()
        / total_power;

    (centroid, variance)
}

fn aggregated_linear_trend(values: &[f64], chunk_len: usize) -> (f64, f64, f64, f64) {
    if values.len() < chunk_len || chunk_len == 0 {
        return (0.0, 0.0, 0.0, 0.0);
    }

    // Split into chunks and compute aggregate (mean) for each
    let chunk_means: Vec<f64> = values
        .chunks(chunk_len)
        .map(|chunk| chunk.iter().sum::<f64>() / chunk.len() as f64)
        .collect();

    if chunk_means.len() < 2 {
        return (0.0, chunk_means.first().cloned().unwrap_or(0.0), 0.0, 0.0);
    }

    let (slope, intercept, r_squared) = linear_trend(&chunk_means);

    // Compute standard error
    let n = chunk_means.len() as f64;
    let x_mean = (n - 1.0) / 2.0;

    let ss_xx: f64 = (0..chunk_means.len())
        .map(|i| (i as f64 - x_mean).powi(2))
        .sum();

    let residual_ss: f64 = chunk_means
        .iter()
        .enumerate()
        .map(|(i, &y)| {
            let predicted = intercept + slope * i as f64;
            (y - predicted).powi(2)
        })
        .sum();

    let stderr = if n > 2.0 && ss_xx > f64::EPSILON {
        (residual_ss / (n - 2.0) / ss_xx).sqrt()
    } else {
        0.0
    };

    (slope, intercept, r_squared.sqrt(), stderr)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_features() {
        let values = vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0];
        let features = extract_features(&values).unwrap();

        assert_eq!(features.get("length"), Some(&10.0));
        assert_eq!(features.get("mean"), Some(&5.5));
        assert_eq!(features.get("minimum"), Some(&1.0));
        assert_eq!(features.get("maximum"), Some(&10.0));

        // Test new features exist
        assert!(features.contains_key("quantile_0.1"));
        assert!(features.contains_key("quantile_0.9"));
        assert!(features.contains_key("first_location_of_maximum"));
        assert!(features.contains_key("lempel_ziv_complexity"));
        assert!(features.contains_key("spectral_centroid"));
    }

    #[test]
    fn test_list_features() {
        let names = list_features();
        assert!(!names.is_empty());
        assert!(names.contains(&"mean".to_string()));
        // Verify we have 76+ features
        assert!(
            names.len() >= 76,
            "Expected at least 76 features, got {}",
            names.len()
        );
    }

    #[test]
    fn test_feature_count_matches() {
        // Ensure extract_features returns same count as list_features for long enough series
        let values: Vec<f64> = (0..100).map(|i| (i as f64).sin()).collect();
        let features = extract_features(&values).unwrap();
        let feature_names = list_features();

        // Some features may not be computed for all series (e.g., skewness with zero std)
        // but the count should be close
        assert!(
            features.len() >= feature_names.len() - 10,
            "Extracted {} features but list has {}",
            features.len(),
            feature_names.len()
        );
    }

    #[test]
    fn test_new_entropy_features() {
        let values = vec![1.0, 2.0, 3.0, 2.0, 1.0, 3.0, 2.0, 1.0, 3.0, 2.0];
        let features = extract_features(&values).unwrap();

        assert!(features.contains_key("sample_entropy"));
        assert!(features.contains_key("approximate_entropy"));
        assert!(features.contains_key("permutation_entropy"));
    }

    #[test]
    fn test_fft_features() {
        let values = vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0];
        let features = extract_features(&values).unwrap();

        // FFT coefficients should exist
        for i in 0..10 {
            assert!(features.contains_key(&format!("fft_coefficient_{}_real", i)));
            assert!(features.contains_key(&format!("fft_coefficient_{}_imag", i)));
            assert!(features.contains_key(&format!("fft_coefficient_{}_abs", i)));
        }
    }
}
