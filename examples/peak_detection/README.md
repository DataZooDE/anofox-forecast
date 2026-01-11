# Peak Detection Examples

> **Peak detection identifies local maxima in time series data.**

This folder contains runnable SQL examples demonstrating peak detection and timing analysis with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_peak_examples.sql`](synthetic_peak_examples.sql) | 2 patterns using generated data | Synthetic |
| [`m5_peak_examples.sql`](m5_peak_examples.sql) | Real-world examples with M5 dataset | [M5 Competition](https://www.kaggle.com/c/m5-forecasting-accuracy) |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/peak_detection/synthetic_peak_examples.sql

# Run M5 examples (requires httpfs for remote data)
./build/release/duckdb < examples/peak_detection/m5_peak_examples.sql
```

---

## Patterns Overview

### Pattern 1: Basic Peak Detection

**Use case:** Find local maxima in time series for demand peaks, anomaly spikes, seasonal highs.

**Key function:** `ts_detect_peaks(values, min_prominence, min_distance)`

**Parameters:**
- `min_prominence` - Filter peaks by significance (0-1 normalized scale)
- `min_distance` - Minimum observations between peaks

**See:** `synthetic_peak_examples.sql` Pattern 1

---

### Pattern 2: Peak Timing Analysis

**Use case:** Analyze when peaks occur within seasonal cycles for demand planning and consistency checks.

**Key function:** `ts_analyze_peak_timing(values, period)`

**Parameters:**
- `period` - Expected seasonal cycle length (e.g., 7 for weekly, 24 for hourly-daily)

**Key outputs:**
- `variability_score` - 0 = stable, 1 = highly variable
- `is_stable` - TRUE if variability_score < 0.3
- `timing_trend` - Positive = peaks shifting later over time

**See:** `synthetic_peak_examples.sql` Pattern 2

---

## M5 Dataset Examples

**Important:** Peak detection only makes sense for seasonal time series. The M5 examples demonstrate the correct workflow: detect seasonality first, then run peak analysis only on items with detected patterns.

| Section | Description |
|---------|-------------|
| 1 | Load M5 data subset (50 items) |
| 2 | Seasonality detection (filter for meaningful analysis) |
| 3 | Peak detection on seasonal items only |
| 4 | Peak timing analysis using detected period |
| 5 | Demand spike detection with dates |
| 6 | Summary with seasonality context |

**Key insight:** ~70% of M5 items have detectable seasonality. Non-seasonal items (higher % zeros, lower demand) would produce meaningless peak detection results.

**See:** `m5_peak_examples.sql`

---

## Key Concepts

### Peak Prominence

Prominence measures how much a peak "stands out" from surrounding data (normalized 0-1 scale):

| Prominence Level | Use Case |
|------------------|----------|
| 0 (default) | Find all local maxima |
| 0.01-0.1 | Include minor peaks |
| 0.1-0.3 | Moderate peaks |
| 0.3-0.5 | Significant peaks only |
| 0.5+ | Only major peaks/anomalies |

### Minimum Distance

Controls peak spacing to prevent detecting minor fluctuations:

| Data Frequency | Typical min_distance |
|----------------|---------------------|
| Hourly | 6-12 (half day) |
| Daily | 5-7 (one week) |
| Weekly | 4 (one month) |
| Monthly | 8-12 (one year) |

### Variability Score

The variability score (0-1) indicates timing regularity:

| Score Range | Interpretation |
|-------------|----------------|
| < 0.1 | Very consistent (peaks at same position each cycle) |
| 0.1-0.3 | Consistent (`is_stable = TRUE`) |
| 0.3-0.5 | Moderately variable |
| > 0.5 | Highly variable timing |

### Period Selection for Timing Analysis

| Data Frequency | Typical Period |
|----------------|----------------|
| Hourly | 24 (daily pattern) |
| Daily | 7 (weekly) or 30 (monthly) |
| Weekly | 52 (yearly) |
| Monthly | 12 (yearly) |

---

## Parameter Selection Guide

### Step 1: Check Data Suitability

Before peak detection, verify your data has patterns:

```sql
-- Detect seasonality first
SELECT ts_detect_seasonality(LIST(value ORDER BY time)) AS periods FROM my_data;
```

- **Empty result `[]`**: Data is non-seasonal, peak detection may be meaningless
- **Result like `[7, 14]`**: Use the first period (7) for timing analysis

### Step 2: Start Without Filtering

```sql
-- See all peaks first
SELECT (ts_detect_peaks(values)).n_peaks AS total FROM ...;
```

### Step 3: Add Prominence Filter

| Data Type | Recommended Prominence | Rationale |
|-----------|----------------------|-----------|
| Sensor anomalies | 0.5-0.7 | Only major spikes |
| Daily traffic peaks | 0.3-0.5 | Clear daily maxima |
| Sales seasonality | 0.2-0.3 | Yearly/monthly peaks |
| Physiological signals | 0.3+ | R-peaks in ECG |
| Noisy data | 0.1 minimum | Filter minor fluctuations |

### Step 4: Use Detected Period for Timing

```sql
-- Use detected period, not hardcoded
WITH seasonality AS (
    SELECT ts_detect_seasonality(values)[1] AS period FROM ...
)
SELECT ts_analyze_peak_timing(values, period) FROM ...;
```

### Common Pitfalls

| Problem | Symptom | Solution |
|---------|---------|----------|
| Intermittent data | variability_score = 1.0 | Check seasonality first, filter non-seasonal items |
| Too strict filtering | 0 peaks found | Lower prominence threshold |
| Too many peaks | Noise detected as peaks | Increase prominence to 0.2+ |
| Wrong period | Meaningless timing stats | Use `ts_detect_seasonality()` to find actual period |

---

## Tips

1. **Start with no filtering** - Run `ts_detect_peaks(values)` first to see all peaks, then add prominence filter.

2. **Use prominence for noise filtering** - Real data often has small fluctuations; prominence > 0.1 usually filters noise.

3. **Detect seasonality first** - Use `ts_detect_seasonality()` to find the actual period, don't guess.

4. **Check timing stability** - `is_stable = TRUE` means peaks are predictable for scheduling and planning.

5. **Watch for drift** - `timing_trend` shows if peaks are shifting over time (positive = later, negative = earlier).

6. **Filter non-seasonal items** - Peak detection on intermittent demand (>50% zeros) produces meaningless results.

---

## Related Functions

- `ts_detect_changepoints_by()` - Detect structural changes (different from peaks)
- `ts_detect_seasonality()` - Detect seasonal period automatically
- `ts_detrend()` - Remove trend before peak detection for cleaner results
