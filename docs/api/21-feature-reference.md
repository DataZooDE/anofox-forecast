# Feature Reference

> Complete reference for all 117 tsfresh-compatible features

## Overview

This document lists every feature returned by `ts_features_by`, `ts_features_table`, and `ts_features_agg`. For API usage and examples, see [Feature Extraction](20-feature-extraction.md).

All features return `DOUBLE` values. Boolean features use `1.0` (true) / `0.0` (false). Location features are normalized to `[0, 1]` relative to series length.

---

## Basic Statistics (10)

| Feature | Description |
|---------|-------------|
| `length` | Number of observations in the series |
| `sum` | Sum of all values |
| `mean` | Arithmetic mean |
| `minimum` | Minimum value |
| `maximum` | Maximum value |
| `range` | Difference between maximum and minimum |
| `variance` | Population variance |
| `standard_deviation` | Population standard deviation |
| `variation_coefficient` | Coefficient of variation (std_dev / abs(mean)), NaN if mean is zero |
| `large_standard_deviation` | 1.0 if std_dev > 0.25 * range, else 0.0 |

---

## Median & Quantiles (5)

| Feature | Description |
|---------|-------------|
| `median` | Median value (50th percentile) |
| `quantile_0.1` | 10th percentile |
| `quantile_0.25` | 25th percentile (Q1) |
| `quantile_0.75` | 75th percentile (Q3) |
| `quantile_0.9` | 90th percentile |

---

## Distribution Shape (2)

| Feature | Description |
|---------|-------------|
| `skewness` | Skewness (third standardized moment). Positive = right-tailed |
| `kurtosis` | Excess kurtosis (fourth standardized moment minus 3). Positive = heavy-tailed |

---

## Counting (3)

| Feature | Description |
|---------|-------------|
| `count_above_mean` | Number of values strictly above the mean |
| `count_below_mean` | Number of values strictly below the mean |
| `percentage_above_mean` | Fraction of values above the mean (0 to 1) |

---

## Changes & Crossings (3)

| Feature | Description |
|---------|-------------|
| `zero_crossing_rate` | Fraction of consecutive pairs that cross zero |
| `mean_change` | Mean of consecutive differences (last - first) / (n-1) |
| `mean_abs_change` | Mean of absolute consecutive differences |

---

## Values & Locations (6)

| Feature | Description |
|---------|-------------|
| `first_value` | First value in the series |
| `last_value` | Last value in the series |
| `first_location_of_maximum` | Relative position (0-1) of first occurrence of the maximum |
| `last_location_of_maximum` | Relative position (0-1) of last occurrence of the maximum |
| `first_location_of_minimum` | Relative position (0-1) of first occurrence of the minimum |
| `last_location_of_minimum` | Relative position (0-1) of last occurrence of the minimum |

---

## Energy (2)

| Feature | Description |
|---------|-------------|
| `abs_energy` | Sum of squared values (signal energy) |
| `root_mean_square` | Root mean square: sqrt(abs_energy / length) |

---

## Derivatives & Complexity (4)

| Feature | Description |
|---------|-------------|
| `mean_second_derivative_central` | Mean of the central second derivative approximation |
| `cid_ce` | Complexity-invariant distance estimate: sqrt of sum of squared consecutive differences |
| `absolute_sum_of_changes` | Sum of absolute consecutive differences |
| `lempel_ziv_complexity` | Lempel-Ziv complexity. Series binarized at the mean |

---

## Strikes & Peaks (5)

| Feature | Description |
|---------|-------------|
| `longest_strike_above_mean` | Length of the longest consecutive run above the mean |
| `longest_strike_below_mean` | Length of the longest consecutive run below the mean |
| `number_peaks` | Number of local peaks (higher than both neighbors) |
| `number_peaks_threshold_1` | Number of peaks with prominence > 1 * std_dev |
| `number_peaks_threshold_2` | Number of peaks with prominence > 2 * std_dev |

---

## Correlation & Trend (4)

| Feature | Description |
|---------|-------------|
| `benford_correlation` | Correlation of first-digit distribution with Benford's law |
| `linear_trend_slope` | Slope of ordinary least squares linear fit |
| `linear_trend_intercept` | Intercept of ordinary least squares linear fit |
| `linear_trend_r_squared` | R-squared (coefficient of determination) of linear fit |

---

## Entropy (4)

