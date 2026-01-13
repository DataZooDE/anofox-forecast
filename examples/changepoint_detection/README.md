# Changepoint Detection Examples

> **Changepoints mark where the rules of the game change.**

This folder contains runnable SQL examples demonstrating time series changepoint detection with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`m5_changepoint_examples.sql`](m5_changepoint_examples.sql) | Full-scale analysis on M5 dataset (~30k items) | [M5 Competition](https://www.kaggle.com/c/m5-forecasting-accuracy) |

## Quick Start

```bash
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

### Sensitivity Analysis

Run detection at multiple thresholds to understand your data:

```sql
-- Compare changepoint counts at different sensitivities
WITH sensitivity_test AS (
    SELECT 'sensitive' AS level, 30 AS lambda
    UNION ALL SELECT 'balanced', 50
    UNION ALL SELECT 'conservative', 100
)
SELECT
    s.level,
    s.lambda,
    AVG(list_count((changepoints).changepoint_indices)) AS avg_changepoints
FROM sensitivity_test s,
LATERAL (
    SELECT * FROM ts_detect_changepoints_by(
        'your_table', group_col, date_col, value_col,
        MAP{'hazard_lambda': s.lambda::VARCHAR}
    )
) results
GROUP BY s.level, s.lambda
ORDER BY s.lambda;
```

### Rule of Thumb

1. **Start with `hazard_lambda = n/10`** where `n` is your series length
2. **Validate**: Plot a few series and check if detected changepoints make sense
3. **Adjust**: Too many? Increase lambda. Missing obvious ones? Decrease lambda
4. **Domain knowledge wins**: If you know changes happen quarterly, use ~90 for daily data

---

## Example Outputs

### Individual Changepoint Detection

```sql
SELECT
    item_id,
    list_count(cp_indices) AS n_changepoints
FROM ts_detect_changepoints_by('m5_data', item_id, ds, y, MAP{'hazard_lambda': '50'});
```

| item_id | n_changepoints |
|---------|----------------|
| FOODS_1_001_CA_1 | 3 |
| FOODS_1_002_CA_1 | 5 |
| HOBBIES_1_001_CA_1 | 1 |

### Changepoint Timing Analysis

```sql
-- When do changepoints occur most frequently?
SELECT
    EXTRACT(MONTH FROM cp_date) AS month,
    COUNT(*) AS n_changepoints
FROM changepoint_dates
GROUP BY month
ORDER BY n_changepoints DESC;
```

| month | n_changepoints |
|-------|----------------|
| 12 | 4521 |
| 11 | 3892 |
| 1 | 3654 |

### Category Patterns

```sql
-- Which categories have most volatile demand?
SELECT
    category,
    AVG(n_changepoints) AS avg_changepoints_per_item
FROM item_changepoints
GROUP BY category
ORDER BY avg_changepoints_per_item DESC;
```

| category | avg_changepoints_per_item |
|----------|---------------------------|
| FOODS | 4.2 |
| HOUSEHOLD | 3.1 |
| HOBBIES | 2.8 |

---

## M5 Dataset Analysis

The M5 example processes the full dataset (~30,490 items) and provides:

1. **Overall statistics**: Total changepoints, items affected, distribution
2. **Temporal patterns**: When changepoints occur (month, day of week, year)
3. **Category analysis**: Which product categories are most volatile
4. **Store analysis**: Which stores experience more demand shifts
5. **Event detection**: Common dates where many items change (potential external events)
6. **Volatility classification**: Stable vs. volatile items

### Key Insights from M5 Data

- **Holiday effects**: December and January show elevated changepoint frequency
- **Category differences**: FOODS items tend to have more changepoints than HOBBIES
- **Store patterns**: Different stores may have different volatility profiles
- **Common events**: Multiple items changing on the same date suggests external factors

---

## Use Cases

### 1. Demand Planning

Identify when demand regimes change to adjust forecasting models:

```sql
-- Find items that changed recently (last 30 days)
SELECT item_id, MAX(cp_date) AS last_changepoint
FROM changepoint_dates
WHERE cp_date > CURRENT_DATE - INTERVAL 30 DAY
GROUP BY item_id;
```

### 2. Anomaly Investigation

Investigate dates where many items changed simultaneously:

```sql
-- Potential external events (many items affected on same date)
SELECT cp_date, COUNT(DISTINCT item_id) AS items_affected
FROM changepoint_dates
GROUP BY cp_date
HAVING COUNT(DISTINCT item_id) > 100
ORDER BY items_affected DESC;
```

### 3. Forecast Model Selection

Use changepoint count to select appropriate models:

```sql
-- Volatile items might need different models than stable ones
SELECT
    CASE
        WHEN n_changepoints = 0 THEN 'stable'
        WHEN n_changepoints <= 3 THEN 'moderate'
        ELSE 'volatile'
    END AS volatility_class,
    COUNT(*) AS n_items
FROM item_changepoints
GROUP BY volatility_class;
```

### 4. Data Quality Checks

Sudden changepoints might indicate data issues:

```sql
-- Items with suspiciously many changepoints
SELECT item_id, n_changepoints
FROM item_changepoints
WHERE n_changepoints > 10  -- Investigate these
ORDER BY n_changepoints DESC;
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
