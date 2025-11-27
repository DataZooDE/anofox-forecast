# Performance Optimization Guide - Technical

## Overview

This guide covers performance optimization techniques for large-scale time series forecasting with anofox-forecast.

## Performance Characteristics

### DuckDB Parallelization

**Key Insight**: DuckDB automatically parallelizes GROUP BY operations!

```sql
-- Single series: Uses 1 core
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});

-- 1,000 series: Uses ALL available cores automatically!
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7});
```

**Scalability**:

- 4 cores: ~4x speedup
- 8 cores: ~7x speedup
- 16 cores: ~12x speedup
- 32 cores: ~20x speedup

## Optimization Techniques

### 1. Materialize Intermediate Results

**Slow** (re-computes stats every time):

```sql
-- Every query re-analyzes data
SELECT * FROM TS_STATS('sales', product_id, date, amount);
```

**Fast** (compute once, reuse):

```sql
-- Compute once
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, amount);

-- Reuse many times
SELECT * FROM sales_stats WHERE quality_score < 0.7;
SELECT * FROM TS_STATS_SUMMARY('sales_stats');
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);
```

### 2. Filter Early

**Slow** (processes all data then filters):

```sql
WITH forecasts AS (
    SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {...})
)
SELECT * FROM forecasts WHERE product_id IN ('P001', 'P002', 'P003');
```

**Fast** (filters before forecasting):

```sql
CREATE TEMP TABLE filtered_sales AS
SELECT * FROM sales WHERE product_id IN ('P001', 'P002', 'P003');

SELECT * FROM TS_FORECAST_BY('filtered_sales', product_id, date, amount, 'AutoETS', 28, {...});
```

### 3. Disable Unused Features

```sql
-- Slower: Returns fitted values
{'return_insample': true, 'confidence_level': 0.95}

-- Faster: Only what you need
{'confidence_level': 0.90}  -- No fitted values (default)
```

### 4. Batch Processing Strategy

**For very large datasets** (100K+ series):

```sql
-- Process in batches
CREATE TABLE forecasts_batch1 AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id BETWEEN 'P000' AND 'P999'),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

CREATE TABLE forecasts_batch2 AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id BETWEEN 'P1000' AND 'P1999'),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- Combine
CREATE TABLE all_forecasts AS
SELECT * FROM forecasts_batch1
UNION ALL
SELECT * FROM forecasts_batch2;
```

## Memory Optimization

### Reduce Memory Footprint

```sql
-- Instead of keeping all forecast steps
CREATE TABLE forecasts_summary AS
SELECT 
    product_id,
    SUM(point_forecast) AS total_forecast,
    AVG(point_forecast) AS avg_forecast,
    MAX(point_forecast) AS peak_forecast
FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7})
GROUP BY product_id;

-- Don't store all intermediate forecast steps if not needed
```

## Query Optimization

### Use CTEs Effectively

**Good** (DuckDB optimizes CTEs):

```sql
WITH prepared AS (
    SELECT * FROM TS_FILL_GAPS('sales', product_id, date, amount)
),
cleaned AS (
    SELECT * FROM TS_DROP_CONSTANT('prepared', product_id, amount)
),
forecasts AS (
    SELECT * FROM TS_FORECAST_BY('cleaned', product_id, date, amount, 'AutoETS', 28, {...})
)
SELECT * FROM forecasts;
```

**Also Good** (Materialize for reuse):

```sql
CREATE TABLE sales_prepared AS
SELECT * FROM TS_FILL_GAPS('sales', product_id, date, amount);

CREATE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY('sales_prepared', product_id, date, amount, 'AutoETS', 28, {...});
```

### Avoid Subqueries in GROUP BY

**Slow**:

```sql
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE category = 'Electronics'),
    product_id, date, amount, 'AutoETS', 28, {...}
);
```

**Fast**:

```sql
CREATE TEMP TABLE electronics AS
SELECT * FROM sales WHERE category = 'Electronics';

SELECT * FROM TS_FORECAST_BY('electronics', product_id, date, amount, 'AutoETS', 28, {...});
```

## Monitoring & Profiling

### Measure Query Performance

```sql
-- Enable timing
.timer on

-- Run query
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {...});

-- Check execution time
-- Run Time: real 12.345 seconds
```

### Profile with EXPLAIN ANALYZE

```sql
EXPLAIN ANALYZE
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7});
```

## Best Practices

### 1. Profile First, Optimize Second

```sql
-- Measure current performance
.timer on
SELECT * FROM TS_FORECAST_BY(...);
-- Note: 15.2 seconds

-- Try optimization
-- Re-measure to confirm improvement
```

### 2. Use Appropriate Model for Scale

