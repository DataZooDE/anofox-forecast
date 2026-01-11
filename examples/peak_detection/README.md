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

The M5 examples demonstrate peak detection on real retail sales data:

| Section | Description |
|---------|-------------|
| 1 | Load M5 data subset (10 items) |
| 2 | Basic peak detection on sales data |
| 3 | Weekly peak timing analysis |
| 4 | Multi-item peak comparison |
| 5 | Demand spike detection with dates |
| 6 | Peak consistency across items |
| 7 | Combined analysis summary |

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

## Tips

1. **Start with no filtering** - Run `ts_detect_peaks(values)` first to see all peaks, then add prominence filter.

2. **Use prominence for noise filtering** - Real data often has small fluctuations; prominence > 0.1 usually filters noise.

3. **Match period to your cycle** - For weekly patterns, use period=7. Wrong periods give meaningless results.

4. **Check timing stability** - `is_stable = TRUE` means peaks are predictable for scheduling and planning.

5. **Watch for drift** - `timing_trend` shows if peaks are shifting over time (positive = later, negative = earlier).

---

## Related Functions

- `ts_detect_changepoints_by()` - Detect structural changes (different from peaks)
- `ts_detect_seasonality()` - Detect seasonal period automatically
- `ts_detrend()` - Remove trend before peak detection for cleaner results
