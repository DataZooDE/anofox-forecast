# Feature Extraction Examples

> **Features are the DNA of your time series - extract them to understand behavior.**

This folder contains runnable SQL examples demonstrating time series feature extraction with the anofox-forecast extension.

## Function

| Function | Description |
|----------|-------------|
| `ts_features_by` | Extract 117 tsfresh-compatible features for multiple series |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_feature_examples.sql`](synthetic_feature_examples.sql) | Multi-series feature extraction examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/feature_extraction/synthetic_feature_examples.sql
```

---

## Usage

### Basic Feature Extraction

```sql
-- Extract all 117 features per series
SELECT * FROM ts_features_by('sales', product_id, date, value, MAP{});
```

### Accessing Specific Features

```sql
-- Access individual features from the result
SELECT
    id,
    ROUND((features).mean, 2) AS mean,
    ROUND((features).variance, 2) AS variance,
    ROUND((features).skewness, 4) AS skewness,
    ROUND((features).kurtosis, 4) AS kurtosis
FROM ts_features_by('sales', product_id, date, value, MAP{});
```

### Feature Selection (Efficiency)

```sql
-- Extract only specific features (faster)
SELECT
    id,
    features
FROM ts_features_by('sales', product_id, date, value,
    MAP{'features': '["mean", "variance", "skewness", "kurtosis"]'});
```

### Trend and Autocorrelation Features

```sql
SELECT
    id,
    ROUND((features).linear_trend_slope, 4) AS trend_slope,
    ROUND((features).linear_trend_r_squared, 4) AS r_squared,
    ROUND((features).autocorrelation_lag1, 4) AS ac_lag1,
    ROUND((features).autocorrelation_lag7, 4) AS ac_lag7
FROM ts_features_by('sales', product_id, date, value, MAP{});
```

---

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `features` | VARCHAR | all | JSON array of feature names, e.g., `'["mean", "variance"]'` |

When `MAP{}` is passed (empty), all 117 features are extracted.

---

## Output Columns

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `features` | STRUCT | Struct containing all extracted features |

---

## Feature Categories

The extension extracts **117 tsfresh-compatible features** organized into categories:

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

## Key Concepts

### Accessing Features

Features are returned as a STRUCT. Access with parentheses and dot notation:

```sql
-- Single feature
SELECT (features).mean FROM ts_features_by('sales', product_id, date, value, MAP{});

-- Multiple features
SELECT
    id,
    (features).mean,
    (features).variance,
    (features).skewness
FROM ts_features_by('sales', product_id, date, value, MAP{});
```

### Feature Selection for Efficiency

Extracting all 117 features can be computationally expensive. For better performance, specify only the features you need:

```sql
-- Extract only needed features
SELECT * FROM ts_features_by('sales', product_id, date, value,
    MAP{'features': '["mean", "variance", "linear_trend_slope"]'});
```

### Feature-Based Classification

Use features to classify or compare time series:

```sql
WITH feature_data AS (
    SELECT
        id,
        (features).linear_trend_slope AS trend,
        (features).variance AS variance,
        (features).autocorrelation_lag7 AS ac7
    FROM ts_features_by('sales', product_id, date, value, MAP{})
)
SELECT
    id,
    CASE
        WHEN trend > 1.0 THEN 'trending'
        WHEN ac7 > 0.5 THEN 'seasonal'
        WHEN variance > 400 THEN 'volatile'
        ELSE 'stable'
    END AS pattern_type
FROM feature_data;
```

---

## Tips

1. **Start with all features** - Use `MAP{}` first to explore, then select specific features.

2. **Select features for production** - Extract only needed features to reduce computation.

3. **Numeric stability** - Some features (entropy) may return NULL for constant series.

4. **Series length** - Most features require at least 10 data points.

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
SELECT * FROM ts_features_by('sales', product_id, date, value,
    MAP{'features': '["mean", "variance", "skewness"]'});
```

### Q: Which features are most useful?

**A:** For forecasting: `autocorrelation_lag1`, `linear_trend_slope`, `seasonal_strength`
For classification: `mean`, `variance`, `skewness`, `kurtosis`, `entropy`

---

## Related Functions

- `ts_detect_periods_by()` - Detect seasonal periods
- `ts_classify_seasonality_by()` - Classify seasonality strength
- `ts_mstl_decomposition_by()` - Decompose into trend/seasonal/remainder
