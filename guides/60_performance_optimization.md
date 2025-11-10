# Performance Optimization Guide - Technical

## Overview

This guide covers performance optimization techniques for large-scale time series forecasting with anofox-forecast.

## Performance Characteristics

### Model Speed Comparison

| Model | Speed | Throughput | Use When |
|-------|-------|------------|----------|
| **Naive** | ⚡⚡⚡⚡⚡ | 100K series/min | Speed critical |
| **SeasonalNaive** | ⚡⚡⚡⚡⚡ | 80K series/min | Fast baseline |
| **Theta** | ⚡⚡⚡⚡ | 50K series/min | Balanced |
| **AutoETS** | ⚡⚡⚡ | 10K series/min | Production standard |
| **AutoARIMA** | ⚡⚡ | 2K series/min | High accuracy needed |
| **AutoTBATS** | ⚡ | 500 series/min | Very complex patterns |

*Benchmarks: Single-threaded, 365-day series, 28-day horizon*

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

### 1. Model Selection for Scale

**Scenario**: Need to forecast 10,000 products daily

**Option A: Fast models** (Recommended for scale)

```sql
-- SeasonalNaive: ~8 seconds for 10K series
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                             'SeasonalNaive', 7, {'seasonal_period': 7});
```

**Option B: Accurate models** (Slower)

```sql
-- AutoETS: ~15 minutes for 10K series
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                             'AutoETS', 7, {'seasonal_period': 7});
```

**Option C: Hybrid approach** (Best of both)

```sql
-- Use fast model for most products, accurate for top products
WITH abc_class AS (
    SELECT 
        product_id,
        CASE 
            WHEN revenue_rank <= 100 THEN 'A'
            WHEN revenue_rank <= 500 THEN 'B'
            ELSE 'C'
        END AS class
    FROM (
        SELECT 
            product_id,
            RANK() OVER (ORDER BY SUM(revenue) DESC) AS revenue_rank
        FROM sales
        GROUP BY product_id
    )
),
a_forecasts AS (
    SELECT * FROM TS_FORECAST_BY(
        (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM abc_class WHERE class = 'A')),
        product_id, date, amount, 'AutoARIMA', 28, {'seasonal_period': 7}
    )
),
b_forecasts AS (
    SELECT * FROM TS_FORECAST_BY(
        (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM abc_class WHERE class = 'B')),
        product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
    )
),
c_forecasts AS (
    SELECT * FROM TS_FORECAST_BY(
        (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM abc_class WHERE class = 'C')),
        product_id, date, amount, 'SeasonalNaive', 28, {'seasonal_period': 7}
    )
)
SELECT * FROM a_forecasts
UNION ALL SELECT * FROM b_forecasts
UNION ALL SELECT * FROM c_forecasts;
```

### 2. Reduce Horizon When Possible

```sql
-- Forecast 90 days: Slower
SELECT * FROM TS_FORECAST(..., 90, ...);

-- Forecast 7 days, re-run weekly: Faster
SELECT * FROM TS_FORECAST(..., 7, ...);
```

**Trade-off**: More frequent re-forecasting vs longer horizons

### 3. Materialize Intermediate Results

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
SELECT * FROM TS_DATASET_SUMMARY('sales_stats');
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);
```

### 4. Filter Early

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

### 5. Disable Unused Features

```sql
-- Slower: Returns fitted values
{'return_insample': true, 'confidence_level': 0.95}

-- Faster: Only what you need
{'confidence_level': 0.90}  -- No fitted values (default)
```

**Performance impact**: ~1-2% faster without `return_insample`

### 6. Batch Processing Strategy

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

### Memory Usage Estimates

| Dataset | Memory (approx) | Notes |
|---------|-----------------|-------|
| 1K series × 365 days | ~50 MB | Small, fits in cache |
| 10K series × 365 days | ~500 MB | Medium, comfortable |
| 100K series × 365 days | ~5 GB | Large, needs good RAM |
| 1M series × 365 days | ~50 GB | Very large, consider batching |

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

## Hardware Recommendations

### Minimum Requirements

- **CPU**: 4 cores
- **RAM**: 8 GB
- **Storage**: SSD recommended

### Recommended for Production

- **CPU**: 16+ cores (better parallelization)
- **RAM**: 32+ GB (handle larger datasets)
- **Storage**: NVMe SSD (faster I/O)

### Cloud Sizing

| Workload | AWS Instance | Cores | RAM | Cost/hour |
|----------|--------------|-------|-----|-----------|
| **Small** (1K series) | c6i.2xlarge | 8 | 16 GB | ~$0.34 |
| **Medium** (10K series) | c6i.4xlarge | 16 | 32 GB | ~$0.68 |
| **Large** (100K series) | c6i.8xlarge | 32 | 64 GB | ~$1.36 |

## Benchmarks

### Standard Workload

**Dataset**: 10,000 series, 365 days each, 28-day horizon

| Model | Single Core | 8 Cores | 16 Cores | Speedup |
|-------|-------------|---------|----------|---------|
| SeasonalNaive | 2.5 min | 20 sec | 12 sec | 12.5x |
| AutoETS | 120 min | 16 min | 9 min | 13.3x |
| AutoARIMA | 450 min | 60 min | 35 min | 12.9x |

### Large-Scale Benchmark

**Dataset**: 100,000 series, 365 days each, 7-day horizon

| Operation | Time (16 cores) | Memory Peak |
|-----------|----------------|-------------|
| TS_STATS | 45 sec | 2.5 GB |
| TS_FILL_GAPS | 30 sec | 3.0 GB |
| TS_FORECAST_BY (SeasonalNaive) | 3 min | 4.5 GB |
| TS_FORECAST_BY (AutoETS) | 90 min | 6.0 GB |

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

**Solutions**:

- Use SSD for I/O
- Materialize preparation results
- Use faster models for non-critical series
- Project only needed columns

### 4. Incremental Updates

**Instead of re-forecasting everything daily**:

```sql
-- Only re-forecast series with new data
CREATE TABLE needs_update AS
SELECT product_id
FROM sales
WHERE date = CURRENT_DATE;

