# Conformal Prediction Examples

> **Distribution-free prediction intervals with guaranteed coverage (under exchangeability
> and a large-enough calibration set — see `docs/api/11-conformal-prediction.md`).**

This folder contains runnable SQL examples demonstrating conformal prediction with the anofox-forecast extension.

## Functions

| Function | Description |
|----------|-------------|
| `ts_conformal_by` | One-step: calibrate + apply, one row per `(group, date)` |
| `ts_conformal_calibrate` | Calibrate a conformity profile per group (or pooled) |
| `ts_conformal_apply_by` | Apply a calibration TABLE to new forecasts |
| `ts_interval_width_by` | Mean interval width per group, paired by date |
| `ts_bootstrap_intervals` / `ts_bootstrap_quantiles` | Residual-resampling bootstrap intervals (model-agnostic) |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_conformal_examples.sql`](synthetic_conformal_examples.sql) | Multi-series conformal prediction, asymmetric, adaptive (difficulty_col), insufficient-calibration, and calibration-table-apply examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
```

---

## Usage

### Basic Conformal Prediction

```sql
-- Compute prediction intervals from backtest data
-- Table needs: actual, forecast (calibration), point_forecast (to add intervals), and a
-- date/step column so residuals and forecasts are paired by horizon, never value-sorted.
SELECT * FROM ts_conformal_by('backtest_data', product_id, date, actual, forecast, point_forecast, MAP{});
```

### Different Coverage Levels

```sql
-- 95% coverage (alpha=0.05)
SELECT * FROM ts_conformal_by('backtest_data', product_id, date, actual, forecast, point_forecast,
    params := {'alpha': 0.05});

-- 80% coverage (alpha=0.20) - narrower intervals
SELECT * FROM ts_conformal_by('backtest_data', product_id, date, actual, forecast, point_forecast,
    params := {'alpha': 0.20});
```

### Asymmetric Intervals

```sql
-- For skewed residual distributions -- the key is 'method', not 'asymmetric'
SELECT * FROM ts_conformal_by('backtest_data', product_id, date, actual, forecast, point_forecast,
    params := {'method': 'asymmetric'});
```

### Apply a Calibration Table

```sql
-- ts_conformal_apply_by takes a calibration TABLE name (ts_conformal_calibrate's output
-- shape), not a bare score. Wrap a literal score in a one-row table with group_col = NULL:
CREATE TABLE literal_score AS SELECT NULL::VARCHAR AS group_col, 'symmetric' AS method, 15.0 AS conformity_score;
SELECT * FROM ts_conformal_apply_by('forecast_table', product_id, date, point_forecast, 'literal_score');
```

---

## Parameters

`params` is read json-free via `params::MAP(VARCHAR, VARCHAR)`; a `{'key': value}` STRUCT
literal casts implicitly.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `alpha` | DOUBLE | `0.1` | Miscoverage rate (1 - coverage). 0.1 = 90% coverage |
| `method` | VARCHAR | `'symmetric'` | `'symmetric'` (alias `'split'`), `'asymmetric'`, or `'adaptive'` (requires `difficulty_col`) |
| `pooled` (calibrate only) | BOOLEAN | `false` | Pool every group into one row instead of one row per group |

When `params := MAP{}` (empty), defaults to 90% coverage with symmetric intervals, calibrated per group.

---

## Output Columns

### ts_conformal_by / ts_conformal_apply_by

One row per `(group_col, date_col)`:

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | VARCHAR | Series identifier |
| `date_col` | — | The paired date/step column |
| `point` | DOUBLE | Point forecast |
| `lower` | DOUBLE | Lower bound |
| `upper` | DOUBLE | Upper bound |
| `method` | VARCHAR | The applied method |
| `conformity_score` | DOUBLE | Calibrated quantile (`NULL` if insufficient) |
| `coverage` *(by only)* | DOUBLE | EMPIRICAL coverage on the calibration residuals |
| `nominal_level` *(by only)* | DOUBLE | The requested `1 - alpha` |
| `n_calibration` *(by only)* | BIGINT | Residuals actually used |
| `calibration_insufficient` *(by only)* | BOOLEAN | `true` when the calibration set is below the alpha's minimum size |

---

## Key Concepts

### What is Conformal Prediction?

Conformal prediction creates prediction intervals with **guaranteed coverage** without distributional assumptions, given exchangeability and a large-enough calibration set.

```
Coverage Guarantee: P(Y in [lower, upper]) >= 1 - alpha
```

### The Workflow

1. **Calibrate** from backtest residuals (actual - forecast), per group by default
2. **Compute** conformity score at target quantile, paired by date/horizon
3. **Apply** to new forecasts via a calibration TABLE: `point +/- conformity_score`

### Alpha, Coverage, and the Finite-Sample Minimum

| Alpha | Coverage | Symmetric/Adaptive minimum n | Asymmetric minimum n |
|-------|----------|-------------------------------|------------------------|
| 0.20 | 80% | 4 | 9 |
| 0.10 | 90% | 9 | 19 |
| 0.05 | 95% | 19 | 39 |

Below the minimum, `conformity_score`/bounds are `NULL` and `calibration_insufficient = true`
— never silently clamped to the maximum observed residual (see Section 8 of the synthetic
example).

### Symmetric, Asymmetric, Adaptive

| Method | Use When |
|--------|----------|
| **Symmetric** | Residuals are roughly symmetric |
| **Asymmetric** | Residuals are skewed (e.g., demand forecasting); not guaranteed tighter — measure both |
| **Adaptive** | Some periods are inherently harder to forecast; pass a per-row `difficulty_col` at calibration AND prediction (no batch-mean normalisation) |

---

## Data Requirements

`ts_conformal_by` expects a table with:

1. **Calibration rows**: `actual` and `forecast` are non-NULL, `point_forecast` is NULL
2. **Forecast rows**: `actual` and `forecast` are NULL, `point_forecast` is non-NULL
3. A date/step column present on every row (required, orders and pairs everything)

```sql
-- Example table structure
CREATE OR REPLACE TABLE backtest_data AS
SELECT * FROM (
    -- Calibration data
    SELECT 'A' AS id, DATE '2024-01-01' AS date, 100.0 AS actual, 98.0 AS forecast, NULL::DOUBLE AS point_forecast
    UNION ALL
    -- ... more calibration rows
    -- Forecast data
    SELECT 'A' AS id, DATE '2024-02-01' AS date, NULL AS actual, NULL AS forecast, 150.0 AS point_forecast
);
```

---

## Tips

1. **More calibration data = better** — and REQUIRED past a minimum for your alpha (see table above).

2. **Match the forecast horizon** — Calibrate on residuals from the same horizon you're forecasting.

3. **Check interval widths** — Wide intervals indicate high uncertainty in your forecasts.

4. **Use asymmetric for demand**, but measure width — it needs roughly double the calibration-set size of symmetric at the same alpha.

5. **Combine with backtesting** — Use backtest results from `ts_cv_folds_by` + `ts_cv_forecast_by` as calibration data.

6. **No fitted model handy?** `ts_bootstrap_intervals`/`ts_bootstrap_quantiles` build intervals purely from resampled residuals — model-agnostic, no calibration table needed.

---

## Related Functions

- `ts_cv_folds_by()` + `ts_cv_forecast_by()` - Generate backtest results for calibration
- `ts_forecast_by()` - Generate point forecasts to conformalize
- `ts_conformal_coverage()` / `ts_conformal_evaluate()` - Evaluate interval coverage
