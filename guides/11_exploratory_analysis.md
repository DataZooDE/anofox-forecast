# EDA & Data Preparation - Complete Workflow Guide

## Introduction

Data quality directly impacts forecast accuracy. This guide covers exploratory data analysis and preparation using SQL macros that operate on time series at scale.

**API Coverage**: 5 EDA macros + 2 Data Quality Health Card macros + 12 data preparation macros for comprehensive data quality workflows.

## Complete Workflow

### Phase 1: Explore Your Data (EDA)

#### Step 1: Generate Statistics

```sql
-- Compute comprehensive stats for all series
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount);

-- View results
SELECT * FROM sales_stats LIMIT 5;
```

**Output Schema**:
Returns comprehensive statistics per series including:

- **Basic stats**: count, mean, std, min, max, median
- **Data quality**: null_count, gap_count, zero_count, constant_flag
- **Pattern indicators**: cv (coefficient of variation), intermittency_rate
- **Trend metrics**: trend_correlation, first_last_ratio

#### Step 2: Dataset Summary

```sql
-- Get overall picture
SELECT * FROM TS_DATASET_SUMMARY('sales_stats');
```

**Example Output**:

```
total_series: 1,000
total_observations: 365,000
avg_series_length: 365
avg_quality_score: 0.8234
high_quality_series: 856
low_quality_series: 23
```

#### Step 3: Data Quality Health Card

**New**: Data quality assessment with actionable recommendations:

```sql
-- Generate comprehensive health card (n_short parameter defaults to 30 if NULL)
CREATE TABLE health_card AS
SELECT * FROM TS_DATA_QUALITY_HEALTH_CARD('sales_raw', product_id, date, sales_amount, 30);

-- View all issues
SELECT * FROM health_card ORDER BY dimension, metric;
```

**Example Output**:

| unique_id | dimension    | metric           | value | value_pct |
|-----------|--------------|------------------|-------|-----------|
| Store_A   | Temporal     | timestamp_gaps   | 23    | 0.152     |
| Store_A   | Magnitude   | missing_values   | 13    | 0.085     |
| Store_B   | Temporal     | series_length    | 5     | NULL      |
| Store_C   | Behavioural  | intermittency    | 104   | 0.523     |

**Four Dimensions Assessed**:

1. **Structural**:

- Key uniqueness and
- ID cardinality

2. **Temporal**:

- Frequency inference,
- Timestamp gaps,
- Series alignment, and
- Series length

3. **Magnitude**:

- Missing values,
- Value bounds, and
- Static values

4. **Behavioural**:

- Seasonality,
- Trend detection, and
- Intermittency

**Summary Function**:

```sql
-- Get summary by dimension (n_short parameter defaults to 30 if NULL)
SELECT * FROM TS_DATA_QUALITY_SUMMARY('sales_raw', product_id, date, sales_amount, 30);
```

#### Step 4: Identify Problems

```sql
-- Find series with quality_score < 0.7
SELECT * FROM TS_GET_PROBLEMATIC('sales_stats', 0.7);
```

**Common Issues**:

- Many gaps → primary_issue = '⚠️ Many gaps'
- Null values → primary_issue = '⚠️ Missing values'
- Constant → primary_issue = '⚠️ Constant'

#### Step 5: Detect Patterns

```sql
-- Seasonality
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_raw', product_id, date, sales_amount);

-- Changepoints (regime changes)
SELECT * FROM TS_DETECT_CHANGEPOINTS_BY('sales_raw', product_id, date, sales_amount,
                                         {'include_probabilities': true});
```

### Phase 2: Prepare Your Data

#### Standard Pipeline (Recommended)

```sql
-- All-in-one preparation (if standard pipeline was implemented)
CREATE TABLE sales_prepared AS
WITH 
-- Step 1: Fill time gaps
step1 AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
-- Step 2: Drop constant series
step2 AS (
    SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount)
),
-- Step 3: Drop short series
step3 AS (
    SELECT * FROM TS_DROP_SHORT('step2', product_id, date, 30)
),
-- Step 4: Remove leading zeros
step4 AS (
    SELECT * FROM TS_DROP_LEADING_ZEROS('step3', product_id, date, sales_amount)
),
-- Step 5: Fill remaining nulls
step5 AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('step4', product_id, date, sales_amount)
)
SELECT * FROM step5;
```

#### Custom Pipeline (Advanced)

Tailor to your specific needs:

```sql
CREATE TABLE sales_custom_prep AS
WITH 
-- Fill gaps first
filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
-- Drop problematic series
filtered AS (
    SELECT f.*
    FROM filled f
    WHERE f.product_id NOT IN (
        SELECT series_id FROM TS_GET_PROBLEMATIC('sales_stats', 0.5)
    )
),
-- Remove edge zeros
no_edges AS (
    SELECT * FROM TS_DROP_EDGE_ZEROS('filtered', product_id, date, sales_amount)
),
-- Fill nulls with interpolation (more sophisticated)
interpolated AS (
    SELECT 
        product_id,
        date,
        -- Linear interpolation
        COALESCE(sales_amount,
            AVG(sales_amount) OVER (
                PARTITION BY product_id 
                ORDER BY date 
                ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING
            )
        ) AS sales_amount
    FROM no_edges
)
SELECT * FROM interpolated;
```

