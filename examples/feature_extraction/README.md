# Feature Extraction Examples

> **Features are the DNA of your time series - extract them to understand behavior.**

This folder contains runnable SQL examples demonstrating time series feature extraction with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_feature_examples.sql`](synthetic_feature_examples.sql) | 6 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/feature_extraction/synthetic_feature_examples.sql
```

---

## Overview

The extension extracts **117 tsfresh-compatible features** from time series data, organized into categories:

| Category | Features | Examples |
|----------|----------|----------|
| **Basic Statistics** | 15 | mean, variance, skewness, kurtosis |
| **Trend** | 5 | linear_trend_slope, linear_trend_r_squared |
| **Autocorrelation** | 20 | autocorrelation_lag1-10, partial_autocorrelation |
| **Entropy** | 5 | sample_entropy, permutation_entropy, binned_entropy |
| **Complexity** | 4 | cid_ce, lempel_ziv_complexity |
| **Spectral** | 10 | spectral_centroid, fft_coefficients |
| **Distribution** | 15 | quantiles, ratio_beyond_r_sigma |
| **Change Detection** | 10 | mean_change, absolute_sum_of_changes |
| **Peaks & Extrema** | 8 | number_peaks, first_location_of_maximum |
| **Time Reversal** | 5 | time_reversal_asymmetry_stat |
| **Other** | 20 | has_duplicate, zero_crossing_rate, c3 |

---

## Patterns Overview

### Pattern 1: Quick Start (Basic Extraction)

**Use case:** Extract all features from a single series.

```sql
SELECT ts_features(ts, value) AS features FROM my_series;
```

**See:** `synthetic_feature_examples.sql` Section 1

---

### Pattern 2: Access Specific Features

**Use case:** Extract and use individual features.

```sql
SELECT
    (ts_features(ts, value)).mean AS avg,
    (ts_features(ts, value)).variance AS var,
    (ts_features(ts, value)).skewness AS skew
FROM my_series;
```

**See:** `synthetic_feature_examples.sql` Section 2

---

### Pattern 3: Multi-Series Feature Extraction

**Use case:** Extract features for multiple time series in one query.

```sql
SELECT
    product_id,
    ts_features(date, value) AS features
FROM sales
GROUP BY product_id;
```

**See:** `synthetic_feature_examples.sql` Section 3

---

### Pattern 4: Feature Selection

**Use case:** Extract only specific features for efficiency.

```sql
SELECT
    product_id,
    ts_features(date, value, ['mean', 'variance', 'autocorrelation_lag1']) AS features
FROM sales
GROUP BY product_id;
```

**See:** `synthetic_feature_examples.sql` Section 4

---

### Pattern 5: List Available Features

**Use case:** Discover all available features and their parameters.

```sql
SELECT * FROM ts_features_list();
```

**See:** `synthetic_feature_examples.sql` Section 5

---

### Pattern 6: Feature-Based Classification

**Use case:** Use features to classify or cluster time series.

```sql
-- High-variance series
SELECT product_id
FROM (
    SELECT product_id, (ts_features(date, value)).variance AS var
    FROM sales GROUP BY product_id
)
WHERE var > 100;
```

**See:** `synthetic_feature_examples.sql` Section 6

---

## Key Concepts

### API Variants

| Function | Input | Best For |
|----------|-------|----------|
| `ts_features(ts, val)` | TIMESTAMP, DOUBLE | Single series |
| `ts_features(ts, val)` + GROUP BY | TIMESTAMP, DOUBLE | Multiple series |
| `ts_features(ts, val, features)` | With selection | Specific features only |

### Feature Categories

#### Basic Statistics
```sql
mean, median, variance, standard_deviation, skewness, kurtosis,
minimum, maximum, range, sum, length, first_value, last_value
```

#### Trend Features
```sql
linear_trend_slope, linear_trend_intercept, linear_trend_r_squared,
agg_linear_trend_slope, mean_change, mean_abs_change
```

#### Autocorrelation
```sql
autocorrelation_lag1, autocorrelation_lag2, ..., autocorrelation_lag10,
partial_autocorrelation_lag1, ..., partial_autocorrelation_lag5
```

#### Entropy & Complexity
```sql
sample_entropy, approximate_entropy, permutation_entropy,
binned_entropy, lempel_ziv_complexity, cid_ce
```

#### Spectral Features
```sql
spectral_centroid, spectral_variance,
fft_coefficient_0_real, fft_coefficient_0_imag, ...
```

### Accessing Features

Features are returned as a STRUCT. Access with dot notation:

```sql
-- Single feature
SELECT (ts_features(ts, value)).mean FROM my_series;

-- Multiple features
SELECT
    (ts_features(ts, value)).mean AS avg,
    (ts_features(ts, value)).variance AS var,
    (ts_features(ts, value)).skewness AS skew
FROM my_series;
```

---

## Tips

1. **Start Simple** - Use `ts_features_scalar` for quick exploration.

2. **Select Features** - Extract only needed features to reduce computation.

3. **Numeric Stability** - Some features (entropy) may return NULL for constant series.

4. **Series Length** - Most features require at least 10 data points.

5. **Normalization** - Consider normalizing features for ML models.

---

## Troubleshooting

### Q: Why is a feature NULL?

**A:** The series may be too short or constant. Check:
- Series length (need 10+ points for most features)
- Variance (constant series cause issues with entropy)

### Q: How do I get only specific features?

**A:** Use feature selection:
```sql
ts_features(date, value, ['mean', 'variance', 'skewness'])
```

### Q: Which features are most useful?

**A:** For forecasting: `autocorrelation_lag1`, `linear_trend_slope`, `seasonal_strength`
For classification: `mean`, `variance`, `skewness`, `kurtosis`, `entropy`