| Feature | Description | Default Parameters |
|---------|-------------|--------------------|
| `binned_entropy` | Shannon entropy of the histogram distribution | bins=10 |
| `sample_entropy` | Sample entropy (regularity measure) | m=2, r=0.2*std_dev |
| `approximate_entropy` | Approximate entropy (regularity measure) | m=2, r=0.2*std_dev |
| `permutation_entropy` | Permutation entropy (complexity of ordinal patterns) | order=3 |

---

## Unique & Duplicates (9)

| Feature | Description |
|---------|-------------|
| `count_unique` | Number of distinct values |
| `ratio_value_number_to_length` | count_unique / length |
| `has_duplicate` | 1.0 if any value appears more than once |
| `has_duplicate_max` | 1.0 if the maximum value appears more than once |
| `has_duplicate_min` | 1.0 if the minimum value appears more than once |
| `percentage_of_reoccurring_datapoints_to_all_datapoints` | Fraction of data points that are part of a reoccurring value |
| `percentage_of_reoccurring_values_to_all_values` | Fraction of distinct values that appear more than once |
| `sum_of_reoccurring_values` | Sum of all values that appear more than once (each counted once) |
| `sum_of_reoccurring_datapoints` | Sum of all values that appear more than once (each counted per occurrence) |

---

## Spectral (2)

| Feature | Description |
|---------|-------------|
| `spectral_centroid` | Weighted mean of DFT frequency bins by magnitude |
| `spectral_variance` | Variance of DFT frequency distribution around the centroid |

---

## Aggregated Trend (4)

Computed by splitting the series into chunks (chunk_len = length/10, minimum 2), fitting a linear trend to the chunk means.

| Feature | Description |
|---------|-------------|
| `agg_linear_trend_slope` | Slope of linear fit on chunk aggregates |
| `agg_linear_trend_intercept` | Intercept of linear fit on chunk aggregates |
| `agg_linear_trend_rvalue` | Correlation coefficient of chunk-level linear fit |
| `agg_linear_trend_stderr` | Standard error of the slope estimate |

---

## Autocorrelation (10)

Pearson autocorrelation at lags 1 through 10.

| Feature | Description |
|---------|-------------|
| `autocorrelation_lag1` | Autocorrelation at lag 1 |
| `autocorrelation_lag2` | Autocorrelation at lag 2 |
| `autocorrelation_lag3` | Autocorrelation at lag 3 |
| `autocorrelation_lag4` | Autocorrelation at lag 4 |
| `autocorrelation_lag5` | Autocorrelation at lag 5 |
| `autocorrelation_lag6` | Autocorrelation at lag 6 |
| `autocorrelation_lag7` | Autocorrelation at lag 7 |
| `autocorrelation_lag8` | Autocorrelation at lag 8 |
| `autocorrelation_lag9` | Autocorrelation at lag 9 |
| `autocorrelation_lag10` | Autocorrelation at lag 10 |

---

## Partial Autocorrelation (5)

Partial autocorrelation at lags 1 through 5, computed via Yule-Walker equations.

| Feature | Description |
|---------|-------------|
| `partial_autocorrelation_lag1` | Partial autocorrelation at lag 1 |
| `partial_autocorrelation_lag2` | Partial autocorrelation at lag 2 |
| `partial_autocorrelation_lag3` | Partial autocorrelation at lag 3 |
| `partial_autocorrelation_lag4` | Partial autocorrelation at lag 4 |
| `partial_autocorrelation_lag5` | Partial autocorrelation at lag 5 |

---

## Distribution Tails (9)

### Ratio Beyond R Sigma (3)

Fraction of values more than r standard deviations from the mean.

| Feature | Description |
|---------|-------------|
| `ratio_beyond_r_sigma_1` | Fraction of values beyond 1 * std_dev from mean |
| `ratio_beyond_r_sigma_2` | Fraction of values beyond 2 * std_dev from mean |
| `ratio_beyond_r_sigma_3` | Fraction of values beyond 3 * std_dev from mean |

### Time Reversal Asymmetry (3)

Measures asymmetry of the time series under time reversal.

| Feature | Description |
|---------|-------------|
| `time_reversal_asymmetry_stat_1` | Time reversal asymmetry statistic at lag 1 |
| `time_reversal_asymmetry_stat_2` | Time reversal asymmetry statistic at lag 2 |
| `time_reversal_asymmetry_stat_3` | Time reversal asymmetry statistic at lag 3 |

### C3 Statistic (3)