### Phase 3: Validate Preparation

#### Compare Before/After

```sql
-- Generate stats for prepared data
CREATE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount);

-- Compare quality
SELECT 
    'Raw data' AS stage,
    COUNT(*) AS num_series,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls,
    SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) AS series_with_gaps,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series
FROM sales_stats
UNION ALL
SELECT 
    'Prepared',
    COUNT(*),
    ROUND(AVG(quality_score), 4),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END)
FROM prepared_stats;
```

**Expected Improvements**:

- Series with nulls: 45 → 0
- Series with gaps: 150 → 0
- Constant series: 23 → 0

## Common Data Issues & Solutions

### Issue 1: Missing Time Points

**Problem**: Dates are not continuous

```sql
-- Detect
SELECT series_id, n_gaps, quality_score
FROM sales_stats
WHERE n_gaps > 0
ORDER BY n_gaps DESC
LIMIT 10;

-- Fix
CREATE TABLE fixed AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount);
```

### Issue 2: Missing Values (NULLs)

**Problem**: Some values are NULL

**Solutions**:

```sql
-- Option A: Forward fill (use last known value)
SELECT * FROM TS_FILL_NULLS_FORWARD('sales', product_id, date, sales_amount);

-- Option B: Mean imputation
SELECT * FROM TS_FILL_NULLS_MEAN('sales', product_id, date, sales_amount);

-- Option C: Drop series with too many nulls
WITH clean AS (
    SELECT * FROM sales_stats WHERE n_null < length * 0.05  -- < 5% missing
)
SELECT s.*
FROM sales s
WHERE s.product_id IN (SELECT series_id FROM clean);
```

### Issue 3: Constant Series

**Problem**: All values are the same

```sql
-- Detect
SELECT * FROM sales_stats WHERE is_constant = true;

-- Fix
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, sales_amount);
```

### Issue 4: Short Series

**Problem**: Not enough historical data

```sql
-- Detect
SELECT * FROM sales_stats WHERE length < 30;

-- Fix: Drop or aggregate
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);

-- Or: Aggregate similar products
WITH aggregated AS (
    SELECT 
        category AS product_id,  -- Aggregate by category
        date,
        SUM(sales_amount) AS sales_amount
    FROM sales
    JOIN product_catalog USING (product_id)
    GROUP BY category, date
)
SELECT * FROM aggregated;
```

### Issue 5: Leading/Trailing Zeros

**Problem**: Product not yet launched or discontinued

```sql
-- Detect
WITH zero_analysis AS (
    SELECT 
        product_id,
        date,
        sales_amount,
        ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY date) AS rn,
        SUM(CASE WHEN sales_amount != 0 THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date) AS nonzero_count
    FROM sales
)
SELECT 
    product_id,
    MIN(CASE WHEN sales_amount != 0 THEN date END) AS first_sale,
    MAX(CASE WHEN sales_amount != 0 THEN date END) AS last_sale,
    COUNT(*) AS total_days,
    SUM(CASE WHEN sales_amount = 0 THEN 1 ELSE 0 END) AS zero_days
FROM zero_analysis
GROUP BY product_id
HAVING zero_days > 0;

-- Fix: Remove edge zeros
SELECT * FROM TS_DROP_EDGE_ZEROS('sales', product_id, date, sales_amount);
```

### Issue 6: Outliers

**Problem**: Extreme values distorting the pattern

