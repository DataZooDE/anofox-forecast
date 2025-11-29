# Time Series Features Guide

## Introduction

The `ts_features` function extracts time series features directly in DuckDB, providing tsfresh-compatible feature vectors for machine learning, anomaly detection, and exploratory data analysis.

**Key Capabilities**:

- Extract 76+ statistical features from time series data
- Support for GROUP BY aggregations and rolling window functions
- Flexible feature selection and parameter customization
- Configuration via JSON/CSV files or inline parameters

---

## Table of Contents

1. [Basic Usage](#basic-usage)
   - [Simple Feature Extraction](#simple-feature-extraction)
   - [Listing Available Features](#listing-available-features)
2. [Feature Selection](#feature-selection)
   - [Select Specific Features](#select-specific-features)
   - [Multiple Features](#multiple-features)
3. [Parameter Customization](#parameter-customization)
   - [Inline Parameter Overrides](#inline-parameter-overrides)
   - [Parameterized Feature Columns](#parameterized-feature-columns)
   - [Multiple Parameter Variants](#multiple-parameter-variants)
4. [Configuration Files](#configuration-files)
   - [JSON Configuration](#json-configuration)
   - [CSV Configuration](#csv-configuration)
   - [Configuration Template](#configuration-template)
5. [Rolling Window Features](#rolling-window-features)
   - [Basic Rolling Window](#basic-rolling-window)
   - [Common Window Patterns](#common-window-patterns)
   - [Multiple Rolling Features](#multiple-rolling-features)
   - [Rolling Features with Parameters](#rolling-features-with-parameters)
6. [Grouping and Aggregation](#grouping-and-aggregation)
   - [GROUP BY Aggregation](#group-by-aggregation)
   - [Multiple Groups](#multiple-groups)
7. [Best Practices](#best-practices)
   - [Performance Optimization](#performance-optimization)
   - [Feature Selection Tips](#feature-selection-tips)
   - [Handling NULL Values](#handling-null-values)
8. [Common Patterns](#common-patterns)
   - [Feature Comparison Across Series](#feature-comparison-across-series)
   - [Rolling Feature Trends](#rolling-feature-trends)
9. [Troubleshooting](#troubleshooting)

---

---

## Basic Usage

### Simple Feature Extraction

Compute all default features for a time series:

```sql
-- Compute all default features for a time series
CREATE TABLE sample_ts AS
SELECT 
    (TIMESTAMP '2024-01-01' + i * INTERVAL '1 day') AS ts,
    (100 + i * 2 + SIN(i * 2 * PI() / 7) * 10)::DOUBLE AS value
FROM generate_series(0, 30) t(i);

SELECT feats.*
FROM (
    SELECT anofox_fcst_ts_features(ts, value) AS feats
    FROM sample_ts
);

```

The output is a STRUCT containing all available features. Use struct expansion (`feats.*`) to access all features as individual columns. If you only need specific features, explicitly specify them in the `feature_selection` parameter:

```sql
-- Extract only specific features
SELECT feats.*
FROM (
    SELECT ts_features(ts, value, ['mean', 'variance', 'length']) AS feats
    FROM sample_ts
);
```

### Listing Available Features

Use `ts_features_list()` to explore available features and their parameters:

```sql
-- List available features and their parameters
SELECT column_name, feature_name, default_parameters, parameter_keys
FROM anofox_fcst_ts_features_list()
ORDER BY column_name
LIMIT 10;

```

**Output Columns**:

- `column_name`: Full column name (includes parameter suffixes)
- `feature_name`: Base feature name
- `parameter_suffix`: Parameter suffix (e.g., `__lag_1`, `__r_2`)
- `default_parameters`: Default parameter values as JSON string
- `parameter_keys`: Available parameter keys for this feature

[↑ Go to top](#time-series-features-guide)

---

## Feature Selection

### Select Specific Features

Limit computation to specific features for better performance:

```sql
-- Create sample time series data
CREATE TABLE sample_ts AS
SELECT 
    TIMESTAMP '2024-01-01' + INTERVAL (d) DAY AS ts,
    100 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 30) t(d);

-- Select specific features for better performance
SELECT feats.*
FROM (
    SELECT anofox_fcst_ts_features(ts, value, ['mean', 'variance', 'length']) AS feats
    FROM sample_ts
);

```

### Multiple Features

Select multiple features at once:

```sql
SELECT 
    product_id,
    feats.*
FROM (
    SELECT 
        product_id,
        ts_features(
            ts,
            value,
            ['mean', 'variance', 'autocorrelation__lag_1', 'sum_values']
        ) AS feats
    FROM sales_data
    GROUP BY product_id
)
ORDER BY product_id;
```

[↑ Go to top](#time-series-features-guide)

---

## Parameter Customization

### Inline Parameter Overrides

Many features accept parameters. Override defaults using the `feature_params` argument:

```sql
-- Create sample time series data
CREATE TABLE sample_ts AS
SELECT 
    TIMESTAMP '2024-01-01' + INTERVAL (d) DAY AS ts,
    100 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 30) t(d);

-- Override default parameters for a feature
SELECT feats.*
FROM (
    SELECT anofox_fcst_ts_features(
        ts,
        value,
        ['ratio_beyond_r_sigma'],
        [{'feature': 'ratio_beyond_r_sigma', 'params': {'r': 1.0}}]
    ) AS feats
    FROM sample_ts
);

```

**Parameter Format**: `LIST(STRUCT(feature VARCHAR, params MAP(VARCHAR, ANY)))`

### Parameterized Feature Columns

When you specify parameters, the output column names include parameter suffixes:

```sql
-- Without parameters: column name is 'ratio_beyond_r_sigma'
-- With r=1.0: column name is 'ratio_beyond_r_sigma__r_1'
-- With r=2.0: column name is 'ratio_beyond_r_sigma__r_2'
```

### Multiple Parameter Variants

Compute the same feature with different parameters:

```sql
SELECT feats.*
FROM (
    SELECT ts_features(
        ts,
        value,
        ['autocorrelation'],
        [
            {'feature': 'autocorrelation', 'params': {'lag': 1}},
            {'feature': 'autocorrelation', 'params': {'lag': 3}},
            {'feature': 'autocorrelation', 'params': {'lag': 6}}
        ]
    ) AS feats
    FROM time_series_data
);
```

This produces columns: `autocorrelation__lag_1`, `autocorrelation__lag_3`, `autocorrelation__lag_6`.

[↑ Go to top](#time-series-features-guide)

---

## Configuration Files

### JSON Configuration

Load feature configuration from a JSON file:

```sql
-- Create sample time series data
CREATE TABLE sample_ts AS
SELECT 
    TIMESTAMP '2024-01-01' + INTERVAL (d) DAY AS ts,
    100 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 30) t(d);

-- Load feature configuration from JSON file
SELECT feats.*
FROM (
    SELECT anofox_fcst_ts_features(
        ts,
        value,
        anofox_fcst_ts_features_config_from_json('benchmark/timeseries_features/data/features_overrides.json')
    ) AS feats
    FROM sample_ts
);

```

**JSON Format**:

```json
[
    {
        "feature": "autocorrelation",
        "params": {"lag": 2}
    },
    {
        "feature": "ratio_beyond_r_sigma",
        "params": {"r": 1.5}
    }
]
```

### CSV Configuration

Load configuration from CSV (table-friendly format):

```sql
-- Create sample time series data
CREATE TABLE sample_ts AS
SELECT 
    TIMESTAMP '2024-01-01' + INTERVAL (d) DAY AS ts,
    100 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 30) t(d);

-- Load feature configuration from CSV file
SELECT feats.*
FROM (
    SELECT anofox_fcst_ts_features(
        ts,
        value,
        anofox_fcst_ts_features_config_from_csv('benchmark/timeseries_features/data/features_overrides.csv')
    ) AS feats
    FROM sample_ts
);

```

**CSV Format**:

```csv
feature,lag,r
autocorrelation,2,
ratio_beyond_r_sigma,,1.5
```

The CSV format uses column headers where:

- `feature` column is required
- Other columns map to parameter names (e.g., `lag`, `r`, `window`)
- Empty cells are treated as NULL (use defaults)

### Configuration Template

Generate a template configuration file:

```sql
SELECT feature, params_json
FROM ts_features_config_template()
ORDER BY feature;
```

Use this to create your own configuration files.

[↑ Go to top](#time-series-features-guide)

---

## Rolling Window Features

### Basic Rolling Window

Compute features over a rolling window using SQL window functions:

```sql
-- Compute rolling features using window functions
CREATE TABLE rolling_ts AS
SELECT 
    (i % 2) AS series_id,
    (TIMESTAMP '2024-01-01' + i * INTERVAL '1 day') AS ts,
    i::DOUBLE AS value
FROM generate_series(0, 10) t(i);

SELECT 
    series_id,
    ts,
    value,
    (anofox_fcst_ts_features(ts, value, ['mean', 'length']) OVER (
        PARTITION BY series_id 
        ORDER BY ts
        ROWS BETWEEN 2 PRECEDING AND CURRENT ROW
    )) AS rolling_stats
FROM rolling_ts
ORDER BY series_id, ts;

```

**Key Points**:

- Use `OVER (PARTITION BY ... ORDER BY ... ROWS BETWEEN ...)`
- Window frame defines the rolling window size
- Features are computed on the data within each window frame
- Some features require minimum sample sizes (may return NULL for small windows)

### Common Window Patterns

**Fixed-size rolling window** (e.g., last 7 days):

```sql
SELECT 
    date,
    value,
    (ts_features(date, value, ['mean']) OVER (
        PARTITION BY series_id 
        ORDER BY date
        ROWS BETWEEN 6 PRECEDING AND CURRENT ROW
    )).mean AS rolling_7day_mean
FROM time_series_data;
```

**Expanding window** (all data up to current row):

```sql
SELECT 
    date,
    value,
    (ts_features(date, value, ['variance']) OVER (
        PARTITION BY series_id 
        ORDER BY date
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    )).variance AS expanding_variance
FROM time_series_data;
```

**Centered window** (e.g., ±3 days around current row):

```sql
SELECT 
    date,
    value,
    (ts_features(date, value, ['mean']) OVER (
        PARTITION BY series_id 
        ORDER BY date
        ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING
    )).mean AS centered_mean
FROM time_series_data;
```

### Multiple Rolling Features

Compute multiple features in the same rolling window:

```sql
-- Create sample time series data
CREATE TABLE time_series_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    100 + series_id * 10 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 50) t(d)
CROSS JOIN (VALUES (1), (2), (3)) series(series_id);

-- Compute multiple rolling features in the same window
SELECT 
    series_id,
    date,
    value,
    (anofox_fcst_ts_features(date, value, ['mean', 'variance', 'linear_trend']) OVER (
        PARTITION BY series_id 
        ORDER BY date
        ROWS BETWEEN 10 PRECEDING AND CURRENT ROW
    )) AS rolling_features
FROM time_series_data
ORDER BY series_id, date;

```

### Rolling Features with Parameters

Combine rolling windows with parameter overrides:

```sql
SELECT 
    date,
    value,
    (ts_features(
        date,
        value,
        ['autocorrelation'],
        [{'feature': 'autocorrelation', 'params': {'lag': 1}}]
    ) OVER (
        PARTITION BY series_id 
        ORDER BY date
        ROWS BETWEEN 10 PRECEDING AND CURRENT ROW
    )).autocorrelation__lag_1 AS rolling_autocorr
FROM time_series_data;
```

[↑ Go to top](#time-series-features-guide)

---

## Grouping and Aggregation

### GROUP BY Aggregation

Compute features per group (e.g., per product, per region):

```sql
-- Compute features per group
CREATE TABLE multi_series AS
SELECT 
    (i % 3) AS product_id,
    (TIMESTAMP '2024-01-01' + i * INTERVAL '1 day') AS ts,
    (100 + product_id * 10 + i)::DOUBLE AS value
FROM generate_series(0, 20) t(i);

SELECT 
    product_id,
    feats.*
FROM (
    SELECT 
        product_id,
        anofox_fcst_ts_features(ts, value, ['mean', 'variance', 'length']) AS feats
    FROM multi_series
    GROUP BY product_id
)
ORDER BY product_id;

```

### Multiple Groups

Group by multiple columns:

```sql
SELECT 
    region,
    category,
    feats.*
FROM (
    SELECT 
        region,
        category,
        ts_features(ts, value, ['mean', 'variance']) AS feats
    FROM sales_data
    GROUP BY region, category
);
```

[↑ Go to top](#time-series-features-guide)

---

## Best Practices

### Performance Optimization

1. **Select only needed features**: Computing all features can be expensive

   ```sql
   -- Good: Select specific features
   SELECT feats.*
   FROM (
       SELECT ts_features(ts, value, ['mean', 'variance']) AS feats
       FROM your_table
   );
   
   -- Avoid: Computing all features unnecessarily
   SELECT feats.*
   FROM (
       SELECT ts_features(ts, value) AS feats
       FROM your_table
   );
   ```

2. **Use appropriate window sizes**: Larger windows = more computation

   ```sql
   -- Good: Reasonable window size
   ROWS BETWEEN 7 PRECEDING AND CURRENT ROW
   
   -- Expensive: Very large windows
   ROWS BETWEEN 1000 PRECEDING AND CURRENT ROW
   ```

3. **Pre-filter data**: Reduce data volume before feature extraction

   ```sql
   -- Good: Filter first
   SELECT feats.*
   FROM (
       SELECT ts_features(ts, value) AS feats
       FROM large_table 
       WHERE date >= CURRENT_DATE - INTERVAL '30 days'
       GROUP BY series_id
   );
   ```

### Feature Selection Tips

1. **Start with defaults**: Use `ts_features_list()` to explore available features
2. **Domain-specific features**: Choose features relevant to your use case
   - Trend detection: `linear_trend`, `mean_abs_change`
   - Seasonality: `autocorrelation`, `fft_coefficient`
   - Outliers: `ratio_beyond_r_sigma`, `max`, `min`
3. **Parameter tuning**: Experiment with different parameter values for parameterized features

### Handling NULL Values

- Features return NULL if insufficient data (e.g., `length` requires ≥2 samples)
- Some features may return NULL for certain input characteristics
- Always check for NULL before using features in downstream calculations

[↑ Go to top](#time-series-features-guide)

---

## Common Patterns

### Feature Comparison Across Series

```sql
WITH features AS (
    SELECT 
        series_id,
        ts_features(ts, value, ['mean', 'variance']) AS feats
    FROM time_series_data
    GROUP BY series_id
)
SELECT 
    series_id,
    feats.mean,
    feats.variance,
    feats.variance / NULLIF(feats.mean, 0) AS coefficient_of_variation
FROM features
ORDER BY coefficient_of_variation DESC;
```

### Rolling Feature Trends

Track how features change over time:

```sql
SELECT 
    date,
    value,
    (ts_features(date, value, ['mean', 'variance']) OVER (
        ORDER BY date
        ROWS BETWEEN 7 PRECEDING AND CURRENT ROW
    )) AS rolling_7day,
    (ts_features(date, value, ['mean', 'variance']) OVER (
        ORDER BY date
        ROWS BETWEEN 30 PRECEDING AND CURRENT ROW
    )) AS rolling_30day
FROM time_series_data;
```

[↑ Go to top](#time-series-features-guide)

---

## Troubleshooting

### Error: "feature 'X' does not exist"

Use `ts_features_list()` to check available feature names:

```sql
SELECT column_name 
FROM ts_features_list() 
WHERE feature_name LIKE '%your_search%';
```

### Error: "feature 'X' specified more than once"

Remove duplicate feature names from your feature list.

### NULL Values in Features

- Check minimum sample requirements for features
- Verify window size is sufficient
- Some features may legitimately return NULL for certain data patterns

### Performance Issues

- Reduce feature count
- Use smaller window sizes
- Filter data before feature extraction
- Consider materializing intermediate results

[↑ Go to top](#time-series-features-guide)
