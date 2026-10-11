//! Time series feature extraction (tsfresh-compatible).

use crate::error::{ForecastError, Result};
use crate::stats::{sample_excess_kurtosis_g2, sample_skewness_g1, template_match_counts};
use rustfft::{num_complex::Complex, FftPlanner};
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

    // Coefficient of variation (FEAT-02, D-03): tsfresh's variation_coefficient
    // is std(x)/mean(x), signed -- NaN exactly when mean == 0.0, not |mean()|.
    if mean == 0.0 {
        features.insert("variation_coefficient".to_string(), f64::NAN);
    } else {
        features.insert("variation_coefficient".to_string(), std_dev / mean);
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

    // Skewness and Kurtosis (D-05, FEAT-02): shared with ts_stats via
    // sample_skewness_g1 / sample_excess_kurtosis_g2 (bias-corrected G1/G2,
    // scipy skew(bias=False)/kurtosis(bias=False)). NaN for n <= 2 / n <= 3
    // or zero variance -- a documented intentional deviation from
    // tsfresh/pandas, which return 0 for a zero-variance series (D-03).
    features.insert("skewness".to_string(), sample_skewness_g1(values));
    features.insert("kurtosis".to_string(), sample_excess_kurtosis_g2(values));

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

    // Mean change (117-key invariant, D-13: NaN for n=1, not omitted)
    if values.len() > 1 {
        let changes: Vec<f64> = values.windows(2).map(|w| w[1] - w[0]).collect();
        let mean_change = changes.iter().sum::<f64>() / changes.len() as f64;
        let mean_abs_change = changes.iter().map(|c| c.abs()).sum::<f64>() / changes.len() as f64;
        features.insert("mean_change".to_string(), mean_change);
        features.insert("mean_abs_change".to_string(), mean_abs_change);
    } else {
        features.insert("mean_change".to_string(), f64::NAN);
        features.insert("mean_abs_change".to_string(), f64::NAN);
    }

    // Extended Autocorrelation - lags 1-10 (Task 3, FEAT-05). Always inserted
    // (no length gate): autocorr itself returns NaN for lag >= n or
    // near-zero variance, matching tsfresh exactly.
    for lag in 1..=10 {
        features.insert(format!("autocorrelation_lag{}", lag), autocorr(values, lag));
    }

    // Extended Partial autocorrelation - lags 1-5 (Durbin-Levinson, D-06/FEAT-01).
    // Always inserted (no length gate): pacf_durbin_levinson itself returns
    // NaN for n <= 1 or lag >= n/2 (integer division), matching
    // statsmodels pacf(method="ld") / tsfresh partial_autocorrelation.
    let pacf = pacf_durbin_levinson(values, 5);
    for (lag, &value) in pacf.iter().enumerate().take(6).skip(1) {
        features.insert(format!("partial_autocorrelation_lag{}", lag), value);
    }

    // First and last values
    features.insert("first_value".to_string(), values[0]);
    features.insert("last_value".to_string(), values[values.len() - 1]);

    // Location of min/max (FEAT-02, D-03): tsfresh's last_location_of_* is
    // `1.0 - argmax(x[::-1])/n == (last_idx+1)/n`, not the raw 0-based
    // `last_idx/n` -- first_location_* stays `first_idx/n`.
    let (first_max_idx, last_max_idx) = first_last_location_of_value(values, max);
    let (first_min_idx, last_min_idx) = first_last_location_of_value(values, min);
    features.insert("first_location_of_maximum".to_string(), first_max_idx / n);
    features.insert(
        "last_location_of_maximum".to_string(),
        (last_max_idx + 1.0) / n,
    );
    features.insert("first_location_of_minimum".to_string(), first_min_idx / n);
    features.insert(
        "last_location_of_minimum".to_string(),
        (last_min_idx + 1.0) / n,
    );

    // Absolute energy
    let abs_energy: f64 = values.iter().map(|v| v.powi(2)).sum();
    features.insert("abs_energy".to_string(), abs_energy);

    // Root mean square
    features.insert("root_mean_square".to_string(), (abs_energy / n).sqrt());

    // Mean second derivative central (FEAT-02, D-13): tsfresh's closed form is
    // (x[-1]-x[-2]-x[1]+x[0]) / (2*(n-2)) -- the windowed sum below telescopes
    // to that same numerator, but the denominator needs the factor of 2
    // (previously missing). NaN for n <= 2, not omitted (117-key invariant).
    if values.len() > 2 {
        let second_deriv: f64 = values
            .windows(3)
            .map(|w| w[2] - 2.0 * w[1] + w[0])
            .sum::<f64>()
            / (2.0 * (values.len() - 2) as f64);
        features.insert("mean_second_derivative_central".to_string(), second_deriv);
    } else {
        features.insert("mean_second_derivative_central".to_string(), f64::NAN);
    }

    // Complexity estimate (CID) -- 0.0 for n=1 (tsfresh sums over an empty
    // diff), not omitted (117-key invariant, D-13)
    let cid: f64 = if values.len() > 1 {
        values
            .windows(2)
            .map(|w| (w[1] - w[0]).powi(2))
            .sum::<f64>()
            .sqrt()
    } else {
        0.0
    };
    features.insert("cid_ce".to_string(), cid);

    // Absolute sum of changes -- 0.0 for n=1, not omitted (117-key
    // invariant, D-13)
    let abs_sum_changes: f64 = if values.len() > 1 {
        values.windows(2).map(|w| (w[1] - w[0]).abs()).sum()
    } else {
        0.0
    };
    features.insert("absolute_sum_of_changes".to_string(), abs_sum_changes);

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
    features.insert("sample_entropy".to_string(), sample_entropy(values));
    features.insert(
        "approximate_entropy".to_string(),
        approximate_entropy(values, 2, 0.2),
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

    // Time reversal asymmetry -- 0.0 when 2*lag >= n (tsfresh), not omitted
    // (117-key invariant, D-13; the term-order bug itself is fixed in 14-02)
    for lag in 1..=3 {
        let value = if values.len() > 2 * lag {
            time_reversal_asymmetry(values, lag)
        } else {
            0.0
        };
        features.insert(format!("time_reversal_asymmetry_stat_{}", lag), value);
    }

    // C3 statistic (nonlinearity measure) -- 0.0 when 2*lag >= n (tsfresh),
    // not omitted (117-key invariant, D-13)
    for lag in 1..=3 {
        let value = if values.len() > 2 * lag {
            c3(values, lag)
        } else {
            0.0
        };
        features.insert(format!("c3_lag{}", lag), value);
    }

    // Lempel-Ziv complexity (D-08, FEAT-04): tsfresh bins=2 range-midpoint
    // quantisation (not the previous mean threshold).
    features.insert(
        "lempel_ziv_complexity".to_string(),
        lempel_ziv_complexity(values, 2),
    );

    // One-sided rustfft spectrum (D-07, FEAT-03): numpy-rfft-exact,
    // unnormalised, O(n log n) for any n including large primes.
    // fft_coefficient_{i}_{real,imag,abs} for i in 0..10 read bin i when it
    // exists (i < n/2 + 1), else NaN (117-key invariant, D-13).
    let spectrum = one_sided_spectrum(values);
    for i in 0..10 {
        let (real, imag, abs) = match spectrum.get(i) {
            Some(c) => (c.re, c.im, c.norm()),
            None => (f64::NAN, f64::NAN, f64::NAN),
        };
        features.insert(format!("fft_coefficient_{}_real", i), real);
        features.insert(format!("fft_coefficient_{}_imag", i), imag);
        features.insert(format!("fft_coefficient_{}_abs", i), abs);
    }

    // Spectral features: tsfresh fft_aggregated on the SAME one-sided
    // magnitude spectrum as fft_coefficient_* above (D-07, FEAT-03).
    let (spectral_centroid, spectral_variance) = fft_aggregated_moments(&spectrum);
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

    // 117-key output invariant (D-13, FEAT-05): every list_features() name
    // must be present for every input length >= 1 -- debug-only since
    // list_features() allocates a Vec on every call.
    debug_assert_eq!(
        features.len(),
        list_features().len(),
        "extract_features: key count {} != list_features() count {} (117-key invariant violated)",
        features.len(),
        list_features().len()
    );

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

/// Autocorrelation at `lag`, matching tsfresh's `autocorrelation` calculator
/// exactly (Task 3, FEAT-05 -- a finding beyond the original audit's F1-F11
/// list): numerator over the first `n-lag` pairs around the whole-series
/// mean, denominator `(n-lag) * population_variance(x)`. NaN when `lag >= n`
/// or the population variance is within `1e-8` of zero (tsfresh
/// `np.isclose(v, 0)` semantics) -- no epsilon guard returning 0.
fn autocorr(values: &[f64], lag: usize) -> f64 {
    let n = values.len();
    if lag >= n {
        return f64::NAN;
    }

    let mean: f64 = values.iter().sum::<f64>() / n as f64;
    let variance: f64 = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n as f64;
    if variance.abs() <= 1e-8 {
        return f64::NAN;
    }

    let num: f64 = (0..(n - lag))
        .map(|i| (values[i] - mean) * (values[i + lag] - mean))
        .sum();

    num / ((n - lag) as f64 * variance)
}

/// Partial autocorrelation via the Durbin-Levinson recursion on the
/// "adjusted" (divide-by-`(n-k)`) autocovariance, matching statsmodels
/// `pacf(x, method="ld")` -- what tsfresh's `partial_autocorrelation`
/// calculator calls internally (D-06, FEAT-01).
///
/// Returns a vector indexed `0..=max_lag`: `pacf[0]` is always `1.0` unless
/// `values.len() <= 1` (every entry NaN). A demanded lag `k >= n / 2`
/// (integer division -- statsmodels' own boundary) is NaN. A zero lag-0
/// autocovariance (constant or all-zero input) propagates as NaN through the
/// recursion via IEEE-754 `0.0 / 0.0` -- no epsilon guard returning 0.
fn pacf_durbin_levinson(values: &[f64], max_lag: usize) -> Vec<f64> {
    let n = values.len();
    let mut pacf = vec![f64::NAN; max_lag + 1];

    if n <= 1 {
        return pacf;
    }

    // statsmodels/tsfresh: a demanded lag >= n/2 is NaN; only lags
    // 1..=(n/2 - 1) are computable (n >= 2 here, so n/2 >= 1, no underflow).
    let max_computable_lag = n / 2 - 1;
    let effective_max_lag = max_lag.min(max_computable_lag);

    pacf[0] = 1.0;
    if effective_max_lag == 0 {
        return pacf;
    }

    let mean = values.iter().sum::<f64>() / n as f64;
    let xo: Vec<f64> = values.iter().map(|v| v - mean).collect();

    // "Adjusted" autocovariance: divide lag k by (n-k), not n.
    let mut acov = vec![0.0; effective_max_lag + 1];
    acov[0] = xo.iter().map(|v| v * v).sum::<f64>() / n as f64;
    for k in 1..=effective_max_lag {
        let s: f64 = (k..n).map(|i| xo[i] * xo[i - k]).sum();
        acov[k] = s / (n - k) as f64;
    }

    // Durbin-Levinson recursion.
    let mut phi = vec![vec![0.0; effective_max_lag + 1]; effective_max_lag + 1];
    let mut sig = vec![0.0; effective_max_lag + 1];
    phi[1][1] = acov[1] / acov[0];
    sig[1] = acov[0] - phi[1][1] * acov[1];
    pacf[1] = phi[1][1];

    for k in 2..=effective_max_lag {
        let dot: f64 = (1..k).map(|j| phi[j][k - 1] * acov[k - j]).sum();
        phi[k][k] = (acov[k] - dot) / sig[k - 1];
        for j in 1..k {
            phi[j][k] = phi[j][k - 1] - phi[k][k] * phi[k - j][k - 1];
        }
        sig[k] = sig[k - 1] * (1.0 - phi[k][k].powi(2));
        pacf[k] = phi[k][k];
    }

    pacf
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

/// tsfresh `benford_correlation` (D-08, FEAT-04, audit F11 part 1): the
/// leading significant digit of EVERY nonzero finite `|v|` -- including
/// `|v| < 1`, via Rust's shortest round-trip scientific formatting
/// (`format!("{:e}", av)`'s first character has the same leading digit as
/// `np.format_float_scientific`, e.g. `0.0034 -> "3.4e-3"` -> digit `3`).
/// `NaN -> digit 0` (`np.nan_to_num`'s zero), `+-inf -> +-f64::MAX` then
/// `abs()` (matching `nan_to_num`'s finite-infinity replacement), an exact
/// `0.0 -> digit 0`. `digit 0` never matches bins `1..=9`, so it only
/// lowers every `data[d]` via the unchanged denominator `n` (zeros/NaNs stay
/// in the denominator, D-03). Pearson correlation against
/// `log10(1 + 1/d)` for `d in 1..=9`; `NaN` when either vector has zero
/// variance (no epsilon guard -- `0.0/0.0 -> NaN` falls out of the formula).
fn benford_correlation(values: &[f64]) -> f64 {
    let n = values.len() as f64;
    let mut counts = [0usize; 10]; // index 0 = digit 0 (unused in the distribution, still counted toward n)

    for &v in values {
        let x = if v.is_nan() {
            0.0
        } else if v.is_infinite() {
            if v > 0.0 {
                f64::MAX
            } else {
                -f64::MAX
            }
        } else {
            v
        };
        let av = x.abs();
        let digit = if av == 0.0 {
            0
        } else {
            format!("{:e}", av)
                .chars()
                .next()
                .and_then(|c| c.to_digit(10))
                .unwrap_or(0)
        };
        counts[digit as usize] += 1;
    }

    let benford: Vec<f64> = (1..=9u32).map(|d| (1.0 + 1.0 / d as f64).log10()).collect();
    let data: Vec<f64> = (1..=9usize).map(|d| counts[d] as f64 / n).collect();

    pearson_correlation(&benford, &data)
}

/// Pearson correlation coefficient of two equal-length slices. `NaN` when
/// either slice has zero variance (`0.0/0.0 -> NaN`, no epsilon guard) --
/// matches `np.corrcoef`'s degenerate-input behaviour exactly (benford_correlation,
/// Task 2, D-08).
fn pearson_correlation(a: &[f64], b: &[f64]) -> f64 {
    let n = a.len() as f64;
    let mean_a: f64 = a.iter().sum::<f64>() / n;
    let mean_b: f64 = b.iter().sum::<f64>() / n;

    let mut num = 0.0;
    let mut denom_a = 0.0;
    let mut denom_b = 0.0;
    for (&va, &vb) in a.iter().zip(b.iter()) {
        num += (va - mean_a) * (vb - mean_b);
        denom_a += (va - mean_a).powi(2);
        denom_b += (vb - mean_b).powi(2);
    }

    num / (denom_a.sqrt() * denom_b.sqrt())
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

    // linear_trend_r_squared (numpy/scipy reference row, Task 3 FEAT-02):
    // scipy's rvalue is NaN when the x- or y-variance is degenerate (ss_xx ==
    // 0 or ss_yy == 0) -- here only ss_yy == 0 (a constant series) is
    // reachable, since x is the time index and always varies for n >= 2.
    let r_squared = if ss_xx.abs() > f64::EPSILON && ss_yy.abs() > f64::EPSILON {
        (ss_xy.powi(2) / (ss_xx * ss_yy)).clamp(0.0, 1.0)
    } else {
        f64::NAN
    };

    (slope, intercept, r_squared)
}

/// tsfresh `binned_entropy` (D-08, FEAT-04, audit F10): ports `np.histogram`'s
/// exact floor-based binning (not the previous rounding-based scheme) --
/// `first`/`last` edge = min/max, widened to `min-0.5`/`max+0.5` when they're
/// equal; interior edges via `linspace` arithmetic (`k*step + first`), with
/// the final edge forced to `last` exactly; the computed bin index is
/// corrected against neighbouring edges exactly as `np.histogram` does, so a
/// value never lands one bin off due to floating-point rounding of the
/// `(v-first)/(last-first)*bins` ratio. Shannon entropy over the resulting
/// bin probabilities, empty bins contributing `0`. `NaN` if any input value
/// is `NaN` (T-14-10 -- a NaN would otherwise poison every bin boundary
/// comparison into silent, wrong bin assignment rather than a clean NaN
/// propagation).
fn binned_entropy(values: &[f64], n_bins: usize) -> f64 {
    if values.iter().any(|v| v.is_nan()) {
        return f64::NAN;
    }
    if values.is_empty() || n_bins == 0 {
        return 0.0;
    }

    let mut first = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let mut last = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    if (last - first).abs() < f64::EPSILON {
        first -= 0.5;
        last += 0.5;
    }
    let range = last - first;
    let step = range / n_bins as f64;
    let edge = |k: usize| -> f64 {
        if k == n_bins {
            last
        } else {
            k as f64 * step + first
        }
    };

    let mut bins = vec![0usize; n_bins];
    for &v in values {
        let scaled = ((v - first) / range) * n_bins as f64;
        let mut idx = scaled as i64;
        if idx == n_bins as i64 {
            idx = n_bins as i64 - 1;
        }
        if v < edge(idx as usize) {
            idx -= 1;
        }
        if idx != n_bins as i64 - 1 && v >= edge((idx + 1) as usize) {
            idx += 1;
        }
        bins[idx as usize] += 1;
    }

    let n = values.len() as f64;
    let sum_plogp: f64 = bins
        .iter()
        .filter(|&&count| count > 0)
        .map(|&count| {
            let p = count as f64 / n;
            p * p.ln()
        })
        .sum();

    -sum_plogp
}

// New helper functions for extended features

/// First/last 0-based index where `values[i] == target` exactly (D-03: tsfresh
/// compares `x == max`/`x == min`, not an epsilon window -- `target` is
/// always the literal max/min of `values`, so exact equality is safe here).
fn first_last_location_of_value(values: &[f64], target: f64) -> (f64, f64) {
    let mut first = 0.0;
    let mut last = 0.0;
    let mut found_first = false;

    for (i, &v) in values.iter().enumerate() {
        if v == target {
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

/// tsfresh `sample_entropy` (D-08, FEAT-04, audit F9, RESEARCH Pitfall 3):
/// `m=2`, `r = 0.2 * population std(x)`. `B` sums, over the `n-m+1`
/// length-`m` templates, `(match count incl. self) - 1`; `A` is the same
/// sum over the `n-m` length-`(m+1)` templates -- the shared
/// [`template_match_counts`] band search gives per-template counts directly,
/// no triangular restriction and no combinatorial divisor (tsfresh's own
/// `_into_subchunks`/full-symmetric-matrix semantics). Returns `-ln(A/B)` on
/// the raw sums: `A=0 < B` gives `+inf`, `B=0` (so `A=0` too) gives `NaN`.
/// NaN if any input value is NaN (T-14-10) -- `template_match_counts`'s sort
/// never panics on NaN, but a NaN template's Chebyshev distance is not
/// physically meaningful, so this guards explicitly rather than relying on
/// whatever count a NaN-laden band search happens to produce.
fn sample_entropy(values: &[f64]) -> f64 {
    if values.iter().any(|v| v.is_nan()) {
        return f64::NAN;
    }

    let n = values.len();
    let m = 2;
    if n < m + 1 {
        return f64::NAN;
    }

    let mean = values.iter().sum::<f64>() / n as f64;
    let variance = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n as f64;
    let r = 0.2 * variance.sqrt();

    let b: f64 = template_match_counts(values, m, r)
        .iter()
        .map(|&c| (c - 1) as f64)
        .sum();
    let a: f64 = template_match_counts(values, m + 1, r)
        .iter()
        .map(|&c| (c - 1) as f64)
        .sum();

    -(a / b).ln()
}

/// tsfresh `approximate_entropy` (D-08, FEAT-04, audit F8-adjacent): `m=2`,
/// `r_ratio=0.2` -- `r = r_ratio * population std(x)`. `0.0` when `n <= m+1`
/// (tsfresh's own degenerate-input branch, not `NaN`); otherwise
/// `|Phi_m - Phi_{m+1}|` with `Phi_m = mean_i(ln(C_i / (n-m+1)))` over the
/// shared [`template_match_counts`] band search. NaN if any input value is
/// NaN.
fn approximate_entropy(values: &[f64], m: usize, r_ratio: f64) -> f64 {
    if values.iter().any(|v| v.is_nan()) {
        return f64::NAN;
    }

    let n = values.len();
    if n <= m + 1 {
        return 0.0;
    }

    let mean = values.iter().sum::<f64>() / n as f64;
    let variance = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n as f64;
    let r = r_ratio * variance.sqrt();

    (phi_m_tsfresh(values, m, r) - phi_m_tsfresh(values, m + 1, r)).abs()
}

/// `Phi_m = mean_i(ln(C_i / (n-m+1)))` over the `n-m+1` length-`m` templates,
/// via the shared [`template_match_counts`] band search (self-match always
/// included, so `C_i >= 1` and the `ln` argument is always `> 0`).
fn phi_m_tsfresh(values: &[f64], m: usize, r: f64) -> f64 {
    let n_m = values.len() - m + 1;
    let counts = template_match_counts(values, m, r);
    let sum_log: f64 = counts.iter().map(|&c| (c as f64 / n_m as f64).ln()).sum();
    sum_log / n_m as f64
}

/// tsfresh `permutation_entropy` (D-08, FEAT-04, audit F11 part 2):
/// `dimension=3`, `tau=1` (contiguous windows, unchanged from before this
/// plan). The pattern key here is a single stable argsort of each window
/// (ties broken by original position, matching numpy's stable argsort) --
/// this is NOT tsfresh's literal `np.argsort(np.argsort(X))` double-argsort,
/// but double-argsort is exactly the inverse permutation of the single
/// argsort, a bijection on same-length permutations, so the two give
/// IDENTICAL equivalence classes (and therefore identical pattern counts
/// and entropy) -- verified against `[0,0,5] -> [0,1,2]` and
/// `[3,3,1] -> [2,0,1]` (both the single-argsort AND tsfresh's final
/// grouping agree). The only real bug was the trailing `ln(order!)`
/// normalisation (now removed) -- tsfresh's own calculator never
/// normalises. `NaN` when `n < dimension`.
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
    let sum_plogp: f64 = pattern_counts
        .values()
        .map(|&count| {
            let p = count as f64 / total;
            p * p.ln()
        })
        .sum();

    -sum_plogp
}

/// Canonicalises `-0.0` to `+0.0` before bit-hashing, matching `np.unique`'s
/// equality semantics (`-0.0 == 0.0`, D-03) for `count_unique`/
/// `has_duplicate`/`reoccurring_stats`.
fn canonical_bits(v: f64) -> u64 {
    if v == 0.0 {
        0.0f64.to_bits()
    } else {
        v.to_bits()
    }
}

fn count_unique(values: &[f64]) -> usize {
    let unique: HashSet<u64> = values.iter().map(|&v| canonical_bits(v)).collect();
    unique.len()
}

fn has_duplicate(values: &[f64]) -> bool {
    let unique: HashSet<u64> = values.iter().map(|&v| canonical_bits(v)).collect();
    unique.len() < values.len()
}

/// `np.sum(x == target) >= 2` (D-03: exact equality, matching tsfresh
/// `has_duplicate_max`/`has_duplicate_min`).
fn has_duplicate_value(values: &[f64], target: f64) -> bool {
    values.iter().filter(|&&v| v == target).count() > 1
}

fn reoccurring_stats(values: &[f64]) -> (f64, f64, f64, f64) {
    let mut value_counts: HashMap<u64, usize> = HashMap::new();
    for &v in values {
        *value_counts.entry(canonical_bits(v)).or_insert(0) += 1;
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

/// tsfresh `time_reversal_asymmetry_statistic`: mean over `i in 0..n-2*lag` of
/// `x[i+2*lag]^2 * x[i+lag] - x[i+lag] * x[i]^2` (FEAT-02, D-03). Re-indexed
/// with `j = i + lag` below (`x_lag = x[j-lag]`, `x = x[j]`, `x_lead =
/// x[j+lag]`): the previous implementation had the second term's operands
/// transposed (`x_lag * x^2` instead of `x * x_lag^2`).
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
        sum += x_lead.powi(2) * x - x * x_lag.powi(2);
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

/// tsfresh `lempel_ziv_complexity` (D-08, FEAT-04, audit F11 part 3): a full
/// algorithm rewrite, not a tweak. Quantises `values` into `bins` symbols via
/// numpy-`linspace` quantization edges ([`lempel_ziv_quantization_edges`]:
/// edge `k` for `k in 1..=bins` is `k*step + min` with the final edge forced
/// to `max`, so a constant series collapses every edge to `min` via
/// `step == 0`), with `symbol(v) = count of edges strictly < v` (equivalent
/// to `np.searchsorted(edges, v, side="left")`). Then tsfresh's own
/// sub-word dictionary-growth loop: no mean threshold, no `log2(n)/n`
/// normalisation -- the result is `dictionary_size / n`.
fn lempel_ziv_complexity(values: &[f64], bins: usize) -> f64 {
    let n = values.len();
    if n == 0 {
        return 0.0;
    }

    let edges = lempel_ziv_quantization_edges(values, bins);
    let symbols: Vec<usize> = values
        .iter()
        .map(|&v| edges.iter().filter(|&&e| e < v).count())
        .collect();

    let mut sub_strings: HashSet<Vec<usize>> = HashSet::new();
    let mut ind = 0usize;
    let mut inc = 1usize;

    while ind + inc <= n {
        let sub_str = symbols[ind..ind + inc].to_vec();
        if sub_strings.contains(&sub_str) {
            inc += 1;
        } else {
            sub_strings.insert(sub_str);
            ind += inc;
            inc = 1;
        }
    }

    sub_strings.len() as f64 / n as f64
}

/// `np.linspace(min(x), max(x), bins + 1)[1:]` (the quantisation edges tsfresh's
/// `lempel_ziv_complexity` uses): edge `k` for `k in 1..=bins` is `k*step +
/// min` where `step = (max-min)/bins`, except the final edge (`k == bins`) is
/// forced to `max` exactly (linspace's own endpoint correction). When
/// `min == max` (a constant series), `step == 0` and every edge collapses to
/// `min` (which also equals `max`), matching numpy exactly.
fn lempel_ziv_quantization_edges(values: &[f64], bins: usize) -> Vec<f64> {
    let min = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let max = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let step = (max - min) / bins as f64;
    (1..=bins)
        .map(|k| {
            if k == bins {
                max
            } else {
                k as f64 * step + min
            }
        })
        .collect()
}

/// One-sided complex spectrum of a real-valued series, numpy `rfft`-exact
/// (D-07, FEAT-03): unnormalised forward FFT of the zero-imaginary input,
/// truncated to the `n/2 + 1` bins a real-input FFT needs. `rustfft` picks
/// Rader/Bluestein for prime lengths, so this stays O(n log n) for any `n`
/// -- including large primes, where a naive O(n^2) DFT would be infeasible.
fn one_sided_spectrum(values: &[f64]) -> Vec<Complex<f64>> {
    let n = values.len();
    let mut buffer: Vec<Complex<f64>> = values.iter().map(|&v| Complex::new(v, 0.0)).collect();
    let mut planner = FftPlanner::<f64>::new();
    let fft = planner.plan_fft_forward(n);
    fft.process(&mut buffer);
    buffer.truncate(n / 2 + 1);
    buffer
}

/// tsfresh `fft_aggregated` (D-07, FEAT-03) on the one-sided magnitude
/// spectrum `y_k = |X_k|` of [`one_sided_spectrum`]'s output:
/// `centroid = get_moment(y, 1) = sum(k*y_k)/sum(y_k)`,
/// `variance = get_moment(y, 2) - centroid^2 = sum(k^2*y_k)/sum(y_k) -
/// centroid^2`. No epsilon guard -- an all-zero series yields `0.0/0.0 ->
/// NaN` exactly as tsfresh's `get_moment` does.
fn fft_aggregated_moments(spectrum: &[Complex<f64>]) -> (f64, f64) {
    let magnitudes: Vec<f64> = spectrum.iter().map(|c| c.norm()).collect();
    let sum_y: f64 = magnitudes.iter().sum();
    let moment = |power: i32| -> f64 {
        magnitudes
            .iter()
            .enumerate()
            .map(|(k, &y_k)| (k as f64).powi(power) * y_k)
            .sum::<f64>()
            / sum_y
    };
    let centroid = moment(1);
    let variance = moment(2) - centroid * centroid;
    (centroid, variance)
}

/// scipy 1.18 `linregress(x, y)` semantics with `x = 0..m-1` (Task 3,
/// FEAT-02): signed Pearson `r` clipped to `[-1, 1]`; degenerate
/// (`ss_xx == 0` or `ss_yy == 0`) gives `r = NaN` when `ss_xy == 0` too,
/// else `r = 0.0`; `slope = ss_xy/ss_xx`, `intercept = ybar - slope*xbar`
/// (plain IEEE-754 division: `0.0/0.0 -> NaN`, nonzero/`0.0 -> inf`);
/// `stderr = sqrt((1 - r^2) * ss_yy / ss_xx / df)` with `df = m - 2`
/// evaluated in `f64` (so `m == 2` gives `df == 0.0`, and a numerator of
/// exactly `0.0` -- e.g. a perfect 2-point fit -- yields `0.0/0.0 -> NaN`).
fn scipy_linregress(y: &[f64]) -> (f64, f64, f64, f64) {
    let m = y.len() as f64;
    let x_mean = (y.len() as f64 - 1.0) / 2.0;
    let y_mean: f64 = y.iter().sum::<f64>() / m;

    let mut ss_xy = 0.0;
    let mut ss_xx = 0.0;
    let mut ss_yy = 0.0;
    for (i, &yi) in y.iter().enumerate() {
        let x = i as f64;
        ss_xy += (x - x_mean) * (yi - y_mean);
        ss_xx += (x - x_mean).powi(2);
        ss_yy += (yi - y_mean).powi(2);
    }

    let degenerate = ss_xx == 0.0 || ss_yy == 0.0;
    let rvalue = if degenerate {
        if ss_xy == 0.0 {
            f64::NAN
        } else {
            0.0
        }
    } else {
        (ss_xy / (ss_xx * ss_yy).sqrt()).clamp(-1.0, 1.0)
    };

    let slope = ss_xy / ss_xx;
    let intercept = y_mean - slope * x_mean;

    let df = m - 2.0;
    let stderr = ((1.0 - rvalue.powi(2)) * ss_yy / ss_xx / df).sqrt();

    (slope, intercept, rvalue, stderr)
}

/// tsfresh `agg_linear_trend(x, [{attr, chunk_len, f_agg: "mean"}])` (D-07,
/// FEAT-02): chunk means over consecutive chunks of `chunk_len`, including
/// the last partial chunk (tsfresh `_aggregate_on_chunks`), then
/// [`scipy_linregress`] on the chunk means against `x = 0..m-1`. NaN for
/// all four outputs when `chunk_len >= n` (tsfresh's own degenerate case).
fn aggregated_linear_trend(values: &[f64], chunk_len: usize) -> (f64, f64, f64, f64) {
    let n = values.len();
    if chunk_len == 0 || chunk_len >= n {
        return (f64::NAN, f64::NAN, f64::NAN, f64::NAN);
    }

    let chunk_means: Vec<f64> = values
        .chunks(chunk_len)
        .map(|chunk| chunk.iter().sum::<f64>() / chunk.len() as f64)
        .collect();

    scipy_linregress(&chunk_means)
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

    // --- Task 3 (D-13/FEAT-05): tsfresh autocorrelation denominator + the
    // 117-key output invariant. Pinned values below are reproduced from the
    // live tsfresh 0.21.2 `feature_calculators.autocorrelation` calculator
    // (see 14-01-PLAN.md Task 3 <behavior>; values confirmed independently
    // via `uv run --project validation/reference/python`).

    #[test]
    fn test_autocorrelation_matches_tsfresh_denominator() {
        // tsfresh: fc.autocorrelation([1,2,3,4], lag=1) == 0.3333333333333333,
        // fc.autocorrelation([1,2,3,4], lag=2) == -0.6 ((n-lag)*var(x)
        // denominator, not the whole-series sum of squared deviations).
        let values = vec![1.0, 2.0, 3.0, 4.0];
        let features = extract_features(&values).unwrap();
        let lag1 = *features.get("autocorrelation_lag1").unwrap();
        let lag2 = *features.get("autocorrelation_lag2").unwrap();
        assert!(
            (lag1 - 0.3333333333333333).abs() < 1e-12,
            "autocorrelation_lag1: got {lag1}, want 0.3333333333333333"
        );
        assert!(
            (lag2 - (-0.6)).abs() < 1e-12,
            "autocorrelation_lag2: got {lag2}, want -0.6"
        );
    }

    #[test]
    fn test_autocorrelation_nan_on_zero_variance_or_lag_ge_n() {
        // tsfresh: fc.autocorrelation([5,5,5,5], lag=1) is NaN (zero
        // variance); fc.autocorrelation([1,2,3,4], lag=4) is NaN (lag >= n).
        let constant = vec![5.0, 5.0, 5.0, 5.0];
        let features = extract_features(&constant).unwrap();
        assert!(features.get("autocorrelation_lag1").unwrap().is_nan());

        let values = vec![1.0, 2.0, 3.0, 4.0];
        let features = extract_features(&values).unwrap();
        assert!(features.get("autocorrelation_lag4").unwrap().is_nan());
    }

    #[test]
    fn test_117_key_invariant_across_lengths() {
        let names = list_features();
        for &n in &[1usize, 2, 3, 5, 8, 10] {
            let values: Vec<f64> = (1..=n).map(|i| i as f64).collect();
            let features = extract_features(&values)
                .unwrap_or_else(|e| panic!("n={n}: extract_features failed: {e:?}"));
            for name in &names {
                assert!(features.contains_key(name), "n={n}: missing key {name:?}");
            }
            assert_eq!(
                features.len(),
                names.len(),
                "n={n}: extract_features returned {} keys, list_features() has {}",
                features.len(),
                names.len()
            );
        }
    }

    #[test]
    fn test_n1_edge_case_values() {
        let features = extract_features(&[5.0]).unwrap();
        assert_eq!(features.get("absolute_sum_of_changes"), Some(&0.0));
        assert_eq!(features.get("cid_ce"), Some(&0.0));
        assert!(features.get("mean_change").unwrap().is_nan());
        assert!(features.get("mean_abs_change").unwrap().is_nan());
        assert!(features
            .get("mean_second_derivative_central")
            .unwrap()
            .is_nan());
    }

    #[test]
    fn test_time_reversal_and_c3_zero_when_2lag_ge_n() {
        // n=4: lag=2 has 2*lag=4 >= n -> 0.0 (tsfresh); lag=1 has 2*lag=2 < n,
        // still computed normally (not asserted here).
        let values = vec![1.0, 2.0, 3.0, 4.0];
        let features = extract_features(&values).unwrap();
        assert_eq!(features.get("time_reversal_asymmetry_stat_2"), Some(&0.0));
        assert_eq!(features.get("c3_lag2"), Some(&0.0));
        assert_eq!(features.get("time_reversal_asymmetry_stat_3"), Some(&0.0));
        assert_eq!(features.get("c3_lag3"), Some(&0.0));
    }

    // --- Task 1 (14-02, FEAT-02): last_location_of_max/min tsfresh-exact
    // ((idx+1)/n, exact-equality location), first_location_* unchanged.

    #[test]
    fn test_last_location_of_max_min_tsfresh_exact() {
        // [1, 3, 3, 2]: max=3 at indices 1,2 (0-based); last_location_of_maximum
        // = (2+1)/4 = 0.75; first_location_of_maximum = 1/4 = 0.25.
        let values = vec![1.0, 3.0, 3.0, 2.0];
        let features = extract_features(&values).unwrap();
        assert_eq!(
            features.get("last_location_of_maximum"),
            Some(&0.75),
            "last_location_of_maximum"
        );
        assert_eq!(
            features.get("first_location_of_maximum"),
            Some(&0.25),
            "first_location_of_maximum"
        );
    }

    #[test]
    fn test_last_location_constant_series_is_one() {
        let values = vec![5.0, 5.0, 5.0];
        let features = extract_features(&values).unwrap();
        assert_eq!(features.get("last_location_of_maximum"), Some(&1.0));
        assert_eq!(features.get("last_location_of_minimum"), Some(&1.0));
        assert_eq!(features.get("first_location_of_maximum"), Some(&0.0));
        assert_eq!(features.get("first_location_of_minimum"), Some(&0.0));
    }

    #[test]
    fn test_last_location_of_minimum_two_point() {
        // [1, 2]: min=1 at index 0; last_location_of_minimum = (0+1)/2 = 0.5.
        let values = vec![1.0, 2.0];
        let features = extract_features(&values).unwrap();
        assert_eq!(features.get("last_location_of_minimum"), Some(&0.5));
    }

    // --- Task 2 (14-02, FEAT-02/D-05): bias-corrected skewness/kurtosis
    // shared with ts_stats, signed variation_coefficient, the
    // mean_second_derivative_central 1/2 factor, and the
    // time_reversal_asymmetry term order. Pinned values reproduced from
    // tsfresh 0.21.2 / this project's own scipy-moments fixture (see
    // 14-02-PLAN.md Task 2 <behavior>).

    #[test]
    fn test_skewness_kurtosis_bias_corrected_matches_tsfresh() {
        // short_n20 (validation/reference/fixtures/tsfresh_features/short_n20.json):
        // tsfresh skewness 0.30292879825084285, kurtosis 0.27652865463225274
        // (bias-corrected G1/G2) -- current Rust is the biased population
        // moment ratio and will NOT match these values.
        let values = vec![
            3.337, 2.674, 2.292, 3.491, 2.217, 4.765, 3.613, 3.258, 0.928, 0.481, 1.015, 3.755,
            3.21, 0.738, 3.727, 1.651, 1.653, 6.178, 3.053, 3.501,
        ];
        let features = extract_features(&values).unwrap();
        let skewness = *features.get("skewness").unwrap();
        let kurtosis = *features.get("kurtosis").unwrap();
        assert!(
            (skewness - 0.30292879825084285).abs() < 1e-9,
            "skewness: got {skewness}, want 0.30292879825084285 (bias-corrected G1)"
        );
        assert!(
            (kurtosis - 0.27652865463225274).abs() < 1e-9,
            "kurtosis: got {kurtosis}, want 0.27652865463225274 (bias-corrected G2)"
        );
    }

    #[test]
    fn test_variation_coefficient_is_signed() {
        // tsfresh variation_coefficient = std(x)/mean(x), signed.
        let negative_mean = vec![-1.0, -2.0, -3.0];
        let features = extract_features(&negative_mean).unwrap();
        let vc = *features.get("variation_coefficient").unwrap();
        assert!(
            (vc - (-0.408248290463863)).abs() < 1e-9,
            "variation_coefficient([-1,-2,-3]): got {vc}, want -0.408248290463863 (signed)"
        );

        let zero_mean = vec![-1.0, 0.0, 1.0];
        let features = extract_features(&zero_mean).unwrap();
        assert!(
            features.get("variation_coefficient").unwrap().is_nan(),
            "variation_coefficient([-1,0,1]): want NaN (mean == 0)"
        );

        let constant = vec![7.0, 7.0, 7.0];
        let features = extract_features(&constant).unwrap();
        assert_eq!(features.get("variation_coefficient"), Some(&0.0));
    }

    #[test]
    fn test_mean_second_derivative_central_half_factor() {
        // (x[-1]-x[-2]-x[1]+x[0]) / (2*(n-2)): [1,4,9,16,25] ->
        // (25-16-4+1)/(2*3) = 6/6 = 1.0; current Rust is missing the /2
        // factor and will report 2.0.
        let values = vec![1.0, 4.0, 9.0, 16.0, 25.0];
        let features = extract_features(&values).unwrap();
        assert_eq!(features.get("mean_second_derivative_central"), Some(&1.0));
    }

    #[test]
    fn test_time_reversal_asymmetry_term_order() {
        // mean(x[i+2lag]^2*x[i+lag] - x[i+lag]*x[i]^2), lag=1, over [1,2,3,4,5]
        // = (18-2 + 48-12 + 100-36)/3 = 38.666666666666664; current Rust has
        // the second term's operands transposed (x_lag*x^2 instead of
        // x*x_lag^2) and will report a different value.
        let values = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let features = extract_features(&values).unwrap();
        let stat1 = *features.get("time_reversal_asymmetry_stat_1").unwrap();
        assert!(
            (stat1 - 38.666666666666664).abs() < 1e-9,
            "time_reversal_asymmetry_stat_1: got {stat1}, want 38.666666666666664"
        );
    }

    #[test]
    fn test_fft_coefficient_nan_beyond_available_bins() {
        // n=8: numpy rfft produces n/2+1 = 5 bins (0..=4); bins 5..9 must be
        // present as NaN (D-07, 14-03) -- the 117-key invariant inserts the
        // key regardless, it just can't have a real value beyond the
        // one-sided spectrum's length.
        let values: Vec<f64> = (0..8).map(|i| i as f64).collect();
        let features = extract_features(&values).unwrap();
        for i in 5..10 {
            for attr in ["real", "imag", "abs"] {
                let key = format!("fft_coefficient_{i}_{attr}");
                let v = *features
                    .get(&key)
                    .unwrap_or_else(|| panic!("missing key {key:?}"));
                assert!(
                    v.is_nan(),
                    "{key}: got {v}, want NaN (n=8, bin unavailable)"
                );
            }
        }
    }

    // --- Task 3 (14-02, FEAT-02): agg_linear_trend / linear_trend_r_squared
    // on scipy 1.18 linregress semantics (signed rvalue, NaN degenerate
    // cases). aggregated_linear_trend is exercised directly for precise
    // control over chunk_len; see 14-02-PLAN.md Task 3 <behavior>.

    #[test]
    fn test_agg_linear_trend_nan_when_chunk_len_ge_n() {
        // n=1, n=2 with chunk_len=2 (chunk_len >= n): all four NaN.
        let (slope, intercept, rvalue, stderr) = aggregated_linear_trend(&[5.0], 2);
        assert!(slope.is_nan() && intercept.is_nan() && rvalue.is_nan() && stderr.is_nan());

        let (slope, intercept, rvalue, stderr) = aggregated_linear_trend(&[5.0, 7.0], 2);
        assert!(slope.is_nan() && intercept.is_nan() && rvalue.is_nan() && stderr.is_nan());
    }

    #[test]
    fn test_agg_linear_trend_signed_rvalue() {
        // chunk_len=2 over 3 chunks -> chunk means [10, 6, 2] (perfectly
        // linear, decreasing): current Rust always returns sqrt(r^2)
        // (non-negative) and will report +1.0 instead of -1.0.
        let values = vec![10.0, 10.0, 6.0, 6.0, 2.0, 2.0];
        let (slope, intercept, rvalue, stderr) = aggregated_linear_trend(&values, 2);
        assert!((slope - (-4.0)).abs() < 1e-12, "slope: got {slope}");
        assert!(
            (intercept - 10.0).abs() < 1e-12,
            "intercept: got {intercept}"
        );
        assert!(
            (rvalue - (-1.0)).abs() < 1e-12,
            "agg_linear_trend_rvalue: got {rvalue}, want -1.0 (signed)"
        );
        assert!((stderr - 0.0).abs() < 1e-12, "stderr: got {stderr}");
    }

    #[test]
    fn test_agg_linear_trend_degenerate_constant_chunk_means() {
        // Constant series: every chunk mean equals the constant. ss_yy == 0
        // (degenerate), ss_xy == 0 -> rvalue NaN; slope 0.0; intercept the
        // constant; stderr NaN (0/0, current Rust returns 0.0).
        let values = vec![7.0; 10];
        let (slope, intercept, rvalue, stderr) = aggregated_linear_trend(&values, 2);
        assert_eq!(slope, 0.0, "slope");
        assert_eq!(intercept, 7.0, "intercept");
        assert!(
            rvalue.is_nan(),
            "agg_linear_trend_rvalue: got {rvalue}, want NaN"
        );
        assert!(
            stderr.is_nan(),
            "agg_linear_trend_stderr: got {stderr}, want NaN"
        );
    }

    #[test]
    fn test_agg_linear_trend_two_chunk_means_stderr_nan() {
        // m=2 chunk means [2.0, 6.0] (df = m-2 = 0): rvalue is exactly ±1.0
        // (perfect 2-point fit), so (1 - r^2) == 0.0 exactly and
        // stderr = sqrt(0.0/ss_xx/0.0) -> 0/0 -> NaN, as scipy 1.18 does.
        let values = vec![1.0, 3.0, 5.0, 7.0];
        let (slope, intercept, rvalue, stderr) = aggregated_linear_trend(&values, 2);
        assert!((slope - 4.0).abs() < 1e-12, "slope: got {slope}");
        assert!(
            (intercept - 2.0).abs() < 1e-12,
            "intercept: got {intercept}"
        );
        assert!((rvalue - 1.0).abs() < 1e-12, "rvalue: got {rvalue}");
        assert!(
            stderr.is_nan(),
            "agg_linear_trend_stderr: got {stderr}, want NaN (0/0, df=0)"
        );
    }

    #[test]
    fn test_linear_trend_r_squared_nan_for_constant_series() {
        let constant = vec![7.0; 10];
        let features = extract_features(&constant).unwrap();
        assert!(
            features.get("linear_trend_r_squared").unwrap().is_nan(),
            "linear_trend_r_squared: want NaN for a constant series"
        );

        let perfect_line = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let features = extract_features(&perfect_line).unwrap();
        assert_eq!(
            features.get("linear_trend_r_squared"),
            Some(&1.0),
            "linear_trend_r_squared: perfect-fit line should clamp to 1.0"
        );
    }

    // --- Task 2 (14-03, FEAT-03): large-prime-n and degenerate-input
    // spectrum tests. one_sided_spectrum is exercised directly (not via
    // extract_features) so these tests don't pay for the other 116
    // features' cost on n = 100003 / 131072.

    #[test]
    fn test_one_sided_spectrum_prime_n_cosine_peak() {
        // n = 100003 is prime -- a naive O(n^2) DFT would need ~1e10
        // operations; rustfft's Rader/Bluestein path keeps this O(n log n)
        // and the test completes in well under a second in release-mode
        // cargo test (D-07).
        let n = 100_003usize;
        let values: Vec<f64> = (0..n)
            .map(|t| (2.0 * std::f64::consts::PI * 5.0 * t as f64 / n as f64).cos())
            .collect();
        let spectrum = one_sided_spectrum(&values);
        let mag_5 = spectrum[5].norm();
        let mag_4 = spectrum[4].norm();
        let n_f = n as f64;
        assert!(
            (mag_5 - n_f / 2.0).abs() < 1e-6 * n_f,
            "|X_5|: got {mag_5}, want ~{} (n/2)",
            n_f / 2.0
        );
        assert!(
            mag_4 < 1e-6 * n_f,
            "|X_4|: got {mag_4}, want < {}",
            1e-6 * n_f
        );
    }

    #[test]
    fn test_one_sided_spectrum_parseval_unnormalised() {
        // n = 131072 = 2^17, deterministic pseudo-random input. One-sided
        // Parseval: |X_0|^2 + 2*sum_{k=1}^{n/2-1} |X_k|^2 + |X_{n/2}|^2 ==
        // n * sum(x^2) to 1e-9 relative -- proves the transform is
        // unnormalised (a normalised transform would be off by a factor of
        // n or n^2 here).
        let n = 131_072usize;
        // Simple deterministic LCG, avoids pulling in a `rand` dependency
        // for a single test.
        let mut state: u64 = 0x2545_F491_4F6C_DD1D;
        let values: Vec<f64> = (0..n)
            .map(|_| {
                state = state
                    .wrapping_mul(6_364_136_223_846_793_005)
                    .wrapping_add(1_442_695_040_888_963_407);
                ((state >> 11) as f64 / (1u64 << 53) as f64) * 2.0 - 1.0
            })
            .collect();

        let spectrum = one_sided_spectrum(&values);
        assert_eq!(spectrum.len(), n / 2 + 1);

        let bin0 = spectrum[0].norm_sqr();
        let bin_nyquist = spectrum[n / 2].norm_sqr();
        let middle_sum: f64 = spectrum[1..n / 2].iter().map(|c| c.norm_sqr()).sum();
        let one_sided_energy = bin0 + 2.0 * middle_sum + bin_nyquist;

        let time_domain_energy: f64 = n as f64 * values.iter().map(|v| v * v).sum::<f64>();

        let rel_diff = (one_sided_energy - time_domain_energy).abs() / time_domain_energy;
        assert!(
            rel_diff < 1e-9,
            "Parseval mismatch: one-sided {one_sided_energy}, n*sum(x^2) {time_domain_energy}, rel diff {rel_diff}"
        );
    }

    #[test]
    fn test_spectrum_degenerate_inputs_no_panic() {
        // Constant series: fft_coefficient_0_real == n*c (unnormalised,
        // D-07); spectral_centroid/variance within 1e-12 of 0 (all
        // non-zero-frequency bins carry ~0 magnitude).
        let constant = vec![7.0; 30];
        let features = extract_features(&constant).unwrap();
        assert_eq!(
            features.get("fft_coefficient_0_real"),
            Some(&210.0),
            "fft_coefficient_0_real: want 30*7.0 = 210.0 (unnormalised)"
        );
        let centroid = *features.get("spectral_centroid").unwrap();
        let variance = *features.get("spectral_variance").unwrap();
        assert!(
            centroid.abs() < 1e-12,
            "spectral_centroid: got {centroid}, want ~0.0"
        );
        assert!(
            variance.abs() < 1e-12,
            "spectral_variance: got {variance}, want ~0.0"
        );

        // All-zero series: spectral_centroid/variance are 0.0/0.0 -> NaN,
        // same as tsfresh's get_moment (no epsilon guard, D-07).
        let all_zero = vec![0.0; 50];
        let features = extract_features(&all_zero).unwrap();
        assert!(features.get("spectral_centroid").unwrap().is_nan());
        assert!(features.get("spectral_variance").unwrap().is_nan());

        // n = 1, 2, 3: no panic.
        for n in 1..=3 {
            let values: Vec<f64> = (0..n).map(|i| i as f64 + 1.0).collect();
            let spectrum = one_sided_spectrum(&values);
            assert_eq!(spectrum.len(), n / 2 + 1);
            let features = extract_features(&values);
            assert!(
                features.is_ok(),
                "extract_features panicked/errored for n={n}"
            );
        }
    }

    // --- Task 1 (D-08, FEAT-04): tsfresh sample_entropy on the shared
    // template_match_counts band search. Literal values reproduced from the
    // live tsfresh 0.21.2 `sample_entropy` calculator (14-RESEARCH.md probe
    // values / mapping.json fixtures).

    #[test]
    fn test_sample_entropy_alternating_pair() {
        let values: Vec<f64> = [1.0, 2.0].iter().cycle().take(10).cloned().collect();
        let got = sample_entropy(&values);
        assert!(
            (got - 0.2876820724517809).abs() < 1e-12,
            "sample_entropy([1,2]*5): got {got}, want 0.2876820724517809"
        );
    }

    #[test]
    fn test_sample_entropy_n3_is_nan() {
        let got = sample_entropy(&[1.0, 2.0, 3.0]);
        assert!(got.is_nan(), "sample_entropy([1,2,3]): got {got}, want NaN");
    }

    #[test]
    fn test_sample_entropy_constant_matches_fixture() {
        // constant_n30 fixture (validation/reference/fixtures/tsfresh_features/
        // constant_n30.json): 30 copies of 7.0, tsfresh sample_entropy ==
        // 0.07145896398214498.
        let values = vec![7.0; 30];
        let got = sample_entropy(&values);
        assert!(
            (got - 0.07145896398214498).abs() < 1e-12,
            "sample_entropy(constant_n30): got {got}, want 0.07145896398214498"
        );
    }

    #[test]
    fn test_sample_entropy_nan_input_is_nan() {
        let got = sample_entropy(&[1.0, 2.0, f64::NAN, 4.0, 5.0]);
        assert!(
            got.is_nan(),
            "sample_entropy with a NaN element: got {got}, want NaN"
        );
    }

    // --- Task 2 (D-08, FEAT-04): approximate_entropy, binned_entropy,
    // benford_correlation, permutation_entropy, lempel_ziv_complexity,
    // -0.0-safe unique/duplicate handling, and the NaN-input no-panic
    // invariant. Literal values reproduced from the live tsfresh 0.21.2
    // calculators (14-RESEARCH.md probe values / mapping.json fixtures).

    #[test]
    fn test_approximate_entropy_degenerate_inputs_are_zero() {
        // n <= m+1 (tsfresh's own degenerate branch).
        let got = approximate_entropy(&[1.0, 2.0, 3.0], 2, 0.2);
        assert_eq!(
            got, 0.0,
            "approximate_entropy([1,2,3], 2, 0.2): got {got}, want 0.0"
        );

        // Constant series: r=0, every template matches every other -- Phi_m
        // - Phi_{m+1} collapses to 0.0 exactly (previously NaN).
        let constant = vec![3.0; 30];
        let got = approximate_entropy(&constant, 2, 0.2);
        assert_eq!(
            got, 0.0,
            "approximate_entropy(constant_n30): got {got}, want 0.0"
        );

        // All-zero series: same collapse.
        let all_zero = vec![0.0; 50];
        let got = approximate_entropy(&all_zero, 2, 0.2);
        assert_eq!(
            got, 0.0,
            "approximate_entropy(all_zero_n50): got {got}, want 0.0"
        );
    }

    #[test]
    fn test_approximate_entropy_nan_input_is_nan() {
        let got = approximate_entropy(&[1.0, f64::NAN, 3.0, 4.0, 5.0], 2, 0.2);
        assert!(
            got.is_nan(),
            "approximate_entropy with a NaN element: got {got}, want NaN"
        );
    }

    #[test]
    fn test_binned_entropy_matches_numpy_histogram() {
        let values: Vec<f64> = (0..=10).map(|i| i as f64).collect();
        let got = binned_entropy(&values, 10);
        assert!(
            (got - 2.2718685126965625).abs() < 1e-12,
            "binned_entropy(arange(11.0), 10): got {got}, want 2.2718685126965625"
        );
    }

    #[test]
    fn test_binned_entropy_nan_input_is_nan() {
        let got = binned_entropy(&[1.0, f64::NAN, 2.0], 10);
        assert!(
            got.is_nan(),
            "binned_entropy([1, NaN, 2], 10): got {got}, want NaN"
        );
    }

    #[test]
    fn test_binned_entropy_constant_series_is_zero() {
        let got = binned_entropy(&[5.0; 30], 10);
        assert_eq!(
            got, 0.0,
            "binned_entropy(constant, 10): got {got}, want 0.0"
        );
    }

    #[test]
    fn test_benford_correlation_matches_tsfresh() {
        let values = [0.0034, 25.0, 0.0, 1000.0, 999.9999999999999, 7.0, 1.5, 0.12];
        let got = benford_correlation(&values);
        assert!(
            (got - 0.82727315443529).abs() < 1e-9,
            "benford_correlation: got {got}, want 0.82727315443529"
        );
    }

    #[test]
    fn test_benford_correlation_all_zero_is_nan() {
        let got = benford_correlation(&[0.0; 50]);
        assert!(
            got.is_nan(),
            "benford_correlation(all_zero): got {got}, want NaN"
        );
    }

    #[test]
    fn test_permutation_entropy_matches_tsfresh_unnormalised() {
        let values = [4.0, 7.0, 9.0, 10.0, 6.0, 11.0, 3.0];
        let got = permutation_entropy(&values, 3);
        assert!(
            (got - 1.0549201679861442).abs() < 1e-12,
            "permutation_entropy: got {got}, want 1.0549201679861442"
        );
    }

    #[test]
    fn test_lempel_ziv_complexity_matches_tsfresh() {
        let values = [0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0];
        let got = lempel_ziv_complexity(&values, 2);
        assert!(
            (got - 0.6).abs() < 1e-12,
            "lempel_ziv_complexity: got {got}, want 0.6"
        );
    }

    #[test]
    fn test_lempel_ziv_complexity_constant_matches_fixture() {
        // constant_n30 fixture: tsfresh lempel_ziv_complexity(bins=2) ==
        // 7/30 == 0.23333333333333334.
        let got = lempel_ziv_complexity(&[7.0; 30], 2);
        assert!(
            (got - (7.0 / 30.0)).abs() < 1e-12,
            "lempel_ziv_complexity(constant_n30): got {got}, want {}",
            7.0 / 30.0
        );
    }

    #[test]
    fn test_negative_zero_treated_as_zero_in_unique_duplicate_features() {
        let values = [0.0, -0.0, 1.0];
        assert_eq!(
            count_unique(&values),
            2,
            "count_unique([0.0, -0.0, 1.0]): np.unique treats -0.0 == 0.0"
        );
        assert!(
            has_duplicate(&values),
            "has_duplicate([0.0, -0.0, 1.0]): -0.0 and 0.0 are one value, so it's a duplicate"
        );

        let features = extract_features(&values).unwrap();
        assert_eq!(features.get("count_unique"), Some(&2.0));
        assert!((features.get("ratio_value_number_to_length").unwrap() - 2.0 / 3.0).abs() < 1e-12);
        assert_eq!(features.get("has_duplicate"), Some(&1.0));
    }

    #[test]
    fn test_extract_features_with_nan_returns_117_keys_without_panicking() {
        let values = [1.0, f64::NAN, 3.0, 4.0, 5.0, 6.0];
        let features = extract_features(&values).unwrap();
        assert_eq!(
            features.len(),
            117,
            "extract_features with a NaN element: expected 117 keys, got {}",
            features.len()
        );
    }
}
