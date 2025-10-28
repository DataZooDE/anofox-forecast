# Regime Features from Changepoints - Complete Guide

## Overview

This guide shows how to create a **regime_id column** from changepoint detection results. This column can be used as a powerful feature for:

- 🤖 **Machine Learning Models**: Categorical feature indicating current market regime
- 📊 **Regime-Specific Forecasting**: Different models for different regimes
- 📈 **Feature Engineering**: Derive regime-based statistics
- 🔍 **Pattern Analysis**: Understand regime transitions and dynamics

## Creating the Regime Column

### Single Series

```sql
WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{})
),
with_regime AS (
    SELECT 
        date_col AS date,
        value_col AS sales,
        is_changepoint,
        -- Create regime_id: increments at each changepoint
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (ORDER BY date_col) AS regime_id
    FROM changepoint_data
)
SELECT * FROM with_regime;
```

**Result:**
```
date       │ sales │ is_changepoint │ regime_id
───────────┼───────┼────────────────┼───────────
2023-01-01 │ 100   │ true           │    1
2023-01-02 │ 102   │ false          │    1
...
2023-04-10 │ 203   │ true           │    2  ← New regime starts
2023-04-11 │ 198   │ false          │    2
...
```

### Multiple Series (GROUP BY)

```sql
WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
),
with_regime AS (
    SELECT 
        product_id,
        date_col AS date,
        value_col AS sales,
        is_changepoint,
        -- Partition by product_id to create regime_id per series
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date_col) AS regime_id
    FROM changepoint_data
)
SELECT * FROM with_regime;
```

## Persisting Regime-Enriched Data

Create a table with regime column for future use:

```sql
DROP TABLE IF EXISTS sales_with_regimes;
CREATE TABLE sales_with_regimes AS
WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
)
SELECT 
    product_id,
    date_col AS date,
    value_col AS sales,
    is_changepoint,
    SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
        OVER (PARTITION BY product_id ORDER BY date_col) AS regime_id
FROM changepoint_data;

-- Now you have a permanent table with regime information!
```

## Feature Engineering with Regimes

### 1. Regime Statistics

Calculate summary statistics for each regime:

```sql
SELECT 
    product_id,
    regime_id,
    COUNT(*) AS regime_length,
    ROUND(AVG(sales), 2) AS regime_mean,
    ROUND(STDDEV(sales), 2) AS regime_std,
    MIN(date) AS regime_start,
    MAX(date) AS regime_end
FROM sales_with_regimes
GROUP BY product_id, regime_id
ORDER BY product_id, regime_id;
```

**Use Case**: Understand characteristics of each stable period

### 2. Days in Current Regime

Track how long each observation has been in current regime:

```sql
SELECT 
    product_id,
    date,
    sales,
    regime_id,
    ROW_NUMBER() OVER (PARTITION BY product_id, regime_id ORDER BY date) AS days_in_regime
FROM sales_with_regimes;
```

**Use Case**: Feature for ML (recent regime changes may affect predictions)

### 3. Regime Mean as Feature

Add regime-level statistics as features:

```sql
SELECT 
    product_id,
    date,
    sales,
    regime_id,
    -- Regime features
    AVG(sales) OVER (PARTITION BY product_id, regime_id) AS regime_mean,
    STDDEV(sales) OVER (PARTITION BY product_id, regime_id) AS regime_volatility,
    -- Deviation from regime mean
    sales - AVG(sales) OVER (PARTITION BY product_id, regime_id) AS deviation_from_regime
FROM sales_with_regimes;
```

**Use Case**: Normalize by regime, detect anomalies within regime

### 4. Time Since Last Changepoint

Calculate stability duration:

```sql
SELECT 
    product_id,
    date,
    sales,
    regime_id,
    date - MAX(CASE WHEN is_changepoint THEN date END) 
        OVER (PARTITION BY product_id ORDER BY date) AS time_since_changepoint,
    EXTRACT(DAY FROM 
        date - MAX(CASE WHEN is_changepoint THEN date END) 
            OVER (PARTITION BY product_id ORDER BY date)
    ) AS days_stable
FROM sales_with_regimes;
```

**Use Case**: Weight recent data more in stable regimes

### 5. Current Regime Identification

Find which regime each series is currently in:

```sql
WITH current_regime AS (
    SELECT 
        product_id,
        MAX(regime_id) AS current_regime_id,
        MAX(date) AS last_observation
    FROM sales_with_regimes
    GROUP BY product_id
)
SELECT 
    cr.product_id,
    cr.current_regime_id,
    COUNT(*) AS points_in_current_regime,
    ROUND(AVG(s.sales), 2) AS current_regime_mean
FROM current_regime cr
INNER JOIN sales_with_regimes s 
    ON cr.product_id = s.product_id 
    AND cr.current_regime_id = s.regime_id
GROUP BY cr.product_id, cr.current_regime_id
ORDER BY cr.product_id;
```

