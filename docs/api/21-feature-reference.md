# Feature Reference

> Complete reference for all 117 tsfresh-inspired features

## Overview

This document lists every feature returned by `ts_features_by`, `ts_features_table`, and `ts_features_agg`. For API usage and examples, see [Feature Extraction](20-feature-extraction.md).

All features return `DOUBLE` values. Boolean features use `1.0` (true) / `0.0` (false). Location features are normalized to `[0, 1]` relative to series length. All 117 features are verified against tsfresh 0.21.2 reference fixtures in `validation/reference/fixtures/tsfresh_features/` (Phase 14) — every feature matches tsfresh (or scipy/numpy for the 9 features with no tsfresh counterpart) within `abs <= 1e-9 + 1e-9*|ref|`, or carries a documented, intentional deviation (see the Deviation column below and [`validation/reference/README.md`](../../validation/reference/README.md)). A feature undefined for the given input length (e.g. too few points) returns `NaN`, not an error.

---

## Basic Statistics (10)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `length` | Number of observations (tsfresh length). | `feature_calculators.length` | — |
| `sum` | Sum of all values (tsfresh sum_values). | `feature_calculators.sum_values` | — |
| `mean` | Arithmetic mean (tsfresh mean). | `feature_calculators.mean` | — |
| `minimum` | Minimum value (tsfresh minimum). | `feature_calculators.minimum` | — |
| `maximum` | Maximum value (tsfresh maximum). | `feature_calculators.maximum` | — |
| `range` | Range (max - min); no tsfresh counterpart (D-02). | none — numpy/scipy reference: np.max(x) - np.min(x) | — |
| `variance` | Population variance, ddof=0 (tsfresh variance). | `feature_calculators.variance` | — |
| `standard_deviation` | Population standard deviation, ddof=0 (tsfresh standard_deviation). | `feature_calculators.standard_deviation` | — |
| `variation_coefficient` | std(x) / mean(x), signed (tsfresh variation_coefficient). | `feature_calculators.variation_coefficient` | — |
| `large_standard_deviation` | 1.0 if std(x) > r * (max-min) else 0.0, r=0.25 (tsfresh large_standard_deviation). | `feature_calculators.large_standard_deviation(r=0.25)` | — |

---

## Median & Quantiles (5)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `median` | Median value (tsfresh median). | `feature_calculators.median` | — |
| `quantile_0.1` | Quantile q=0.1 (tsfresh quantile). | `feature_calculators.quantile(q=0.1)` | — |
| `quantile_0.25` | Quantile q=0.25 (tsfresh quantile). | `feature_calculators.quantile(q=0.25)` | — |
| `quantile_0.75` | Quantile q=0.75 (tsfresh quantile). | `feature_calculators.quantile(q=0.75)` | — |
| `quantile_0.9` | Quantile q=0.9 (tsfresh quantile). | `feature_calculators.quantile(q=0.9)` | — |

---

## Distribution Shape (2)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `skewness` | pandas Series.skew (bias-corrected G1), shared with ts_stats (D-05). | `feature_calculators.skewness` | zero-variance sample: G1/G2 are 0/0; scipy skew/kurtosis(bias=False) and ts_stats return NaN (D-05); pandas/tsfresh return 0 by convention, which is arguably wrong for an undefined moment (D-03). (series: constant_n30, all_zero_n50; our value: NaN) |
| `kurtosis` | pandas Series.kurtosis (bias-corrected excess G2), shared with ts_stats (D-05). | `feature_calculators.kurtosis` | zero-variance sample: G1/G2 are 0/0; scipy skew/kurtosis(bias=False) and ts_stats return NaN (D-05); pandas/tsfresh return 0 by convention, which is arguably wrong for an undefined moment (D-03). (series: constant_n30, all_zero_n50; our value: NaN) |

---

## Counting (3)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `count_above_mean` | Count of values strictly above the mean (tsfresh count_above_mean). | `feature_calculators.count_above_mean` | — |
| `count_below_mean` | Count of values strictly below the mean (tsfresh count_below_mean). | `feature_calculators.count_below_mean` | — |
| `percentage_above_mean` | Fraction of values strictly above the mean; no tsfresh counterpart (D-02). | none — numpy/scipy reference: np.sum(x > np.mean(x)) / n | — |

