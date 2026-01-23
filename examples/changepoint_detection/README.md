# Changepoint Detection Examples

> **Changepoints mark where the rules of the game change.**

This folder contains runnable SQL examples demonstrating time series changepoint detection with the anofox-forecast extension.

## Function

| Function | Description |
|----------|-------------|
| `ts_detect_changepoints_by` | Detect structural breaks for multiple series |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_changepoint_examples.sql`](synthetic_changepoint_examples.sql) | Multi-series changepoint detection examples | Synthetic |
| [`m5_changepoint_examples.sql`](m5_changepoint_examples.sql) | Full-scale analysis on M5 dataset (~30k items) | [M5 Competition](https://www.kaggle.com/c/m5-forecasting-accuracy) |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/changepoint_detection/synthetic_changepoint_examples.sql

# Run M5 changepoint detection (requires httpfs for remote data)
./build/release/duckdb -unsigned < examples/changepoint_detection/m5_changepoint_examples.sql
```

---

## What is Changepoint Detection?

Changepoint detection identifies points in a time series where the statistical properties change significantly. Common causes include:

- **Demand shifts**: Product lifecycle phases (launch, growth, maturity, decline)
- **External shocks**: Promotions, holidays, supply disruptions, competitor actions
- **Trend breaks**: Seasonality changes, market dynamics, policy changes
- **Data issues**: Sensor failures, reporting changes, missing data periods

---

## Usage

### Basic Changepoint Detection

```sql
-- Detect changepoints with default parameters
SELECT * FROM ts_detect_changepoints_by('sales', product_id, date, value, MAP{});
```

### Accessing Results

```sql
-- Extract changepoint indices and count
SELECT
    id,
    (changepoints).changepoint_indices AS detected_indices,
    list_count((changepoints).changepoint_indices) AS n_changepoints
FROM ts_detect_changepoints_by('sales', product_id, date, value, MAP{});
```

### Tuning Sensitivity

```sql
-- Conservative detection (fewer changepoints)
SELECT * FROM ts_detect_changepoints_by('sales', product_id, date, value,
    MAP{'hazard_lambda': '500'});

-- Sensitive detection (more changepoints)
SELECT * FROM ts_detect_changepoints_by('sales', product_id, date, value,
    MAP{'hazard_lambda': '50'});
```

### Include Probabilities

```sql
-- Get changepoint probabilities for each point
SELECT
    id,
    (changepoints).changepoint_indices AS indices,
    (changepoints).changepoint_probability AS probabilities
FROM ts_detect_changepoints_by('sales', product_id, date, value,
    MAP{'include_probabilities': 'true'});
```

---

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `hazard_lambda` | VARCHAR | '250' | Expected run length between changepoints |
| `include_probabilities` | VARCHAR | 'false' | Return probability scores for each point |

When `MAP{}` is passed (empty), uses default parameters.

---

## Output Columns

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `changepoints` | STRUCT | Struct containing detection results |

### Changepoints Struct Fields

| Field | Type | Description |
|-------|------|-------------|
| `is_changepoint` | BOOLEAN[] | Per-point changepoint flags |
| `changepoint_probability` | DOUBLE[] | Per-point probability scores |
| `changepoint_indices` | UBIGINT[] | Indices of detected changepoints |

---

## Key Concepts

### Understanding `hazard_lambda`

The `hazard_lambda` parameter controls detection sensitivity. It represents the **expected number of observations between changepoints**.

**Intuition**: If you believe demand typically stays stable for ~2 months before shifting, and you have daily data, use `hazard_lambda = 60`.

### Tuning Guide by Data Frequency

| Data Frequency | Conservative | Balanced | Sensitive |
|----------------|--------------|----------|-----------|
| **Daily** | 180-365 | 30-90 | 7-14 |
| **Weekly** | 26-52 | 8-12 | 2-4 |
| **Monthly** | 12-24 | 3-6 | 1-2 |
| **Hourly** | 720-2160 | 168-336 | 24-72 |

### Quick Reference

```
hazard_lambda = 10   -> Very sensitive (many changepoints)
hazard_lambda = 50   -> Sensitive
hazard_lambda = 100  -> Balanced
hazard_lambda = 250  -> Conservative (default)
hazard_lambda = 500  -> Very conservative
```

---

## BOCPD Algorithm

This extension uses **Bayesian Online Changepoint Detection (BOCPD)**, which:

- Provides **probability scores** for each point being a changepoint
- Works **online** (streaming) - doesn't need future data
- **Adapts automatically** to different series characteristics
- Uses **uninformative priors** - no domain knowledge required

---

## Tips

1. **Start conservative** - Use higher `hazard_lambda` to catch only major changes, then decrease if needed.

2. **Validate changepoints** - Spot-check detected changepoints by plotting the time series.

3. **Consider seasonality** - Regular seasonal patterns may be detected as changepoints. Deseasonalize first if needed.

4. **Combine with domain knowledge** - Detected changepoints often correspond to known events.

5. **Use for segmentation** - Changepoints naturally segment a time series into regimes.

---

## Troubleshooting

### Q: Too many changepoints detected

**A:** Increase `hazard_lambda` to be more conservative:
```sql
MAP{'hazard_lambda': '500'}
```

### Q: Obvious changepoints not detected

**A:** Decrease `hazard_lambda` to be more sensitive:
```sql
MAP{'hazard_lambda': '50'}
```

### Q: First observation has high probability

**A:** This is expected. At the start of a series, there's no prior history. The `is_changepoint` flag correctly excludes the first observation.

---

## Related Functions

- `ts_mstl_decomposition_by()` - Deseasonalize before detection
- `ts_forecast_by()` - Forecast different regimes separately
- `ts_features_by()` - Extract features before/after changepoints
