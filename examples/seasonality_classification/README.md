# Seasonality Classification Examples

> **Seasonality classification identifies the type and stability of seasonal patterns in time series data.**

This folder contains runnable SQL examples demonstrating seasonality classification with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_seasonality_examples.sql`](synthetic_seasonality_examples.sql) | 5 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/seasonality_classification/synthetic_seasonality_examples.sql
```

---

## Patterns Overview

### Pattern 1: Basic Seasonality Classification

**Use case:** Determine if a series has seasonality and what type (stable, variable, intermittent).

**Key function:** `ts_classify_seasonality(values, period, [strength_threshold], [timing_threshold])`

**Parameters:**
- `values` - Time series values as DOUBLE[]
- `period` - Expected seasonal period (e.g., 7 for weekly, 12 for monthly)
- `strength_threshold` - Minimum strength to consider seasonal (default: 0.3)
- `timing_threshold` - Threshold for timing variability (default: 0.1)

**See:** `synthetic_seasonality_examples.sql` Pattern 1

---

### Pattern 2: Aggregate Classification

**Use case:** Classify seasonality directly from (timestamp, value) pairs without pre-aggregation.

**Key function:** `ts_classify_seasonality_agg(ts, value, period)`

**Parameters:**
- `ts` - Timestamp column
- `value` - Value column
- `period` - Expected seasonal period

**See:** `synthetic_seasonality_examples.sql` Pattern 2

---

### Pattern 3: Table Macro for Grouped Analysis

**Use case:** Classify seasonality for multiple series in a single query.

**Key function:** `ts_classify_seasonality_by(source, group_col, date_col, value_col, period)`

**Parameters:**
- `source` - Table name as string
- `group_col` - Column identifying groups/series
- `date_col` - Timestamp column
- `value_col` - Value column
- `period` - Expected seasonal period

**See:** `synthetic_seasonality_examples.sql` Pattern 3

---

### Pattern 4: Forecasting Method Selection

**Use case:** Use classification results to automatically select appropriate forecasting methods.

**Logic:**
| Classification | Recommended Method |
|----------------|-------------------|
| Non-seasonal | AutoARIMA, Theta |
| Strong stable seasonality | MSTL, STL decomposition |
| Moderate seasonality | ETS with seasonal component |
| Variable seasonality | AutoARIMA with seasonal differencing |

**See:** `synthetic_seasonality_examples.sql` Pattern 4

---

### Pattern 5: Cycle Strength Analysis

**Use case:** Identify weak or anomalous seasonal cycles within a series.

**Key outputs:**
- `cycle_strengths` - Strength of each individual cycle
- `weak_seasons` - Indices of cycles below threshold

**See:** `synthetic_seasonality_examples.sql` Pattern 5

---

## Return Structure

All classification functions return a STRUCT with these fields:

| Field | Type | Description |
|-------|------|-------------|
| `timing_classification` | VARCHAR | Classification: 'non_seasonal', 'stable_seasonal', 'variable_seasonal', 'intermittent_seasonal' |
| `modulation_type` | VARCHAR | Amplitude modulation: 'non_seasonal', 'stable', 'modulated', 'unknown' |
| `has_stable_timing` | BOOLEAN | Whether seasonal timing is consistent across cycles |
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

### Threshold Selection

| Parameter | Default | Use Case |
|-----------|---------|----------|
| `strength_threshold = 0.3` | Default | Standard seasonality detection |
| `strength_threshold = 0.5` | Stricter | Only strong seasonal patterns |
| `strength_threshold = 0.1` | Lenient | Detect weak seasonality |
| `timing_threshold = 0.1` | Default | Standard timing consistency |

---

## Tips

1. **Start with detected period** - Use `ts_detect_seasonality()` to find the actual period, don't guess.

2. **Check `is_seasonal` first** - Before interpreting other fields, verify seasonality exists.

3. **Use classification for method selection** - Match forecasting method to seasonality type for best results.

4. **Monitor `timing_variability`** - Low variability (< 0.1) indicates predictable patterns suitable for scheduling.

5. **Inspect `weak_seasons`** - Identify anomalous cycles that may need special handling.

6. **Use table macro for batch analysis** - `ts_classify_seasonality_by` is efficient for analyzing many series.

---

## Related Functions

- `ts_analyze_seasonality()` - Detect period and strength without classification
- `ts_detect_seasonality()` - Quick period detection
- `ts_detect_amplitude_modulation()` - Detailed amplitude analysis
- `ts_detect_seasonality_changes()` - Detect when seasonality begins/ends