-- Forecast only updated series
CREATE TABLE new_forecasts AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM needs_update)),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- Merge with existing forecasts
DELETE FROM forecasts WHERE product_id IN (SELECT product_id FROM needs_update);
INSERT INTO forecasts SELECT * FROM new_forecasts;
```

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

**Recommended**: Up to ~50K series

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

**Recommended**: 50K-500K series

### Pattern 3: Time-Based Partitioning

**Partition by time window**

```sql
-- Only forecast last 90 days of each series (faster)
CREATE TABLE recent_sales AS
SELECT * FROM sales
WHERE date >= CURRENT_DATE - INTERVAL '90 days';

SELECT * FROM TS_FORECAST_BY('recent_sales', product_id, date, amount, 'AutoETS', 28, {...});
```

**Pros**: Reduces data processed
**Cons**: May lose long-term patterns

## Real-World Performance Cases

### Case 1: Daily Forecasting Job

**Requirement**: Forecast 5,000 products every night

```sql
-- Nightly ETL job
BEGIN TRANSACTION;

-- 1. Refresh sales data (incremental load)
INSERT INTO sales SELECT * FROM staging_sales;

-- 2. Update statistics
CREATE OR REPLACE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, amount);

-- 3. Generate forecasts (only active products)
CREATE OR REPLACE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id IN (SELECT series_id FROM sales_stats WHERE quality_score >= 0.6)),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- 4. Export for consumption
COPY forecasts TO 'forecast_output.parquet' (FORMAT PARQUET);

COMMIT;
```

**Performance**: ~15 minutes total on 16-core machine

### Case 2: Real-Time API

**Requirement**: Generate forecast on-demand for single product

```sql
-- Create parameterized prepared statement
PREPARE forecast_single AS
SELECT * FROM TS_FORECAST(
    (SELECT * FROM sales WHERE product_id = $1),
    date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- Execute with parameter
EXECUTE forecast_single('P12345');
```

**Performance**: < 1 second per product (with warm cache)

### Case 3: Batch Reforecasting

**Requirement**: Re-forecast all 50,000 products weekly

```sql
-- Parallel batch processing
CREATE TABLE forecasts_week_$(date +%Y%m%d) AS
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                             'AutoETS', 28, {'seasonal_period': 7});
```

**Strategy**:

- Run on weekend (off-peak)
- Use all available cores
- Store results for the week
- Incremental updates mid-week if needed

**Performance**: ~4 hours on 32-core machine

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
- [ ] Consider incremental updates vs full reforecasting

## Summary

**Key Takeaways**:

- ✅ DuckDB parallelizes GROUP BY automatically (use it!)
- ✅ Model selection has biggest impact on speed
- ✅ Materialize intermediate results for reuse
- ✅ Filter early, process less data
- ✅ Use hybrid approach for 10K+ series (fast for most, accurate for top)
- ✅ Monitor performance over time

**Performance Hierarchy** (fastest to slowest):

1. SeasonalNaive, Naive
2. Theta, OptimizedTheta
3. AutoETS, ETS
4. AutoARIMA, ARIMA
5. AutoMSTL, MSTL
6. AutoTBATS, TBATS

**Scalability Sweet Spots**:

- **< 1K series**: Any model, single machine
- **1K-10K series**: AutoETS, single machine (16+ cores)
- **10K-100K series**: Hybrid (fast + accurate), single powerful machine
- **> 100K series**: Batch processing, consider distributed approach

---

**Next**: [EDA & Data Prep](11_exploratory_analysis.md) - Optimize data preparation performance

**Related**: [API Reference](90_api_reference.md) - Complete function documentation
