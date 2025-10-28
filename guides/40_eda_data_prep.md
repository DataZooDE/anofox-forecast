# EDA & Data Preparation - Complete Workflow Guide

## Introduction

**"Garbage in, garbage out"** - The quality of your forecasts depends on the quality of your data.

This guide covers the complete data preparation workflow using built-in SQL macros.

## Why Data Preparation Matters

### Impact on Forecast Accuracy

| Issue | Impact on MAPE | Solution |
|-------|----------------|----------|
| **Missing values (5%)** | +10-15% | Fill nulls |
| **Time gaps (10%)** | +15-20% | Fill gaps |
| **Constant series** | Model fails | Drop series |
| **Outliers (1%)** | +5-10% | Cap/remove |
| **Wrong seasonality** | +20-30% | Auto-detect |

**Bottom line**: Proper data prep can improve accuracy by 30-50%!

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

**Output includes**:
- 23 statistical features per series
- Quality score (0-1, higher is better)
- Gaps, nulls, zeros, patterns
- Trend correlation, CV, intermittency

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

#### Step 3: Quality Report

```sql
-- Comprehensive quality checks
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);
```

**Example Output**:
```
Gap Analysis:
  - 850 series with no gaps (85%)
  - 150 series with gaps (15%)
  - 2,450 total gaps

Missing Values:
  - 45 series with nulls (4.5%)
  - 892 total nulls (0.24% of data)

Constant Series:
  - 23 constant series (2.3%)

Short Series (< 30):
  - 67 series too short (6.7%)
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
- Quality score: 0.65 → 0.92
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

**Problem**: All values are the same (impossible to forecast)

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

### Technique 1: Seasonal Adjustment

```sql
-- Remove seasonality for trend analysis
WITH seasonality AS (
    SELECT * FROM TS_ANALYZE_SEASONALITY(
        LIST(date ORDER BY date),
        LIST(sales_amount ORDER BY date)
    )
    FROM sales
    WHERE product_id = 'P001'
)
-- Future: seasonal_component would be extracted
-- Current: Use models that handle seasonality (ETS, TBATS)
SELECT 'Use seasonal models like ETS or AutoETS' AS recommendation;
```

### Technique 2: Aggregation for Stability

```sql
-- Daily data too noisy? Aggregate to weekly
CREATE TABLE sales_weekly AS
SELECT 
    product_id,
    DATE_TRUNC('week', date) AS week,
    SUM(sales_amount) AS weekly_sales,
    AVG(sales_amount) AS avg_daily_sales,
    COUNT(*) AS days_in_week
FROM sales_daily
GROUP BY product_id, week;

-- Forecast on weekly data
SELECT * FROM TS_FORECAST_BY('sales_weekly', product_id, week, weekly_sales,
                             'AutoETS', 12, {'seasonal_period': 4});  -- 4 weeks = monthly
```

### Technique 3: Hierarchical Aggregation

```sql
-- Forecast at category level, then disaggregate
WITH category_forecast AS (
    SELECT 
        pc.category,
        date,
        SUM(sales_amount) AS category_sales
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    GROUP BY pc.category, date
),
category_predictions AS (
    SELECT * FROM TS_FORECAST_BY('category_forecast', category, date, category_sales,
                                 'AutoETS', 28, {'seasonal_period': 7})
),
product_proportions AS (
    SELECT 
        product_id,
        category,
        AVG(sales_amount) / SUM(sales_amount) OVER (PARTITION BY category) AS product_share
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    GROUP BY product_id, category
)
SELECT 
    pp.product_id,
    cp.date_col AS forecast_date,
    ROUND(cp.point_forecast * pp.product_share, 2) AS product_forecast
