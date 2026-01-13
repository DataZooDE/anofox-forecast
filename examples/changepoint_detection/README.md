# Changepoint Detection Examples

> **Changepoints mark where the rules of the game change.**

This folder contains runnable SQL examples demonstrating time series changepoint detection with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_changepoint_examples.sql`](synthetic_changepoint_examples.sql) | 5 patterns using generated data | Synthetic |
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

### BOCPD Algorithm

This extension uses **Bayesian Online Changepoint Detection (BOCPD)**, which:

- Provides **probability scores** for each point being a changepoint
- Works **online** (streaming) - doesn't need future data
- **Adapts automatically** to different series characteristics
- Uses **uninformative priors** - no domain knowledge required

---

## Key Functions

### Scalar Function: `_ts_detect_changepoints_bocpd`

Low-level function for single arrays:

```sql
SELECT _ts_detect_changepoints_bocpd(
    [100, 100, 100, 10, 10, 10],  -- values array
    10.0,                          -- hazard_lambda (expected run length)
    true                           -- include_probabilities
);
-- Returns: STRUCT with is_changepoint[], changepoint_probability[], changepoint_indices[]
```

### Aggregate Function: `ts_detect_changepoints_agg`

For grouped data within a query:

```sql
SELECT
    item_id,
    ts_detect_changepoints_agg(timestamp, value, MAP{'hazard_lambda': '50'})
FROM sales
GROUP BY item_id;
```

### Table Macro: `ts_detect_changepoints_by`

Most efficient for processing many time series:

```sql
SELECT * FROM ts_detect_changepoints_by(
    'sales_table',     -- table name
    item_id,           -- group column
    timestamp,         -- date column
    value,             -- value column
    MAP{'hazard_lambda': '50'}
);
```

### Table Function: `ts_detect_changepoints`

For single series from a table:

```sql
SELECT * FROM ts_detect_changepoints(
    'sales_table',
    timestamp,
    value,
    MAP{'hazard_lambda': '10.0'}
);
```

---

## Patterns Overview

### Pattern 1: Quick Start (Table Macro)

**Use case:** Detect changepoints using the table macro.

```sql
SELECT * FROM ts_detect_changepoints('my_data', date_col, value_col, MAP{});
```

**See:** `synthetic_changepoint_examples.sql` Section 1

---

### Pattern 2: BOCPD Scalar Function

**Use case:** Use the scalar BOCPD function on array data.

```sql
SELECT _ts_detect_changepoints_bocpd(
    [1.0, 1.0, 1.0, 10.0, 10.0, 10.0],
    250.0,  -- hazard_lambda
    true    -- include_probabilities
);
```

**See:** `synthetic_changepoint_examples.sql` Section 2

---

### Pattern 3: Parameter Tuning (hazard_lambda)

**Use case:** Adjust sensitivity with the hazard_lambda parameter.

```sql
-- More sensitive detection (lower hazard_lambda)
SELECT * FROM ts_detect_changepoints('data', ts, val, MAP{'hazard_lambda': '100.0'});
```

**See:** `synthetic_changepoint_examples.sql` Section 3

---

### Pattern 4: Multi-Series Detection (Grouped)

**Use case:** Detect changepoints across multiple time series.

```sql
SELECT * FROM ts_detect_changepoints_by(
    'sales', product_id, date, value, MAP{}
);
```

**See:** `synthetic_changepoint_examples.sql` Section 4

---

### Pattern 5: Aggregate Function

**Use case:** Use aggregate function for custom grouping.

```sql
SELECT
    product_id,
    ts_detect_changepoints_agg(date, value, MAP{}) AS result
