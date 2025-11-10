# Changepoint Detection API

## Overview

The `anofox-forecast` extension provides powerful changepoint detection capabilities using **Bayesian Online Changepoint Detection (BOCPD)**. Changepoints mark significant shifts in time series behavior such as:

- **Level shifts**: Mean value changes
- **Trend changes**: Slope changes
- **Variance changes**: Volatility shifts
- **Regime changes**: Different statistical behavior

## Functions

### TS_DETECT_CHANGEPOINTS

Detects changepoints in a single time series.

**Signature:**
```sql
TS_DETECT_CHANGEPOINTS(
    table_name: STRING,
    date_col: TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) -> TABLE(date_col TIMESTAMP, value_col DOUBLE, is_changepoint BOOLEAN)
```

**Parameters:**
- `table_name`: Name or subquery of the table containing data
- `date_col`: Column name for timestamps
- `value_col`: Column name for values
- `params`: MAP of optional parameters (use `MAP{}` for defaults)
  - `hazard_lambda`: Expected run length between changepoints (default: 250.0)
    - Lower values (e.g., 50) → more sensitive → more changepoints
    - Higher values (e.g., 500) → less sensitive → fewer changepoints
  - `include_probabilities`: Compute changepoint probabilities (default: false)
    - `false` → probabilities = 0.0 (faster, default)
    - `true` → compute actual probabilities from BOCPD

**Returns:**
Table with original data plus:
- `is_changepoint`: Boolean marking detected changepoints
- `changepoint_probability`: DOUBLE (0-1) confidence score
  - 0.0 when `include_probabilities=false` (default)
  - Actual Bayesian probability when `include_probabilities=true`

**Example:**
```sql
-- Detect with default parameters
SELECT *
FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{})
WHERE is_changepoint = true;

-- More sensitive detection
SELECT *
FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, {'hazard_lambda': 50.0})
WHERE is_changepoint = true;

-- With probabilities for confidence scoring
SELECT date_col, is_changepoint, ROUND(changepoint_probability, 4) AS confidence
FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, {'include_probabilities': true})
WHERE is_changepoint = true
ORDER BY changepoint_probability DESC;
```

### TS_DETECT_CHANGEPOINTS_BY

Detects changepoints for multiple time series using GROUP BY.

**Signature:**
```sql
TS_DETECT_CHANGEPOINTS_BY(
    table_name: STRING,
    group_col: ANY,
    date_col: TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) -> TABLE(group_col ANY, date_col TIMESTAMP, value_col DOUBLE, is_changepoint BOOLEAN)
```

**Parameters:**
- `table_name`: Name or subquery of the table containing data
- `group_col`: Column name for grouping (e.g., product_id, region)
- `date_col`: Column name for timestamps
- `value_col`: Column name for values
- `params`: MAP of optional parameters

**Returns:**
Table with original data grouped by `group_col` plus `is_changepoint` column.

**Example:**
```sql
-- Detect changepoints for each product
SELECT 
    group_col AS product_id,
    date_col AS date,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
WHERE is_changepoint = true
ORDER BY product_id, date;

-- Count changepoints per product
SELECT 
    group_col AS product_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
GROUP BY product_id
ORDER BY num_changepoints DESC;
```

## Algorithm: BOCPD

**Bayesian Online Changepoint Detection** is a probabilistic method that:

1. **Maintains Run Length Distribution**: Tracks probability of how long current segment has lasted
2. **Computes Predictive Probability**: Uses Normal-Gamma conjugate prior
3. **Detects Changepoints**: When run length distribution drops (new segment starts)
4. **Online/Streaming**: Processes data sequentially (no look-ahead)

### Key Characteristics

- **Probabilistic**: Accounts for uncertainty
- **Adaptive**: Automatically adjusts to data characteristics
- **No Pre-specification**: No need to specify number of changepoints
- **Robust**: Works with non-stationary data

## Parameters

### hazard_lambda

Controls sensitivity of changepoint detection.

**Interpretation**: Expected number of observations between changepoints.

| Value | Sensitivity | Use Case |
|-------|------------|----------|
| 10-50 | Very High | Detect subtle changes, high-frequency data |
| 100-200 | High | Moderate changes, daily data |
| **250** | **Default** | **General purpose, weekly/monthly data** |
| 500-1000 | Low | Only major shifts, noisy data |