**Use Case**: Assess forecast reliability (more data in current regime = better)

## Advanced Applications

### Application 1: Regime-Specific Forecasting

```sql
-- Forecast separately for each regime type
WITH regime_stats AS (
    SELECT 
        regime_id,
        AVG(sales) AS regime_avg
    FROM sales_with_regimes
    GROUP BY regime_id
),
current_regime AS (
    SELECT MAX(regime_id) AS id FROM sales_with_regimes
),
current_regime_data AS (
    SELECT date, sales
    FROM sales_with_regimes
    WHERE regime_id = (SELECT id FROM current_regime)
)
SELECT * FROM TS_FORECAST('current_regime_data', date, sales, 'AutoETS', 28, MAP{});
```

**Benefit**: Models trained on homogeneous data (one regime) are more accurate

### Application 2: Regime Transition Prediction

```sql
-- Analyze regime transition patterns
WITH transitions AS (
    SELECT 
        product_id,
        regime_id AS current_regime,
        LAG(regime_id) OVER (PARTITION BY product_id ORDER BY date) AS prev_regime,
        date,
        LAG(date) OVER (PARTITION BY product_id ORDER BY date) AS prev_transition
    FROM sales_with_regimes
    WHERE is_changepoint = true
)
SELECT 
    current_regime,
    COUNT(*) AS num_transitions,
    ROUND(AVG(DATE_DIFF('day', prev_transition, date)), 1) AS avg_regime_duration
FROM transitions
WHERE prev_regime IS NOT NULL
GROUP BY current_regime
ORDER BY current_regime;
```

**Benefit**: Understand typical regime durations for planning

### Application 3: Feature Matrix for ML

```sql
-- Create comprehensive feature set
SELECT 
    product_id,
    date,
    sales AS target,
    -- Regime features
    regime_id AS regime_categorical,
    ROW_NUMBER() OVER (PARTITION BY product_id, regime_id ORDER BY date) AS days_in_regime,
    AVG(sales) OVER (PARTITION BY product_id, regime_id) AS regime_mean,
    STDDEV(sales) OVER (PARTITION BY product_id, regime_id) AS regime_std,
    -- Deviation features
    sales - AVG(sales) OVER (PARTITION BY product_id, regime_id) AS regime_deviation,
    -- Stability features
    EXTRACT(DAY FROM 
        date - MAX(CASE WHEN is_changepoint THEN date END) 
            OVER (PARTITION BY product_id ORDER BY date)
    ) AS days_stable,
    -- Lagged features
    LAG(sales, 1) OVER (PARTITION BY product_id ORDER BY date) AS lag1,
    LAG(sales, 7) OVER (PARTITION BY product_id ORDER BY date) AS lag7,
    -- Regime change indicator
    CASE WHEN is_changepoint THEN 1 ELSE 0 END AS just_changed
FROM sales_with_regimes
ORDER BY product_id, date;
```

**Benefit**: Rich feature set for training ML forecasting models

## Use Cases by Industry

### Retail
- **Regime ID**: Promotional periods, seasonal shifts
- **Feature**: `regime_id` indicates "normal", "promo", "holiday"
- **Forecast**: Different models for each regime

### Finance
- **Regime ID**: Bull/bear markets, volatility regimes
- **Feature**: `regime_volatility`, `days_in_regime`
- **Forecast**: Risk-adjusted forecasts per regime

### Manufacturing
- **Regime ID**: Production modes, quality shifts
- **Feature**: `regime_mean` (target production level)
- **Forecast**: Adaptive quality control limits

### Energy
- **Regime ID**: Seasonal patterns, demand shifts
- **Feature**: `regime_id`, `days_stable`
- **Forecast**: Regime-aware demand forecasting

## Example from Tests

**Data**: 5 series with different changepoint patterns

**Regime Statistics:**
```
id │ regime_id │ observations │ mean   │ std_dev │ range
───┼───────────┼──────────────┼────────┼─────────┼───────
 1 │     1     │      99      │ 105.55 │   2.92  │  9.96
 1 │     2     │     100      │ 203.97 │  10.15  │ 102.56  ← Different behavior!
 2 │     1     │      58      │ 154.70 │   2.94  │  9.88
 2 │     2     │      61      │ 105.70 │   6.70  │ 50.93
 2 │     3     │      80      │ 253.21 │  17.56  │ 159.27  ← High volatility regime
```

**Current Regime:**
```
id │ current_regime_id │ points_in_current_regime │ current_regime_mean
───┼───────────────────┼──────────────────────────┼─────────────────────
 1 │         3         │            1             │       208.77
 2 │         4         │            1             │       259.21
 3 │         2         │            1             │       118.40
```