---

## Changes & Crossings (3)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `zero_crossing_rate` | Zero-crossing rate; tsfresh's closest calculator (number_crossing_m) has no rate/exact-zero-exclusion semantics, so this stays a documented numpy reference (D-02, Open Question 1). | none — numpy/scipy reference: count of i in 0..n-2 with x[i]!=0, x[i+1]!=0 and sign(x[i])!=sign(x[i+1]), divided by max(n-1,1) | — |
| `mean_change` | Mean of first differences (tsfresh mean_change). | `feature_calculators.mean_change` | — |
| `mean_abs_change` | Mean of abs(first differences) (tsfresh mean_abs_change). | `feature_calculators.mean_abs_change` | — |

---

## Values & Locations (6)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `first_value` | First value; no tsfresh counterpart (D-02). | none — numpy/scipy reference: x[0] | — |
| `last_value` | Last value; no tsfresh counterpart (D-02). | none — numpy/scipy reference: x[-1] | — |
| `first_location_of_maximum` | argmax(x)/n, 0-based (tsfresh first_location_of_maximum). | `feature_calculators.first_location_of_maximum` | — |
| `last_location_of_maximum` | 1 - argmax(x[::-1])/n == (last_idx+1)/n, 0-based last_idx (tsfresh last_location_of_maximum). | `feature_calculators.last_location_of_maximum` | — |
| `first_location_of_minimum` | argmin(x)/n, 0-based (tsfresh first_location_of_minimum). | `feature_calculators.first_location_of_minimum` | — |
| `last_location_of_minimum` | 1 - argmin(x[::-1])/n == (last_idx+1)/n, 0-based last_idx (tsfresh last_location_of_minimum). | `feature_calculators.last_location_of_minimum` | — |

---

## Energy (2)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `abs_energy` | Sum of squared values (tsfresh abs_energy). | `feature_calculators.abs_energy` | — |
| `root_mean_square` | sqrt(mean(x^2)) (tsfresh root_mean_square). | `feature_calculators.root_mean_square` | — |

---

## Derivatives & Complexity (4)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `mean_second_derivative_central` | (x[-1]-x[-2]-x[1]+x[0]) / (2*(n-2)) (tsfresh mean_second_derivative_central). | `feature_calculators.mean_second_derivative_central` | — |
| `cid_ce` | Complexity-invariant distance of the (unnormalized) series to itself (tsfresh cid_ce, normalize=false). | `feature_calculators.cid_ce(normalize=False)` | — |
| `absolute_sum_of_changes` | Sum of abs(first differences) (tsfresh absolute_sum_of_changes). | `feature_calculators.absolute_sum_of_changes` | — |
| `lempel_ziv_complexity` | tsfresh's bins=2 range-midpoint (linspace) quantization + sub-word dictionary-growth loop, result = dictionary size / n. | `feature_calculators.lempel_ziv_complexity(bins=2)` | — |

---

## Strikes & Peaks (5)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `longest_strike_above_mean` | Longest run of consecutive values above the mean (tsfresh longest_strike_above_mean). | `feature_calculators.longest_strike_above_mean` | — |
| `longest_strike_below_mean` | Longest run of consecutive values below the mean (tsfresh longest_strike_below_mean). | `feature_calculators.longest_strike_below_mean` | — |
| `number_peaks` | Count of points exceeding all neighbours within +/-n (n=1 support; tsfresh number_peaks). | `feature_calculators.number_peaks(n=1)` | — |
| `number_peaks_threshold_1` | Sigma-scaled amplitude-thresholded peak count, k=1; no tsfresh counterpart (D-02). | none — numpy/scipy reference: count of interior i with x[i]>x[i-1], x[i]>x[i+1] and abs(x[i]-mean(x)) > 1*std(x) | — |
| `number_peaks_threshold_2` | Sigma-scaled amplitude-thresholded peak count, k=2; no tsfresh counterpart (D-02). | none — numpy/scipy reference: count of interior i with x[i]>x[i-1], x[i]>x[i+1] and abs(x[i]-mean(x)) > 2*std(x) | — |

---