Nonlinearity measure: mean of x(t) * x(t+lag) * x(t+2*lag).

| Feature | Description |
|---------|-------------|
| `c3_lag1` | C3 nonlinearity statistic at lag 1 |
| `c3_lag2` | C3 nonlinearity statistic at lag 2 |
| `c3_lag3` | C3 nonlinearity statistic at lag 3 |

---

## FFT Coefficients (30)

First 10 coefficients of the Discrete Fourier Transform, each with real, imaginary, and absolute components.

| Feature | Description |
|---------|-------------|
| `fft_coefficient_0_real` | DFT coefficient 0, real part (equals sum of values) |
| `fft_coefficient_0_imag` | DFT coefficient 0, imaginary part (always 0) |
| `fft_coefficient_0_abs` | DFT coefficient 0, absolute value |
| `fft_coefficient_1_real` | DFT coefficient 1, real part |
| `fft_coefficient_1_imag` | DFT coefficient 1, imaginary part |
| `fft_coefficient_1_abs` | DFT coefficient 1, absolute value |
| `fft_coefficient_2_real` | DFT coefficient 2, real part |
| `fft_coefficient_2_imag` | DFT coefficient 2, imaginary part |
| `fft_coefficient_2_abs` | DFT coefficient 2, absolute value |
| `fft_coefficient_3_real` | DFT coefficient 3, real part |
| `fft_coefficient_3_imag` | DFT coefficient 3, imaginary part |
| `fft_coefficient_3_abs` | DFT coefficient 3, absolute value |
| `fft_coefficient_4_real` | DFT coefficient 4, real part |
| `fft_coefficient_4_imag` | DFT coefficient 4, imaginary part |
| `fft_coefficient_4_abs` | DFT coefficient 4, absolute value |
| `fft_coefficient_5_real` | DFT coefficient 5, real part |
| `fft_coefficient_5_imag` | DFT coefficient 5, imaginary part |
| `fft_coefficient_5_abs` | DFT coefficient 5, absolute value |
| `fft_coefficient_6_real` | DFT coefficient 6, real part |
| `fft_coefficient_6_imag` | DFT coefficient 6, imaginary part |
| `fft_coefficient_6_abs` | DFT coefficient 6, absolute value |
| `fft_coefficient_7_real` | DFT coefficient 7, real part |
| `fft_coefficient_7_imag` | DFT coefficient 7, imaginary part |
| `fft_coefficient_7_abs` | DFT coefficient 7, absolute value |
| `fft_coefficient_8_real` | DFT coefficient 8, real part |
| `fft_coefficient_8_imag` | DFT coefficient 8, imaginary part |
| `fft_coefficient_8_abs` | DFT coefficient 8, absolute value |
| `fft_coefficient_9_real` | DFT coefficient 9, real part |
| `fft_coefficient_9_imag` | DFT coefficient 9, imaginary part |
| `fft_coefficient_9_abs` | DFT coefficient 9, absolute value |

---

## Configurable Parameters Summary

Features with non-default or notable parameter choices:

| Feature | Parameter | Default Value |
|---------|-----------|---------------|
| `large_standard_deviation` | r (threshold ratio) | 0.25 |
| `binned_entropy` | bins | 10 |
| `sample_entropy` | m (embedding dimension) | 2 |
| `sample_entropy` | r (tolerance) | 0.2 * std_dev |
| `approximate_entropy` | m (embedding dimension) | 2 |
| `approximate_entropy` | r (tolerance) | 0.2 * std_dev |
| `permutation_entropy` | order | 3 |
| `number_peaks_threshold_1` | threshold | 1.0 * std_dev |
| `number_peaks_threshold_2` | threshold | 2.0 * std_dev |
| `lempel_ziv_complexity` | binarization threshold | mean |
| `agg_linear_trend_*` | chunk_len | length / 10 (min 2) |
| `autocorrelation_lag*` | lag | 1 through 10 |
| `partial_autocorrelation_lag*` | lag | 1 through 5 |
| `ratio_beyond_r_sigma_*` | r | 1, 2, 3 |
| `time_reversal_asymmetry_stat_*` | lag | 1, 2, 3 |
| `c3_lag*` | lag | 1, 2, 3 |
| `fft_coefficient_*` | coefficient index | 0 through 9 |

---

*See also: [Feature Extraction](20-feature-extraction.md) | [Statistics](03-statistics.md) | [Changepoint Detection](06-changepoint-detection.md)*