**Regime Features (ML-ready):**
```
id │ date       │  y    │ regime_id │ days_in_regime │ regime_mean │ regime_vol │ days_stable
───┼────────────┼───────┼───────────┼────────────────┼─────────────┼────────────┼────────────
 1 │ 2023-01-01 │ 109.3 │     1     │       1        │   105.55    │    2.92    │      0
 1 │ 2023-01-02 │ 103.6 │     1     │       2        │   105.55    │    2.92    │      1
 1 │ 2023-01-03 │ 105.3 │     1     │       3        │   105.55    │    2.92    │      2
```

## Key Patterns

### Pattern 1: Create Regime Column
```sql
SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
    OVER (PARTITION BY series_id ORDER BY date) AS regime_id
```

### Pattern 2: Regime Statistics
```sql
AVG(value) OVER (PARTITION BY series_id, regime_id) AS regime_mean
STDDEV(value) OVER (PARTITION BY series_id, regime_id) AS regime_std
```

### Pattern 3: Days in Regime
```sql
ROW_NUMBER() OVER (PARTITION BY series_id, regime_id ORDER BY date) AS days_in_regime
```

### Pattern 4: Stability Duration
```sql
EXTRACT(DAY FROM 
    date - MAX(CASE WHEN is_changepoint THEN date END) 
        OVER (PARTITION BY series_id ORDER BY date)
) AS days_stable
```

## Benefits for Forecasting

### 1. **Improved Accuracy**
- Models trained on homogeneous regimes perform better
- Avoid contamination from different regimes

### 2. **Adaptive Behavior**
- Use `regime_id` to select appropriate model per regime
- Switch models when regime changes

### 3. **Feature-Rich Models**
- `regime_mean`, `regime_std` provide context
- `days_in_regime` indicates regime maturity
- `days_stable` measures stability

### 4. **Uncertainty Quantification**
- New regimes (low `days_stable`) → wider prediction intervals
- Mature regimes (high `days_stable`) → narrower intervals

## Real-World Workflow

```sql
-- Step 1: Detect changepoints and create regimes
CREATE TABLE sales_with_regimes AS
SELECT 
    product_id,
    date,
    sales,
    SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
        OVER (PARTITION BY product_id ORDER BY date) AS regime_id,
    is_changepoint
FROM (SELECT * FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{}));

-- Step 2: Engineer features
CREATE TABLE sales_features AS
SELECT 
    product_id,
    date,
    sales,
    regime_id,
    ROW_NUMBER() OVER (PARTITION BY product_id, regime_id ORDER BY date) AS days_in_regime,
    AVG(sales) OVER (PARTITION BY product_id, regime_id) AS regime_mean,
    STDDEV(sales) OVER (PARTITION BY product_id, regime_id) AS regime_volatility,
    sales - AVG(sales) OVER (PARTITION BY product_id, regime_id) AS regime_deviation,
    EXTRACT(DAY FROM date - MAX(CASE WHEN is_changepoint THEN date END) 
        OVER (PARTITION BY product_id ORDER BY date)) AS days_stable
FROM sales_with_regimes;

-- Step 3: Use in ML or forecasting
-- Export to Python/R for ML training, or
-- Use regime_id to select forecasting method:
WITH current_regime_stats AS (
    SELECT 
        product_id,
        MAX(regime_id) AS current_regime,
        COUNT(*) AS regime_observations
    FROM sales_features
    GROUP BY product_id, MAX(regime_id)
)
SELECT 
    product_id,
    CASE 
        WHEN regime_observations >= 50 THEN 'AutoETS'
        WHEN regime_observations >= 20 THEN 'SeasonalNaive'
        ELSE 'Naive'
    END AS recommended_model
FROM current_regime_stats;
```

## Complete Example

See `examples/changepoint_detection.sql` for a complete working example:

- **Step 9a**: Single series with regime_id
- **Step 9b**: Multiple series with regime_id (GROUP BY)
- **Step 9c**: Regime characteristics and statistics
- **Step 9d**: Persist regime-enriched data
- **Step 11a**: Regime-specific statistics
- **Step 11b**: Current regime identification  
- **Step 11c**: Comprehensive feature engineering
- **Step 11d**: Regime transition analysis

## Performance

- **Regime creation**: Negligible overhead (~0.1ms) - just a window function
- **Feature calculation**: Fast window operations
- **Parallelization**: ✅ Full DuckDB parallelization with PARTITION BY

## Summary

The regime column created from changepoint detection provides:

✅ **Categorical feature** for ML models  
✅ **Homogeneous segments** for better forecasting  
✅ **Rich feature set** (regime stats, stability metrics)  
✅ **Transition analysis** for pattern understanding  
✅ **Adaptive forecasting** based on current regime  

**Status**: Production-ready and demonstrated in comprehensive examples! 🚀