## Correlation & Trend (4)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `benford_correlation` | Correlation of the first-significant-digit distribution (via shortest scientific notation, incl. abs(v)<1) to Benford's law log10(1+1/d), denominator n (zeros/NaN count toward n as digit 0). | `feature_calculators.benford_correlation` | — |
| `linear_trend_slope` | OLS slope of x against index (tsfresh linear_trend, attr=slope). | `feature_calculators.linear_trend(attr=slope)` | — |
| `linear_trend_intercept` | OLS intercept of x against index (tsfresh linear_trend, attr=intercept). | `feature_calculators.linear_trend(attr=intercept)` | — |
| `linear_trend_r_squared` | scipy.stats.linregress(np.arange(n), x).rvalue ** 2; NaN for a constant series (scipy degenerate rvalue is NaN). | none — numpy/scipy reference: scipy.stats.linregress(np.arange(n), x).rvalue ** 2 | — |

---

## Entropy (4)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `binned_entropy` | Shannon entropy over np.histogram(x, bins=10)'s floor-based bins (edge corrections, min==max widened by 0.5); empty bins contribute 0; NaN on NaN input. | `feature_calculators.binned_entropy(max_bins=10)` | — |
| `sample_entropy` | tsfresh sample_entropy: m=2, r=0.2*population std(x), Chebyshev distance, full symmetric match-count matrix minus self-matches (B/A), -ln(A/B) on the raw sums -- shares the stats.rs template_match_counts band search with ts_stats' approximate_entropy. | `feature_calculators.sample_entropy` | — |
| `approximate_entropy` | tsfresh approximate_entropy: m=2, r=0.2*population std(x) (Pincus Phi_m - Phi_{m+1}), 0.0 when n<=m+1 (tsfresh's own degenerate branch, not NaN) -- shares the stats.rs template_match_counts band search. | `feature_calculators.approximate_entropy(m=2, r=0.2)` | — |
| `permutation_entropy` | -sum(p*ln(p)) over rank-encoded windows (dimension=3, tau=1), NOT normalized by ln(dimension!) -- the stable single-argsort pattern key gives identical equivalence classes to tsfresh's literal double-argsort. | `feature_calculators.permutation_entropy(tau=1, dimension=3)` | — |

---

## Unique & Duplicates (9)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `count_unique` | Count of distinct values; no tsfresh counterpart (D-02). | none — numpy/scipy reference: np.unique(x).size | — |
| `ratio_value_number_to_length` | len(unique(x)) / n (tsfresh ratio_value_number_to_time_series_length). | `feature_calculators.ratio_value_number_to_time_series_length` | — |
| `has_duplicate` | 1.0 if any value repeats else 0.0 (tsfresh has_duplicate). | `feature_calculators.has_duplicate` | — |
| `has_duplicate_max` | 1.0 if the maximum value occurs more than once else 0.0 (tsfresh has_duplicate_max). | `feature_calculators.has_duplicate_max` | — |
| `has_duplicate_min` | 1.0 if the minimum value occurs more than once else 0.0 (tsfresh has_duplicate_min). | `feature_calculators.has_duplicate_min` | — |
| `percentage_of_reoccurring_datapoints_to_all_datapoints` | Fraction of datapoints that belong to a value occurring more than once, counted with multiplicity (tsfresh). | `feature_calculators.percentage_of_reoccurring_datapoints_to_all_datapoints` | — |
| `percentage_of_reoccurring_values_to_all_values` | Fraction of distinct values that occur more than once (tsfresh). | `feature_calculators.percentage_of_reoccurring_values_to_all_values` | — |
| `sum_of_reoccurring_values` | Sum of distinct values that occur more than once, each counted once (tsfresh sum_of_reoccurring_values). | `feature_calculators.sum_of_reoccurring_values` | — |
| `sum_of_reoccurring_datapoints` | Sum of values that occur more than once, counted with multiplicity (tsfresh sum_of_reoccurring_data_points). | `feature_calculators.sum_of_reoccurring_data_points` | — |

---

## Spectral (2)

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `spectral_centroid` | First raw moment of the one-sided abs(rfft(x)) spectrum (tsfresh fft_aggregated, aggtype=centroid), computed via rustfft's O(n log n) forward FFT. | `feature_calculators.fft_aggregated(aggtype=centroid)` | — |
| `spectral_variance` | Second central moment of the one-sided abs(rfft(x)) spectrum (tsfresh fft_aggregated, aggtype=variance), computed via rustfft's O(n log n) forward FFT. | `feature_calculators.fft_aggregated(aggtype=variance)` | — |

---

## Aggregated Trend (4)

Computed by splitting the series into chunks (chunk_len = max(length/10, 2)), fitting a linear trend to the chunk means (tsfresh `agg_linear_trend`, f_agg='mean').

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `agg_linear_trend_slope` | OLS slope of the mean-aggregated-chunk fit, chunk_len=max(n//10,2), f_agg=mean (tsfresh agg_linear_trend). | `feature_calculators.agg_linear_trend(attr=slope, chunk_len=n_div_10_min_2, f_agg=mean)` | — |
| `agg_linear_trend_intercept` | OLS intercept of the mean-aggregated-chunk fit, chunk_len=max(n//10,2), f_agg=mean (tsfresh agg_linear_trend). | `feature_calculators.agg_linear_trend(attr=intercept, chunk_len=n_div_10_min_2, f_agg=mean)` | — |
| `agg_linear_trend_rvalue` | Signed Pearson r of the OLS fit over mean-aggregated chunks of length max(n//10,2) (tsfresh agg_linear_trend, f_agg=mean). | `feature_calculators.agg_linear_trend(attr=rvalue, chunk_len=n_div_10_min_2, f_agg=mean)` | — |
| `agg_linear_trend_stderr` | OLS stderr of the mean-aggregated-chunk fit, chunk_len=max(n//10,2), f_agg=mean (tsfresh agg_linear_trend). NaN on a degenerate (zero-variance) chunk-mean series or when there are exactly 2 chunks (df=0). | `feature_calculators.agg_linear_trend(attr=stderr, chunk_len=n_div_10_min_2, f_agg=mean)` | — |

---

## Autocorrelation (10)

Pearson autocorrelation at lags 1 through 10, tsfresh's `(n-lag)*var(x)` denominator.

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `autocorrelation_lag1` | Autocorrelation at lag 1: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=1)` | — |
| `autocorrelation_lag2` | Autocorrelation at lag 2: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=2)` | — |
| `autocorrelation_lag3` | Autocorrelation at lag 3: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=3)` | — |
| `autocorrelation_lag4` | Autocorrelation at lag 4: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=4)` | — |
| `autocorrelation_lag5` | Autocorrelation at lag 5: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=5)` | — |
| `autocorrelation_lag6` | Autocorrelation at lag 6: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=6)` | — |
| `autocorrelation_lag7` | Autocorrelation at lag 7: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=7)` | — |
| `autocorrelation_lag8` | Autocorrelation at lag 8: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=8)` | — |
| `autocorrelation_lag9` | Autocorrelation at lag 9: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=9)` | — |
| `autocorrelation_lag10` | Autocorrelation at lag 10: numerator over the first n-lag pairs around the whole-series mean, denominator (n-lag)*population variance (tsfresh autocorrelation). | `feature_calculators.autocorrelation(lag=10)` | — |

---

## Partial Autocorrelation (5)

Partial autocorrelation at lags 1 through 5, via Durbin-Levinson recursion (statsmodels `pacf(method="ld")`).

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `partial_autocorrelation_lag1` | Partial autocorrelation at lag 1, Durbin-Levinson recursion on the adjusted (n-k denominator) autocovariance (statsmodels pacf(method='ld')). | `feature_calculators.partial_autocorrelation(lag=1)` | — |
| `partial_autocorrelation_lag2` | Partial autocorrelation at lag 2, Durbin-Levinson recursion on the adjusted (n-k denominator) autocovariance (statsmodels pacf(method='ld')). | `feature_calculators.partial_autocorrelation(lag=2)` | — |
| `partial_autocorrelation_lag3` | Partial autocorrelation at lag 3, Durbin-Levinson recursion on the adjusted (n-k denominator) autocovariance (statsmodels pacf(method='ld')). | `feature_calculators.partial_autocorrelation(lag=3)` | — |
| `partial_autocorrelation_lag4` | Partial autocorrelation at lag 4, Durbin-Levinson recursion on the adjusted (n-k denominator) autocovariance (statsmodels pacf(method='ld')). | `feature_calculators.partial_autocorrelation(lag=4)` | — |
| `partial_autocorrelation_lag5` | Partial autocorrelation at lag 5, Durbin-Levinson recursion on the adjusted (n-k denominator) autocovariance (statsmodels pacf(method='ld')). | `feature_calculators.partial_autocorrelation(lag=5)` | — |

---

## Ratio Beyond R Sigma (3)

Fraction of values more than r standard deviations from the mean.

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `ratio_beyond_r_sigma_1` | Fraction of values more than 1 standard deviations from the mean (tsfresh ratio_beyond_r_sigma, r=1). | `feature_calculators.ratio_beyond_r_sigma(r=1)` | — |
| `ratio_beyond_r_sigma_2` | Fraction of values more than 2 standard deviations from the mean (tsfresh ratio_beyond_r_sigma, r=2). | `feature_calculators.ratio_beyond_r_sigma(r=2)` | — |
| `ratio_beyond_r_sigma_3` | Fraction of values more than 3 standard deviations from the mean (tsfresh ratio_beyond_r_sigma, r=3). | `feature_calculators.ratio_beyond_r_sigma(r=3)` | — |

---

## Time Reversal Asymmetry (3)

Measures asymmetry of the time series under time reversal.

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `time_reversal_asymmetry_stat_1` | mean(x[i+2lag]^2*x[i+lag] - x[i+lag]*x[i]^2), lag=1 (tsfresh time_reversal_asymmetry_statistic). | `feature_calculators.time_reversal_asymmetry_statistic(lag=1)` | — |
| `time_reversal_asymmetry_stat_2` | mean(x[i+2lag]^2*x[i+lag] - x[i+lag]*x[i]^2), lag=2 (tsfresh time_reversal_asymmetry_statistic). | `feature_calculators.time_reversal_asymmetry_statistic(lag=2)` | — |
| `time_reversal_asymmetry_stat_3` | mean(x[i+2lag]^2*x[i+lag] - x[i+lag]*x[i]^2), lag=3 (tsfresh time_reversal_asymmetry_statistic). | `feature_calculators.time_reversal_asymmetry_statistic(lag=3)` | — |

---

## C3 Statistic (3)

Nonlinearity measure: mean of x(t) * x(t+lag) * x(t+2*lag).

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `c3_lag1` | mean(x_lead2*x_lead*x), lag=1 (tsfresh c3). | `feature_calculators.c3(lag=1)` | — |
| `c3_lag2` | mean(x_lead2*x_lead*x), lag=2 (tsfresh c3). | `feature_calculators.c3(lag=2)` | — |
| `c3_lag3` | mean(x_lead2*x_lead*x), lag=3 (tsfresh c3). | `feature_calculators.c3(lag=3)` | — |

---

## FFT Coefficients (30)

First 10 coefficients of the one-sided real FFT (numpy `rfft`-exact, unnormalised, O(n log n) via rustfft), each with real, imaginary, and absolute components. NaN beyond the n/2+1 bins a real-input FFT actually produces.

| Feature | Definition | tsfresh equivalent | Deviation |
|---------|------------|---------------------|-----------|
| `fft_coefficient_0_real` | Coefficient 0's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=0, attr=real)` | — |
| `fft_coefficient_0_imag` | Coefficient 0's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; always exactly 0.0 for a real-valued input. | `feature_calculators.fft_coefficient(coeff=0, attr=imag)` | — |
| `fft_coefficient_0_abs` | Coefficient 0's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=0, attr=abs)` | — |
| `fft_coefficient_1_real` | Coefficient 1's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=1, attr=real)` | — |
| `fft_coefficient_1_imag` | Coefficient 1's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=1, attr=imag)` | — |
| `fft_coefficient_1_abs` | Coefficient 1's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=1, attr=abs)` | — |
| `fft_coefficient_2_real` | Coefficient 2's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=2, attr=real)` | — |
| `fft_coefficient_2_imag` | Coefficient 2's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=2, attr=imag)` | — |
| `fft_coefficient_2_abs` | Coefficient 2's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=2, attr=abs)` | — |
| `fft_coefficient_3_real` | Coefficient 3's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=3, attr=real)` | — |
| `fft_coefficient_3_imag` | Coefficient 3's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=3, attr=imag)` | — |
| `fft_coefficient_3_abs` | Coefficient 3's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=3, attr=abs)` | — |
| `fft_coefficient_4_real` | Coefficient 4's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=4, attr=real)` | — |
| `fft_coefficient_4_imag` | Coefficient 4's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=4, attr=imag)` | — |
| `fft_coefficient_4_abs` | Coefficient 4's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=4, attr=abs)` | — |
| `fft_coefficient_5_real` | Coefficient 5's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=5, attr=real)` | — |
| `fft_coefficient_5_imag` | Coefficient 5's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=5, attr=imag)` | — |
| `fft_coefficient_5_abs` | Coefficient 5's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=5, attr=abs)` | — |
| `fft_coefficient_6_real` | Coefficient 6's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=6, attr=real)` | — |
| `fft_coefficient_6_imag` | Coefficient 6's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=6, attr=imag)` | — |
| `fft_coefficient_6_abs` | Coefficient 6's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=6, attr=abs)` | — |
| `fft_coefficient_7_real` | Coefficient 7's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=7, attr=real)` | — |
| `fft_coefficient_7_imag` | Coefficient 7's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=7, attr=imag)` | — |
| `fft_coefficient_7_abs` | Coefficient 7's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=7, attr=abs)` | — |
| `fft_coefficient_8_real` | Coefficient 8's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=8, attr=real)` | — |
| `fft_coefficient_8_imag` | Coefficient 8's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=8, attr=imag)` | — |
| `fft_coefficient_8_abs` | Coefficient 8's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=8, attr=abs)` | — |
| `fft_coefficient_9_real` | Coefficient 9's real part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=9, attr=real)` | — |
| `fft_coefficient_9_imag` | Coefficient 9's imag part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=9, attr=imag)` | — |
| `fft_coefficient_9_abs` | Coefficient 9's abs part of the unnormalized one-sided np.fft.rfft(x) spectrum (tsfresh fft_coefficient), computed via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN when the coefficient index is >= n/2+1. | `feature_calculators.fft_coefficient(coeff=9, attr=abs)` | — |