```
< 100 series → Any model
100-1K series → AutoETS, Theta, AutoARIMA
1K-10K series → AutoETS, SeasonalNaive
10K-100K series → SeasonalNaive, Theta
> 100K series → SeasonalNaive, consider batching
```

### 3. Optimize the Bottleneck

**Typical bottlenecks**:

1. Data loading (I/O)
2. Data preparation (gaps, nulls)
3. Model fitting (AutoETS, AutoARIMA)
4. Result materialization

### 5. Caching & Materialized Views

```sql
-- Cache preparation results
CREATE TABLE sales_prepared AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, amount);

-- Materialized view for stats (refresh daily)
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, amount);

-- Use throughout the day without recomputation
SELECT * FROM sales_stats WHERE quality_score < 0.7;
```

## Advanced Optimizations

### 1. Parallel Processing Control

DuckDB automatically uses available cores. To limit:

```sql
-- Limit threads (if running alongside other workloads)
SET threads TO 8;

-- Reset to default (all cores)
RESET threads;
```

### 2. Memory Limit

```sql
-- Set memory limit
SET memory_limit = '16GB';

-- Check current usage
SELECT * FROM duckdb_memory();
```

### 3. Optimize Data Layout

```sql
-- Store frequently-accessed columns together
CREATE TABLE sales_optimized AS
SELECT 
    product_id,
    date,
    amount
FROM sales
ORDER BY product_id, date;  -- Sorted for better compression
```

### 4. Columnar Storage Benefits

DuckDB's columnar storage means:

- Reading only needed columns is very fast
- Compression reduces I/O
- Vectorized operations (SIMD)

```sql
-- Fast: Only reads 3 columns
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, ...);

-- Slower: Reads all columns first
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales_with_100_columns),  -- Reads everything
    product_id, date, amount, ...
);
```

## Scaling Patterns

### Pattern 1: Vertical Scaling (More Cores)

**Single machine with more CPUs**

```sql
-- Automatically uses all cores
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {...});
```

**Pros**: Simple, no code changes
**Cons**: Limited by single machine capacity

### Pattern 2: Horizontal Partitioning

**Multiple machines processing different subsets**

```sql
-- Machine 1: Products A-M
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id < 'M'),
    product_id, date, amount, 'AutoETS', 28, {...}
);

-- Machine 2: Products N-Z
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id >= 'M'),
    product_id, date, amount, 'AutoETS', 28, {...}
);

-- Combine results
```

**Pros**: Linear scaling
**Cons**: Need orchestration layer

## Monitoring

### Track Query Performance

```sql
-- Create performance log
CREATE TABLE forecast_performance_log (
    run_date DATE,
    num_series INT,
    model_used VARCHAR,
    execution_time_sec DOUBLE,
    memory_mb DOUBLE
);

-- Log each run
INSERT INTO forecast_performance_log
SELECT 
    CURRENT_DATE,
    (SELECT COUNT(DISTINCT product_id) FROM sales),
    'AutoETS',
    15.7,  -- Measured execution time
    2048   -- Measured memory
);

-- Monitor trends
SELECT 
    run_date,
    execution_time_sec,
    num_series,
    ROUND(execution_time_sec / num_series * 1000, 2) AS ms_per_series
FROM forecast_performance_log
WHERE run_date >= CURRENT_DATE - INTERVAL '30 days'
ORDER BY run_date;
```

## Troubleshooting Slow Queries

### Diagnostic Checklist

1. **Check data size**:

```sql
SELECT 
    COUNT(DISTINCT product_id) AS num_series,
    COUNT(*) AS total_rows,
    COUNT(*) / COUNT(DISTINCT product_id) AS avg_series_length
FROM sales;
```

2. **Check model**:

```sql
-- Is AutoARIMA or AutoTBATS slowing you down?
-- Try AutoETS or Theta instead
```

3. **Check preparation overhead**:

```sql
-- Time each step
.timer on
CREATE TABLE step1 AS SELECT * FROM TS_FILL_GAPS(...);
-- Note time
CREATE TABLE step2 AS SELECT * FROM TS_DROP_CONSTANT('step1', ...);
-- Note time
```

4. **Check parallelization**:

```sql
-- Verify cores being used
SELECT * FROM duckdb_settings() WHERE name = 'threads';
```

5. **Check memory**:

```sql
-- Is it swapping?
SELECT * FROM duckdb_memory();
```

## Performance Tuning Checklist

- [ ] Use fastest model appropriate for accuracy needs
- [ ] Filter data before forecasting
- [ ] Materialize intermediate results
- [ ] Use batching for very large datasets (>100K series)
- [ ] Disable `return_insample` if not needed
- [ ] Cache preparation results (TS_STATS, TS_FILL_GAPS)
- [ ] Optimize data layout (sorted, columnar)
- [ ] Monitor and log performance
- [ ] Use appropriate hardware (sufficient RAM, multiple cores)