```sql
-- Detect outliers using IQR method
WITH series_bounds AS (
    SELECT 
        product_id,
        QUANTILE_CONT(sales_amount, 0.25) AS q1,
        QUANTILE_CONT(sales_amount, 0.75) AS q3,
        QUANTILE_CONT(sales_amount, 0.75) - QUANTILE_CONT(sales_amount, 0.25) AS iqr
    FROM sales
    WHERE sales_amount IS NOT NULL
    GROUP BY product_id
),
outliers AS (
    SELECT 
        s.product_id,
        s.date,
        s.sales_amount,
        CASE 
            WHEN s.sales_amount > b.q3 + 1.5 * b.iqr THEN 'Upper outlier'
            WHEN s.sales_amount < b.q1 - 1.5 * b.iqr THEN 'Lower outlier'
            ELSE 'Normal'
        END AS outlier_type
    FROM sales s
    JOIN series_bounds b ON s.product_id = b.product_id
)
SELECT product_id, COUNT(*) AS n_outliers
FROM outliers
WHERE outlier_type != 'Normal'
GROUP BY product_id
HAVING COUNT(*) > 0;

-- Fix: Cap outliers (keep them but reduce impact)
-- (Would use TS_CAP_OUTLIERS_IQR if it was in integrated macros)
WITH series_bounds AS (
    SELECT 
        product_id,
        QUANTILE_CONT(sales_amount, 0.25) AS q1,
        QUANTILE_CONT(sales_amount, 0.75) AS q3,
        (QUANTILE_CONT(sales_amount, 0.75) - QUANTILE_CONT(sales_amount, 0.25)) AS iqr
    FROM sales
    WHERE sales_amount IS NOT NULL
    GROUP BY product_id
)
SELECT 
    s.product_id,
    s.date,
    CASE 
        WHEN s.sales_amount > b.q3 + 1.5 * b.iqr THEN b.q3 + 1.5 * b.iqr
        WHEN s.sales_amount < b.q1 - 1.5 * b.iqr THEN b.q1 - 1.5 * b.iqr
        ELSE s.sales_amount
    END AS sales_amount
FROM sales s
JOIN series_bounds b ON s.product_id = b.product_id;
```

### Issue 7: Different End Dates

**Problem**: Series end on different dates

```sql
-- Detect
WITH end_dates AS (
    SELECT 
        MAX(end_date) AS latest_date,
        COUNT(DISTINCT end_date) AS n_different_ends
    FROM sales_stats
)
SELECT * FROM end_dates;

-- Fix: Extend all series to common date
CREATE TABLE sales_aligned AS
SELECT * FROM TS_FILL_FORWARD(
    'sales',
    product_id,
    date,
    sales_amount,
    (SELECT MAX(date) FROM sales)  -- Latest date
);
```

## Advanced Preparation Techniques

## Data Quality Metrics

### Custom Quality Metrics

```sql
-- Define your own quality criteria
WITH custom_quality AS (
    SELECT 
        series_id,
        quality_score,  -- Built-in
        -- Custom: Penalize intermittency
        CASE 
            WHEN intermittency > 0.5 THEN quality_score * 0.7
            ELSE quality_score
        END AS adjusted_quality,
        -- Custom: Require minimum length
        CASE 
            WHEN length < 60 THEN 0.0
            ELSE quality_score
        END AS length_adjusted_quality
    FROM sales_stats
)
SELECT 
    series_id,
    ROUND(quality_score, 4) AS original_quality,
    ROUND(adjusted_quality, 4) AS intermittency_adjusted,
    ROUND(length_adjusted_quality, 4) AS length_adjusted
FROM custom_quality
ORDER BY original_quality DESC;
```

## Preparation Checklist

### Before Forecasting

- [ ] Check data quality: `TS_STATS()`, `TS_DATA_QUALITY_HEALTH_CARD()`
- [ ] Fill time gaps: `TS_FILL_GAPS()`
- [ ] Fill up to end date: `TS_FILL_FORWARD()`
- [ ] Handle missing values: `TS_FILL_NULLS_*()`
- [ ] Remove constant series: `TS_DROP_CONSTANT()`
- [ ] Check minimum length: `TS_DROP_SHORT()`
- [ ] Remove leading zeros: `TS_DROP_LEADING_ZEROS()`
- [ ] Detect seasonality: `TS_DETECT_SEASONALITY_ALL()`
- [ ] Detect changepoints: `TS_DETECT_CHANGEPOINTS_BY()`
- [ ] Remove edge zeros: `TS_DROP_EDGE_ZEROS()` (if applicable)
- [ ] Validate: Re-run `TS_STATS()` on prepared data

### Quality Gates

Define minimum standards:

```sql
-- Only forecast high-quality series
WITH quality_check AS (
    SELECT series_id
    FROM sales_stats
    WHERE quality_score >= 0.7        -- High quality
      AND length >= 60                -- Sufficient history
      AND n_unique_values > 5         -- Not near-constant
      AND intermittency < 0.30        -- Not too sparse
)
SELECT s.*
FROM sales_prepared s
WHERE s.product_id IN (SELECT series_id FROM quality_check);
```

## Automation

### Automated Data Prep Pipeline

```sql
-- Create a reusable preparation view
CREATE VIEW sales_autoprepared AS
WITH stats AS (
    SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount)
),
quality_series AS (
    SELECT series_id FROM stats WHERE quality_score >= 0.6
),
filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
    WHERE product_id IN (SELECT series_id FROM quality_series)
),
no_constant AS (
    SELECT * FROM TS_DROP_CONSTANT('filled', product_id, sales_amount)
),
complete AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('no_constant', product_id, date, sales_amount)
)
SELECT * FROM complete;

-- Use in forecasting
SELECT * FROM TS_FORECAST_BY('sales_autoprepared', product_id, date, sales_amount,
                             'AutoETS', 28, {'seasonal_period': 7});
```