---

## Configurable Parameters Summary

Shipped parameter choices (not configurable at the SQL layer, D-14):

| Feature | Parameter | Value |
|---------|-----------|-------|
| `large_standard_deviation` | r (threshold ratio) | 0.25 |
| `binned_entropy` | bins | 10, numpy `np.histogram`-exact |
| `sample_entropy` | m, r | 2, 0.2 * population std |
| `approximate_entropy` | m, r | 2, 0.2 * population std |
| `permutation_entropy` | dimension, tau | 3, 1 (unnormalised, NOT divided by ln(dimension!)) |
| `lempel_ziv_complexity` | bins | 2 (range-midpoint quantisation, tsfresh's dictionary-growth algorithm) |
| `number_peaks_threshold_1` | threshold | 1.0 * std_dev |
| `number_peaks_threshold_2` | threshold | 2.0 * std_dev |
| `agg_linear_trend_*` | chunk_len, f_agg | max(length / 10, 2), mean |
| `autocorrelation_lag*` | lag | 1 through 10, `(n-lag)*var(x)` denominator |
| `partial_autocorrelation_lag*` | lag | 1 through 5, Durbin-Levinson (`pacf(method="ld")`), lags >= n/2 are NaN |
| `ratio_beyond_r_sigma_*` | r | 1, 2, 3 |
| `time_reversal_asymmetry_stat_*` | lag | 1, 2, 3 |
| `c3_lag*` | lag | 1, 2, 3 |
| `fft_coefficient_*` | coefficient index | 0 through 9, unnormalised one-sided rfft, NaN beyond n/2+1 bins |
| `spectral_centroid`, `spectral_variance` | spectrum | one-sided `abs(rfft(x))` magnitude spectrum |

---

*See also: [Feature Extraction](20-feature-extraction.md) | [Statistics](03-statistics.md) | [Changepoint Detection](06-changepoint-detection.md)*
