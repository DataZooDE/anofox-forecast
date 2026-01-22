# Decomposition Examples

> **Breaking down time series into trend, seasonal, and remainder components.**

This folder contains runnable SQL examples demonstrating time series decomposition with the anofox-forecast extension.

## Function

| Function | Description |
|----------|-------------|
| `ts_mstl_decomposition_by` | MSTL decomposition for multiple series |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_decomposition_examples.sql`](synthetic_decomposition_examples.sql) | Multi-series decomposition examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/decomposition/synthetic_decomposition_examples.sql
```

---

## Usage

### Basic Decomposition (Auto-detect Periods)

```sql
-- Decompose all series with automatic period detection
SELECT * FROM ts_mstl_decomposition_by('sales', product_id, date, value, MAP{});
```

### With Explicit Periods

```sql
-- Weekly decomposition
SELECT * FROM ts_mstl_decomposition_by('sales', product_id, date, value,
    MAP{'periods': '[7]'});

-- Multiple seasonal periods (weekly + monthly)
SELECT * FROM ts_mstl_decomposition_by('sales', product_id, date, value,
    MAP{'periods': '[7, 30]'});
```

### Extracting Components

```sql
-- Access individual components
SELECT
    id,
    trend[1:5] AS first_5_trend,
    seasonal[1:5] AS first_5_seasonal,
    remainder[1:5] AS first_5_remainder
FROM ts_mstl_decomposition_by('sales', product_id, date, value, MAP{});
```

### Computing Trend Statistics

```sql
WITH decomposed AS (
    SELECT * FROM ts_mstl_decomposition_by('sales', product_id, date, value, MAP{})
)
SELECT
    id,
    ROUND(list_avg(trend), 2) AS mean_trend,
    ROUND(trend[length(trend)] - trend[1], 2) AS trend_change,
    ROUND(list_stddev(remainder), 2) AS noise_level
FROM decomposed;
```

---

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `periods` | VARCHAR | auto | JSON array of periods, e.g., `'[7, 30]'` |

When `MAP{}` is passed (empty), periods are automatically detected.

---

## Output Columns

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `trend` | DOUBLE[] | Trend component |
| `seasonal` | DOUBLE[][] | Seasonal components (one per period) |
| `remainder` | DOUBLE[] | Residual component |
| `periods` | INTEGER[] | Periods used/detected |

---

## Key Concepts

### What MSTL Does

MSTL (Multiple Seasonal-Trend decomposition using Loess) separates your data into:

1. **Trend** - Long-term direction (growth, decline, stability)
2. **Seasonal** - Repeating patterns (daily, weekly, yearly cycles)
3. **Remainder** - What's left after removing trend and seasonality (noise)

### Original = Trend + Seasonal + Remainder

For additive decomposition: `value = trend + seasonal + remainder`

### Choosing Periods

| Data Frequency | Common Periods |
|----------------|----------------|
| Hourly | 24 (daily), 168 (weekly) |
| Daily | 7 (weekly), 30 (monthly), 365 (yearly) |
| Weekly | 52 (yearly) |
| Monthly | 12 (yearly) |

---

## Tips

1. **Let it auto-detect** - Use `MAP{}` first to see what periods are found automatically.

2. **Check remainder** - A good decomposition leaves only noise in the remainder. High remainder std = poor fit.

3. **Minimum data** - Need at least 2x the longest period for reliable decomposition.

4. **Use trend for forecasting** - The trend component shows the underlying direction, useful for planning.

5. **Detect seasonality strength** - Use `ts_classify_seasonality_by()` to check if decomposition makes sense.

---

## Related Functions

- `ts_detect_periods_by()` - Find seasonal periods automatically
- `ts_classify_seasonality_by()` - Check if series has meaningful seasonality
- `ts_forecast_by()` - Forecast using MSTL model
