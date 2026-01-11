# Backtesting Examples

> **Backtesting is the art of simulating the past to predict the future.**

This folder contains runnable SQL examples demonstrating time series cross-validation and backtesting with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_backtest_examples.sql`](synthetic_backtest_examples.sql) | 5 patterns using generated data | Synthetic |
| [`m5_backtest_examples.sql`](m5_backtest_examples.sql) | Real-world examples with M5 dataset | [M5 Competition](https://www.kaggle.com/c/m5-forecasting-accuracy) |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/backtesting/synthetic_backtest_examples.sql

# Run M5 examples (requires httpfs for remote data)
./build/release/duckdb < examples/backtesting/m5_backtest_examples.sql
```

---

## Patterns Overview

### Pattern 1: Quick Start (One-Liner)

**Use case:** Quick model evaluation with minimal code.

```sql
SELECT * FROM ts_backtest_auto(
    'sales_data', store_id, date, revenue,
    7,              -- horizon
    5,              -- folds
    '1d',           -- frequency
    MAP{'method': 'AutoETS'}
);
```

**See:** `synthetic_backtest_examples.sql` Section 1

---

### Pattern 2: Regression with External Features

**Use case:** Sales depend on temperature, holidays, promotions.

**Requires:** `anofox_statistics` extension for OLS regression.

**Key functions:**
- `ts_cv_split` - Create train/test splits
- `ts_prepare_regression_input` - Mask target for test rows
- `ols_fit_predict_by` - Fit and predict in one pass

**See:** `synthetic_backtest_examples.sql` Section 2

---

### Pattern 3: Production Reality (Gap Parameter)

**Use case:** Simulate ETL latency where data arrives with a delay.

```sql
SELECT * FROM ts_backtest_auto(
    'sales_data', store_id, date, revenue, 7, 5, '1d',
    MAP{
        'method': 'AutoARIMA',
        'gap': '2'      -- Skip 2 days between train end and test start
    }
);
```

**See:** `synthetic_backtest_examples.sql` Section 3

---

### Pattern 4: Composable Pipeline

**Use case:** Need full control for debugging or custom transformations.

**Steps:**
1. `ts_cv_generate_folds` - Generate fold cutoff dates
2. `ts_cv_split` - Create train/test splits
3. Filter to training data
4. `ts_cv_forecast_by` - Run forecasts in parallel

**See:** `synthetic_backtest_examples.sql` Section 4

---

### Pattern 5: Unknown vs Known Features (Mask & Fill)

**Use case:** Prevent look-ahead bias by masking features not known at forecast time.

**Key functions:**
- `ts_hydrate_features` - Join features with `_is_test` flag
- `ts_fill_unknown` - Impute masked values

**See:** `synthetic_backtest_examples.sql` Section 5

---

### Pattern 6: Scenario Calendar (What-If Analysis)

**Use case:** Test hypothetical interventions on specific dates.

**Example questions:**
- "What if we ran promotions on Feb 20-22?"
- "What if we had a price change starting March 1?"

**Approach:**
1. Create a scenario calendar with date ranges and intervention values
2. Join calendar to test data to apply interventions
3. Compare baseline vs what-if forecasts

```sql
-- Define scenario calendar
CREATE TABLE promo_calendar AS
SELECT * FROM (VALUES
    ('2024-02-20'::DATE, '2024-02-22'::DATE, 'winter_sale'),
    ('2024-03-05'::DATE, '2024-03-07'::DATE, 'spring_launch')
) AS t(start_date, end_date, promo_name);

-- Apply to test data
SELECT
    s.*,
    CASE WHEN p.promo_name IS NOT NULL THEN 1 ELSE 0 END AS promo_scenario
FROM test_data s
LEFT JOIN promo_calendar p
    ON s.date >= p.start_date AND s.date <= p.end_date;
```

**See:** `synthetic_backtest_examples.sql` Section 6

---

## M5 Dataset Examples

The M5 examples demonstrate backtesting on real retail sales data:

| Section | Description |
|---------|-------------|
| 1 | Load M5 data subset (10 items) |
| 2 | Basic backtest with SeasonalNaive |
| 3 | Model comparison (Naive, SeasonalNaive, Theta) |
| 4 | Different metrics (SMAPE, Coverage) |
| 5 | Backtest with gap parameter |
| 6 | OLS regression backtest |
| 7 | Per-item performance analysis |

**See:** `m5_backtest_examples.sql`

---

## Key Concepts

### Gap vs Embargo

```
GAP: Simulates data latency (ETL delays)
─────────────────────────────────────────

  Day:  1   2   3   4   5   6   7   8   9  10  11  12  13  14
       [TRAIN TRAIN TRAIN TRAIN]     [TEST TEST TEST TEST]
                             │   ▲   │
                             └───┼───┘
                                 │
                            gap = 2 days

  Reality: "I don't get Monday's data until Wednesday"

EMBARGO: Prevents label leakage between folds
─────────────────────────────────────────────

  Fold 1: [TRAIN TRAIN TRAIN][TEST TEST TEST]
  Fold 2:          ███████████[TRAIN TRAIN][TEST TEST TEST]
                   ▲         ▲
                   └─────────┘
                   embargo = 3 days
                   (excluded from fold 2 training)

  Reality: "Rolling 7-day sales target overlaps previous test window"
```

### Metrics

All examples use built-in metric functions:

```sql
SELECT
    fold_id,
    ts_mae(LIST(actual), LIST(forecast)) AS mae,
    ts_rmse(LIST(actual), LIST(forecast)) AS rmse,
    ts_bias(LIST(actual), LIST(forecast)) AS bias
FROM predictions
GROUP BY fold_id;
```

### Fill Methods for Unknown Features

| Method | Description | Best For |
|--------|-------------|----------|
| `'last_value'` | Forward-fill from last training value | Slowly changing features |
| `'mean'` | Use training set mean | Stable, centered features |
| `'zero'` | Fill with zeros | Event indicators (default = no event) |
| `'linear'` | Linear interpolation | Trending features |

---

## Tips

1. **Start Simple** - Run `ts_backtest_auto` with `Naive` or `SeasonalNaive` before complex models.

2. **Check the Gap** - If accuracy is suspiciously high (99% R-squared), you probably forgot `gap` or masked a feature.

3. **Match Your Horizon** - Backtest `horizon` should match production forecast window.

4. **Use Multiple Folds** - Never trust a single fold. Use 3-5 folds minimum.

5. **Control Fold Spacing** - Use `skip_length` for custom spacing:
   - `skip_length: '1'` - Dense overlapping folds
   - `skip_length: '30'` - Sparse monthly checkpoints

---

## Troubleshooting

### Q: Why is my forecast NULL?

**A:** Unknown features in test set were not filled. Use `ts_fill_unknown`:

```sql
SELECT * FROM ts_fill_unknown(
    'masked_data', store_id, date, your_feature,
    cutoff_date, MAP{'strategy': 'last_value'}
);
```

### Q: Can I use different models for different groups?

**A:** Yes! Each group gets its own model. For explicit selection:

```sql
SELECT * FROM ts_backtest_auto('large_stores', ..., MAP{'method': 'AutoARIMA'})
UNION ALL
SELECT * FROM ts_backtest_auto('small_stores', ..., MAP{'method': 'Theta'});
```