### include_probabilities

Controls whether to compute Bayesian changepoint probabilities.

| Value | Behavior | Use Case |
|-------|----------|----------|
| **false** (default) | Probabilities = 0.0 | Faster, when you only need binary detection |
| true | Compute actual probabilities | When you need confidence scores for filtering/ranking |

**Examples:**
```sql
-- Highly sensitive: detect even small changes
{'hazard_lambda': 50.0}

-- Default: balanced detection
MAP{}  -- or {'hazard_lambda': 250.0}

-- Conservative: only major shifts
{'hazard_lambda': 500.0}
```

## Parallelization

✅ **Fully parallelized with DuckDB's GROUP BY**

When using `TS_DETECT_CHANGEPOINTS_BY`, DuckDB automatically distributes series across CPU cores:

```sql
-- This processes 10,000 series in parallel across all cores
SELECT 
    group_col AS series_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
FROM TS_DETECT_CHANGEPOINTS_BY('large_dataset', series_id, date, value, MAP{})
GROUP BY group_col;

-- Performance: ~10-20ms per series × (num_series / num_cores)
```

## Interpretation

### When is a Point Marked as Changepoint?

A point is marked as `is_changepoint = true` when:

1. The **run length distribution** shows highest probability at length 0
2. This indicates a **new segment just started**
3. The previous point was the **last point of the old segment**

### Understanding Changepoint Locations

```
Segment 1: 100 ± 5  (days 1-49)
Changepoint: Day 50 ← Level shift detected here
Segment 2: 200 ± 5  (days 51-100)
```

The changepoint marks the **beginning of the new regime**, not the exact transition point.

## Tuning Guidelines

### Choose hazard_lambda Based On:

**1. Data Frequency**
- Hourly data: 24-100 (1-4 days)
- Daily data: 30-250 (1-8 months)  
- Weekly data: 52-250 (1-5 years)
- Monthly data: 12-60 (1-5 years)

**2. Expected Change Frequency**
- Frequent changes (e.g., A/B tests): 50-100
- Occasional changes (e.g., promotions): 200-300
- Rare changes (e.g., rebranding): 500-1000

**3. Noise Level**
- High noise: Use higher values (500+)
- Low noise: Can use lower values (50-100)

### Validation Strategy

```sql
-- Step 1: Try default
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
WHERE is_changepoint = true;

-- Step 2: If too few, decrease hazard_lambda
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, {'hazard_lambda': 100.0})
WHERE is_changepoint = true;

-- Step 3: If too many, increase hazard_lambda
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, {'hazard_lambda': 500.0})
WHERE is_changepoint = true;
```

## Best Practices

1. **Start with Defaults**: Use `MAP{}` first, tune only if needed
2. **Visualize Results**: Plot data with changepoints to validate
3. **Consider Domain Knowledge**: Expected change frequency
4. **Use for Segmentation**: Analyze segments separately
5. **Adaptive Forecasting**: Use only recent stable data

## Common Patterns

### Pattern 1: Find All Changepoints

```sql
SELECT *
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
WHERE is_changepoint = true;
```

### Pattern 2: Count Changepoints

```sql
SELECT 
    COUNT(*) FILTER (WHERE is_changepoint) AS total_changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{});
```

### Pattern 3: Most Recent Changepoint

```sql
SELECT MAX(date_col) AS last_change
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
WHERE is_changepoint = true;
```

### Pattern 4: Segment Statistics

```sql
WITH cp AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
),
segments AS (
    SELECT 
        *,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (ORDER BY date_col) AS segment_id
    FROM cp
)
SELECT 
    segment_id,
    COUNT(*) AS length,
    AVG(value_col) AS avg_value
FROM segments
GROUP BY segment_id;
```

## Limitations

1. **Assumes Normal Distribution**: BOCPD uses Normal-Gamma prior (works well for most data)
2. **Sequential Processing**: Cannot detect changepoints in parallel within one series
3. **Memory**: O(1024) per series for run length tracking
4. **Fixed Prior**: Normal-Gamma parameters currently not configurable
