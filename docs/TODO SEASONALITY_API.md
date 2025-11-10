# Seasonality Detection

## Overview

The `anofox-forecast` extension provides two powerful functions for automatic seasonality detection in time series data:

1. **`TS_DETECT_SEASONALITY`** - Simple detection returning seasonal periods
2. **`TS_ANALYZE_SEASONALITY`** - Analysis with seasonal/trend strength

These functions use autocorrelation-based periodogram analysis to automatically identify recurring patterns in your data.

## Functions

### TS_DETECT_SEASONALITY

Detects up to 3 seasonal periods from a time series.

**Signature:**
```sql
TS_DETECT_SEASONALITY(values: DOUBLE[]) -> INT[]
```

**Parameters:**
- `values`: Array of time series values

**Returns:**
- Array of detected seasonal periods (up to 3), sorted by period length

**Example:**
```sql
WITH aggregated AS (
    SELECT LIST(sales ORDER BY date) AS values
    FROM sales_data
)
SELECT TS_DETECT_SEASONALITY(values) AS periods
FROM aggregated;

-- Result: [7, 30]  (weekly and monthly seasonality)
```

### TS_ANALYZE_SEASONALITY

Performs comprehensive seasonality analysis including strength metrics.

**Signature:**
```sql
TS_ANALYZE_SEASONALITY(
    timestamps: TIMESTAMP[], 
    values: DOUBLE[]
) -> STRUCT(
    detected_periods: INT[],
    primary_period: INT,
    seasonal_strength: DOUBLE,
    trend_strength: DOUBLE
)
```

**Parameters:**
- `timestamps`: Array of timestamps
- `values`: Array of corresponding values

**Returns:**
A struct with:
- `detected_periods`: Up to 3 detected seasonal periods
- `primary_period`: The strongest (primary) seasonal period
- `seasonal_strength`: Strength of seasonality (0-1, higher = stronger)
- `trend_strength`: Strength of trend (0-1, higher = stronger)

**Example:**
```sql
WITH aggregated AS (
    SELECT 
        LIST(date ORDER BY date) AS timestamps,
        LIST(sales ORDER BY date) AS values
    FROM sales_data
)
SELECT 
    result.detected_periods AS periods,
    result.primary_period AS primary,
    ROUND(result.seasonal_strength, 3) AS seasonal_str,
    ROUND(result.trend_strength, 3) AS trend_str
FROM (
    SELECT TS_ANALYZE_SEASONALITY(timestamps, values) AS result
    FROM aggregated
);

-- Result:
-- periods: [7, 30, 91]
-- primary: 7
-- seasonal_str: 0.842
-- trend_str: 0.156
```

## How It Works

### Periodogram Analysis

The detection algorithm uses **autocorrelation-based periodogram analysis**:

1. **Autocorrelation Calculation**: For each potential period `p`, computes the correlation between the series and itself shifted by `p` time steps

2. **Peak Detection**: Identifies peaks in the autocorrelation function where:
   - The autocorrelation is above a threshold (default: 0.9)
   - The value is a local maximum (higher than neighbors)

3. **Period Selection**: Returns up to 3 strongest periods, sorted by period length

### Seasonal Strength Calculation

For detailed analysis (`TS_ANALYZE_SEASONALITY`):

1. **Decomposition**: Uses STL (Seasonal and Trend decomposition using Loess) or MSTL (Multiple STL for multiple seasonalities)

2. **Strength Metrics**:
   - **Seasonal Strength**: `1 - Var(Remainder) / Var(Seasonal + Remainder)`
   - **Trend Strength**: `1 - Var(Remainder) / Var(Trend + Remainder)`

Both metrics range from 0 to 1:
- **0.0-0.3**: Weak (seasonality/trend barely present)
- **0.3-0.6**: Moderate 
- **0.6-0.8**: Strong
- **0.8-1.0**: Very strong (dominant pattern)


## Parameters and Tuning

### Detection Parameters

The algorithm uses these internal defaults:

- **Min Period**: 4 (minimum cycle length to detect)
- **Max Period**: `min(n/3, 512)` where `n` is series length
- **Threshold**: 0.9 (90% of max autocorrelation)
- **Max Peaks**: 3 (returns up to 3 strongest periods)

### Data Requirements

- **Minimum Length**: 
  - `TS_DETECT_SEASONALITY`: At least 8 observations
  - `TS_ANALYZE_SEASONALITY`: At least 2 full seasonal cycles (e.g., 14+ for weekly)
  
- **Quality**: 
  - Data should be regularly spaced (no large gaps)
  - Non-constant (variance > 0)
  - Ideally, at least 3 complete seasonal cycles for reliable detection

## Common Patterns

| Period | Interpretation | Common In |
|--------|---------------|-----------|
| 7 | Weekly seasonality | Daily data (weekday effects) |
| 12 | Monthly seasonality | Monthly data (annual patterns) |
| 24 | Daily seasonality | Hourly data (day/night cycles) |
| 30 | Monthly seasonality | Daily data (~30 days/month) |
| 52 | Annual seasonality | Weekly data (52 weeks/year) |
| 365 | Annual seasonality | Daily data |

## Troubleshooting

### Empty Result (`[]`)

**Causes:**
- Data too short (< 8 observations)
- No clear seasonality (autocorrelations too weak)
- Constant or near-constant data (zero variance)

**Solutions:**
- Collect more data
- Check if data actually has seasonality (visualize it)
- Try different granularity (daily vs weekly vs monthly)

### Unexpected Periods

**Causes:**
- Harmonics (detected period is multiple/fraction of true period)
- Multiple overlapping patterns
- Irregular spacing in data

**Solutions:**
- Use `TS_ANALYZE_SEASONALITY` to see strength metrics
- Manually specify period if known
- Check data quality and completeness

### Low Seasonal Strength

**Causes:**
- Weak seasonality relative to noise
- Trend dominates the signal
- Irregular seasonal pattern

**Solutions:**
- Use robust forecasting methods
- Consider detrending first
- Use ensemble methods that combine seasonal and non-seasonal models

## Best Practices

1. **Always check seasonal strength** before using seasonal models
2. **Visualize your data** to validate detected periods
3. **Use the primary period** (first in list) for forecasting
4. **Consider multiple seasonalities** (use MSTL, TBATS, or MFLES)
5. **Re-detect periodically** as patterns may change over time

## Examples

See `examples/seasonality_detection.sql` for comprehensive examples including:
- Simple detection
- Detailed analysis
- Multiple series
- Integration with forecasting
- Rolling window analysis