FROM category_predictions cp
JOIN product_proportions pp ON cp.category = pp.category;
```

## Real-World Scenarios

### Scenario 1: Messy Retail Data

```sql
-- Typical retail data issues
CREATE TABLE retail_prepared AS
WITH 
-- Step 1: Fill gaps (stores closed on some days)
filled AS (
    SELECT * FROM TS_FILL_GAPS('retail_raw', store_id || '_' || sku AS series_key, 
                               date, sales_qty)
),
-- Step 2: Separate back to store and SKU
parsed AS (
    SELECT 
        SPLIT_PART(series_key, '_', 1) AS store_id,
        SPLIT_PART(series_key, '_', 2) AS sku,
        date,
        sales_qty
    FROM filled
),
-- Step 3: Drop products with < 90 days history
sufficient_history AS (
    SELECT * FROM TS_DROP_SHORT('parsed', sku, date, 90)
),
-- Step 4: Fill nulls (missed scans)
filled_nulls AS (
    SELECT * FROM TS_FILL_NULLS_CONST('sufficient_history', sku, date, sales_qty, 0.0)
),
-- Step 5: Remove products with no recent sales
active_products AS (
    SELECT * FROM TS_DROP_TRAILING_ZEROS('filled_nulls', sku, date, sales_qty)
)
SELECT * FROM active_products;
```

### Scenario 2: Sensor/IoT Data

```sql
-- IoT sensor data with measurement errors
CREATE TABLE sensor_prepared AS
WITH 
-- Remove extreme outliers (sensor malfunction)
outliers_removed AS (
    SELECT 
        sensor_id,
        timestamp,
        CASE 
            WHEN measurement > 1000 OR measurement < -50 THEN NULL  -- Physically impossible
            ELSE measurement
        END AS measurement
    FROM sensor_raw
),
-- Interpolate gaps (linear)
interpolated AS (
    SELECT 
        sensor_id,
        timestamp,
        COALESCE(
            measurement,
            -- Linear interpolation between neighbors
            (LAST_VALUE(measurement IGNORE NULLS) 
                OVER (PARTITION BY sensor_id ORDER BY timestamp 
                      ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) +
             FIRST_VALUE(measurement IGNORE NULLS) 
                OVER (PARTITION BY sensor_id ORDER BY timestamp 
                      ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)) / 2.0
        ) AS measurement
    FROM outliers_removed
)
SELECT * FROM interpolated;
```

### Scenario 3: E-commerce with Promotions

```sql
-- Handle promotional spikes
CREATE TABLE ecommerce_prepared AS
WITH 
-- Identify promotion periods (changepoints)
changepoints AS (
    SELECT * FROM TS_DETECT_CHANGEPOINTS_BY('ecom_sales', product_id, date, order_count,
                                             {'include_probabilities': true})
    WHERE changepoint_probability > 0.95  -- High confidence
),
-- Create regime indicator
with_regimes AS (
    SELECT 
        product_id,
        date,
        order_count,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date) AS regime_id
    FROM changepoints
),
-- Compute regime statistics
regime_stats AS (
    SELECT 
        product_id,
        regime_id,
        AVG(order_count) AS regime_avg
    FROM with_regimes
    GROUP BY product_id, regime_id
),
-- Flag promotion periods
flagged AS (
    SELECT 
        w.*,
        CASE 
            WHEN rs.regime_avg > 
                 LAG(rs.regime_avg) OVER (PARTITION BY w.product_id ORDER BY rs.regime_id) * 1.3
            THEN true
            ELSE false
        END AS is_promo_period
    FROM with_regimes w
    JOIN regime_stats rs ON w.product_id = rs.product_id AND w.regime_id = rs.regime_id
)
SELECT product_id, date, order_count, is_promo_period
FROM flagged;

-- Forecast only on non-promo data for base demand
CREATE TABLE base_demand_forecast AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM ecommerce_prepared WHERE is_promo_period = false),
    product_id, date, order_count,
    'AutoETS', 30, {'seasonal_period': 7}
);
```

## Data Quality Metrics

### Define Quality Score

The built-in quality_score formula:

```
quality_score = 1.0 - (
    (n_null / length) * 0.4 +           -- Missing values (40% weight)
    (is_constant ? 0.3 : 0.0) +         -- Constant series (30% weight)
    (n_gaps / expected_length) * 0.3    -- Gaps (30% weight)
)
```

**Interpretation**:
- 1.0 = Perfect data
- 0.8-1.0 = High quality
- 0.5-0.8 = Moderate quality
- < 0.5 = Low quality (needs attention)

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

- [ ] Check data quality: `TS_STATS()`, `TS_QUALITY_REPORT()`
- [ ] Fill time gaps: `TS_FILL_GAPS()`
- [ ] Handle missing values: `TS_FILL_NULLS_*()`
- [ ] Remove constant series: `TS_DROP_CONSTANT()`
- [ ] Check minimum length: `TS_DROP_SHORT()`
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

## Summary

**Data Preparation Workflow**:
1. ✅ **Explore**: Use TS_STATS(), TS_QUALITY_REPORT()
2. ✅ **Identify**: Find gaps, nulls, outliers, patterns
3. ✅ **Clean**: Fill gaps, handle nulls, remove bad series
4. ✅ **Transform**: Remove edge zeros, cap outliers
5. ✅ **Validate**: Re-check quality scores
6. ✅ **Forecast**: Generate predictions on clean data

**Expected Outcome**:
- 30-50% improvement in forecast accuracy
- Fewer model failures
- More reliable confidence intervals
- Better business decisions

**Next Steps**:
- [Demand Forecasting Use Case](30_demand_forecasting.md)
- [Model Selection Guide](11_model_selection.md)
- [Statistical Guide](20_understanding_forecasts.md)

---

**Pro Tip**: Save your preparation pipeline as a VIEW for reusability!

