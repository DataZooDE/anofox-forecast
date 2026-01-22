# Seasonality Classification Examples

> **Seasonality classification identifies the type and stability of seasonal patterns in time series data.**

This folder contains runnable SQL examples demonstrating seasonality classification with the anofox-forecast extension.

## Function

| Function | Description |
|----------|-------------|
| `ts_classify_seasonality_by` | Classify seasonality type for multiple series |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_seasonality_examples.sql`](synthetic_seasonality_examples.sql) | Multi-series seasonality classification examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/seasonality_classification/synthetic_seasonality_examples.sql
```

---

## Usage

### Basic Classification

```sql
-- Classify seasonality with expected period=7 (weekly)
SELECT * FROM ts_classify_seasonality_by('sales', product_id, date, value, 7);
```

### Accessing Classification Results

```sql
-- Extract specific classification fields
SELECT
    id AS series_id,
    (classification).timing_classification AS timing_class,
    (classification).modulation_type AS modulation,
    (classification).is_seasonal AS is_seasonal,
    (classification).has_stable_timing AS stable_timing,
    ROUND((classification).seasonal_strength, 4) AS strength
FROM ts_classify_seasonality_by('sales', product_id, date, value, 7);
```

### Filter Seasonal Series

```sql
-- Only show series with detected seasonality
SELECT
    id AS series_id,
    (classification).timing_classification AS timing_class,
    ROUND((classification).seasonal_strength, 4) AS strength
FROM ts_classify_seasonality_by('sales', product_id, date, value, 7)
WHERE (classification).is_seasonal = true;
```

### Forecasting Method Selection

```sql
-- Recommend forecasting method based on classification
SELECT
    id AS series_id,
    CASE
        WHEN NOT (classification).is_seasonal THEN 'AutoARIMA (non-seasonal)'
        WHEN (classification).has_stable_timing AND (classification).seasonal_strength > 0.5
            THEN 'MSTL (strong stable seasonality)'
        WHEN (classification).has_stable_timing
            THEN 'ETS (moderate seasonality)'
        ELSE 'AutoARIMA with seasonal differencing'
    END AS recommended_method
FROM ts_classify_seasonality_by('sales', product_id, date, value, 7);
```

---

## Parameters

The function signature is:
```sql
ts_classify_seasonality_by(source, group_col, date_col, value_col, period)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Table name |
| `group_col` | COLUMN | Column identifying groups/series |
| `date_col` | COLUMN | Timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | INTEGER | Expected seasonal period (e.g., 7 for weekly) |

---

## Output Columns

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `classification` | STRUCT | Struct containing classification results |

### Classification Struct Fields

| Field | Type | Description |
|-------|------|-------------|
| `timing_classification` | VARCHAR | 'non_seasonal', 'stable_seasonal', 'variable_seasonal', 'intermittent_seasonal' |
| `modulation_type` | VARCHAR | 'non_seasonal', 'stable', 'modulated', 'unknown' |
| `has_stable_timing` | BOOLEAN | Whether seasonal timing is consistent |
| `timing_variability` | DOUBLE | Measure of timing consistency (lower = more stable) |
| `seasonal_strength` | DOUBLE | Overall seasonal strength (0-1) |
| `is_seasonal` | BOOLEAN | Whether series exhibits seasonality |
| `cycle_strengths` | DOUBLE[] | Strength of each detected cycle |
| `weak_seasons` | BIGINT[] | Indices of weak seasonal cycles |

---

## Key Concepts

### Timing Classification Types

| Classification | Description |
|----------------|-------------|
| `non_seasonal` | No detectable seasonal pattern |
| `stable_seasonal` | Consistent seasonal pattern with regular timing |
| `variable_seasonal` | Seasonality present but timing varies across cycles |
| `intermittent_seasonal` | Seasonality appears and disappears |

### Modulation Types

| Type | Description |
|------|-------------|
| `non_seasonal` | No seasonality detected |
| `stable` | Constant amplitude across cycles |
| `modulated` | Amplitude varies across cycles |
| `unknown` | Could not determine modulation type |

### Period Selection

| Data Frequency | Typical Period |
|----------------|----------------|
| Hourly | 24 (daily pattern) |
| Daily | 7 (weekly) or 30 (monthly) |
| Weekly | 52 (yearly) |
| Monthly | 12 (yearly) |

---

## Tips

1. **Detect period first** - Use `ts_detect_periods_by()` to find the actual period before classification.

2. **Check `is_seasonal` first** - Before interpreting other fields, verify seasonality exists.

3. **Use for method selection** - Match forecasting method to seasonality type for best results.

4. **Monitor `timing_variability`** - Low variability (< 0.1) indicates predictable patterns.

5. **Inspect `weak_seasons`** - Identify anomalous cycles that may need special handling.

---

## Related Functions

- `ts_detect_periods_by()` - Detect seasonal periods automatically
- `ts_mstl_decomposition_by()` - Decompose series into trend/seasonal/remainder
- `ts_forecast_by()` - Forecast using appropriate seasonal model