FROM sales
GROUP BY product_id;
```

**See:** `synthetic_changepoint_examples.sql` Section 5

---

## Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `hazard_lambda` | `250` | Expected run length between changepoints. Higher = fewer changepoints (more conservative). |
| `include_probabilities` | `false` | Return probability scores for each point. |

### Understanding `hazard_lambda`

The `hazard_lambda` parameter controls the algorithm's prior belief about how frequently changepoints occur. It represents the **expected number of observations between changepoints**.

**Intuition**: If you believe demand typically stays stable for ~2 months before shifting, and you have daily data, use `hazard_lambda = 60`.

### Tuning Guide by Data Frequency

| Data Frequency | Conservative | Balanced | Sensitive |
|----------------|--------------|----------|-----------|
| **Daily** (retail, web traffic) | 180-365 (6mo-1yr) | 30-90 (1-3mo) | 7-14 (1-2wk) |
| **Weekly** (inventory, reports) | 26-52 (6mo-1yr) | 8-12 (2-3mo) | 2-4 (2-4wk) |
| **Monthly** (financial, KPIs) | 12-24 (1-2yr) | 3-6 (quarter-half) | 1-2 (1-2mo) |
| **Hourly** (IoT, monitoring) | 720-2160 (1-3mo) | 168-336 (1-2wk) | 24-72 (1-3days) |

### Domain-Specific Recommendations

| Domain | Recommended `hazard_lambda` | Rationale |
|--------|----------------------------|-----------|
| **Retail demand** | 30-60 | Promotions, seasons cause frequent shifts |
| **Financial markets** | 60-180 | Regime changes less frequent but impactful |
| **Manufacturing** | 90-180 | Process changes are deliberate, less frequent |
| **Web/app metrics** | 14-30 | Product releases, campaigns cause frequent changes |
| **Sensor/IoT data** | 168-720 | Equipment typically stable, detect failures |

### Quick Reference

```
hazard_lambda = 10   -> Very sensitive (many changepoints)
hazard_lambda = 30   -> Sensitive (weekly patterns in daily data)
hazard_lambda = 50   -> Balanced (good starting point)
hazard_lambda = 100  -> Moderate (monthly patterns in daily data)
hazard_lambda = 250  -> Conservative (default, major changes only)
hazard_lambda = 500  -> Very conservative (rare events only)
```

---

## Return Values

### Table Macros (ts_detect_changepoints, ts_detect_changepoints_by)

Returns a table with columns:

| Column | Type | Description |
|--------|------|-------------|
| `date_col` | TIMESTAMP | Timestamp |
| `value_col` | DOUBLE | Original value |
| `is_changepoint` | BOOLEAN | Changepoint flag |
| `changepoint_probability` | DOUBLE | Probability |

### Scalar Function (_ts_detect_changepoints_bocpd)

```sql
STRUCT(
    is_changepoint           BOOLEAN[],   -- Per-point flags
    changepoint_probability  DOUBLE[],    -- Per-point probabilities
    changepoint_indices      UBIGINT[]    -- Detected indices
)
```

---

## Tips

1. **Start conservative**: Use higher `hazard_lambda` (e.g., 100-250) to catch only major changes, then decrease if needed.

2. **Validate changepoints**: Spot-check detected changepoints by plotting the time series around those dates.

3. **Consider seasonality**: Regular seasonal patterns may be detected as changepoints. If this is undesired, consider deseasonalizing first.

4. **Combine with domain knowledge**: The most common changepoint dates in your data often correspond to known events (promotions, holidays, supply issues).

5. **Use for segmentation**: Changepoints naturally segment a time series into regimes that can be modeled separately.

---

## Troubleshooting

### Q: Too many changepoints detected

**A:** Increase `hazard_lambda` to be more conservative:

```sql
MAP{'hazard_lambda': '200'}  -- Instead of default 250
```

### Q: Obvious changepoints not detected

**A:** Decrease `hazard_lambda` to be more sensitive:

```sql
MAP{'hazard_lambda': '20'}  -- More sensitive
```

### Q: First observation always has high probability

**A:** This is expected behavior. At the start of a series, there's no prior history, so P(r=1) is naturally high. The `is_changepoint` flag correctly excludes the first observation.

### Q: Processing is slow

**A:** Use `ts_detect_changepoints_by` for batch processing many series - it's optimized for this use case.
