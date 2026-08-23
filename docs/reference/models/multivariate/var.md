# VAR (Vector Autoregression)

> Multivariate time series forecasting via Vector Autoregression

## Signature

```sql
ts_forecast_var_by(
    source     VARCHAR,     -- source table name (quoted string)
    date_col   VARCHAR,     -- date column name (quoted string)
    value_cols VARCHAR[],   -- array of value column names
    horizon    INTEGER,     -- periods to forecast
    frequency  VARCHAR,     -- time step between observations
    p          INTEGER,     -- lag order (default: 1)
    params     MAP          -- reserved for future use (default: MAP{})
)
→ TABLE (variable VARCHAR, forecast_step BIGINT, <date_col>, forecast_value DOUBLE)
```

**Note:** `ts_forecast_var_by` is a **dedicated multivariate function**, distinct from
`ts_forecast_by`. It does not accept a `group_col` (v1 is single-panel: one VAR fit over
the entire input table). Lag order is the named parameter `p` (not `order` — `ORDER` is a
SQL reserved word).

## Description

VAR(p) models multiple time series simultaneously, capturing cross-variable dynamics through
a matrix of autoregressive coefficients. Each variable is regressed on p lags of **all** variables
in the system, not just its own lags. This captures cross-variable influences that univariate
models cannot model.

**K input variables → K output series**, each with `horizon` forecast steps, returned in
**long format** (one row per variable × horizon step).

## Key Constraints (v1)

| Constraint | Details |
|------------|---------|
| Single panel | No `group_col`; one VAR fit over the entire input table |
| Equal-length columns | All value columns must have the same number of valid observations after null imputation |
| Minimum observations | n > k×p + 1 (n = obs after imputation, k = number of variables, p = lag order) |
| Point forecasts only | No prediction intervals in v1 |
| Null handling | Missing values are imputed via linear interpolation before fitting |

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `source` | VARCHAR | Yes | — | Source table name (quoted string) |
| `date_col` | VARCHAR | Yes | — | Date column name (quoted string) |
| `value_cols` | VARCHAR[] | Yes | — | Array of value column names to include as VAR variables |
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step between observations (e.g., `'1d'`, `'1h'`) |
| `p` | INTEGER | No | 1 | VAR lag order (named param `p`, not `order`) |
| `params` | MAP | No | `MAP{}` | Reserved for future parameters |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `variable` | VARCHAR | Variable name (from `value_cols`) |
| `forecast_step` | BIGINT | Forecast horizon step (1 = next period) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `forecast_value` | DOUBLE | Point forecast for this variable at this step |

## SQL Examples (verified end-to-end)

### VAR(1) default — 2-variable system

```sql
-- Create a synthetic 2-variable table
CREATE OR REPLACE TABLE var_src AS
    SELECT
        (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
        (0.6 * SIN(i * 0.4) + 0.1 * COS(i * 0.2)) AS y1,
        (0.05 * SIN(i * 0.4) + 0.7 * COS(i * 0.2)) AS y2
    FROM range(60) t(i);  -- 60 obs > k*p+1 = 3 minimum

-- VAR(1) forecast — 14-step ahead, long format output
SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d')
ORDER BY variable, forecast_step;
```

Expected: 2 variables × 14 steps = **28 rows** in long format.

### VAR(2) — higher lag order

```sql
-- Use p:=2 to capture longer-range cross-variable dynamics
SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d', p:=2)
ORDER BY variable, forecast_step;
```

### Row count verification

```sql
-- Verify k_vars * horizon rows
SELECT
    count(*) AS total_rows,
    count(DISTINCT variable) AS distinct_variables,
    count(*) FILTER (WHERE variable = 'y1') AS y1_rows,
    count(*) FILTER (WHERE variable = 'y2') AS y2_rows
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d');
-- Expected: total_rows=28, distinct_variables=2, y1_rows=14, y2_rows=14
```

### 3-variable system

```sql
-- VAR works for any number of variables K
SELECT variable, forecast_step, ROUND(forecast_value, 4) AS fv
FROM ts_forecast_var_by('macro_data', 'date', ['gdp', 'inflation', 'unemployment'], 8, '1mo')
ORDER BY variable, forecast_step;
-- Returns 3 × 8 = 24 rows
```

## Typical Workflow

```sql
-- Step 1: Prepare multivariate table (wide format: one column per variable)
CREATE OR REPLACE TABLE macro AS
    SELECT date, gdp_growth, cpi_change, unemployment_rate
    FROM macro_indicators
    WHERE date >= '2015-01-01'
    ORDER BY date;

-- Step 2: VAR(1) forecast — 4 quarters ahead
SELECT
    variable,
    forecast_step,
    <date_col> AS forecast_date,
    ROUND(forecast_value, 4) AS forecast
FROM ts_forecast_var_by(
    'macro',
    'date',
    ['gdp_growth', 'cpi_change', 'unemployment_rate'],
    4,
    '1mo'
)
ORDER BY variable, forecast_step;
```

## Model Details

- **Estimation:** OLS equation-by-equation (each variable regressed on all lagged variables).
  This is equivalent to MLE under normally distributed errors.
- **Lag order selection:** Explicit `p` parameter only in v1. Automatic AIC/BIC lag selection
  is planned for a future release.
- **Null imputation:** Missing values in any column are imputed via linear interpolation before
  fitting. If leading or trailing nulls cannot be interpolated, the series is truncated.
- **Equal-length check:** All columns must have the same effective length after imputation.
  A `DimensionMismatch` error is raised if they differ.
- **Under-determination guard:** If `n_eff < k × p + 1`, the function raises an error before
  the FFI call (`n_eff = n - p`; the OLS system would be underdetermined).

## Common Pitfalls

| Pitfall | Problem | Solution |
|---------|---------|---------|
| `order` parameter name | DuckDB SQL parser rejects `order` as a named param | Use `p:=2` (not `order:=2`) |
| Missing values | `VAR::fit` rejects NaN/Inf | Values are auto-imputed; ensure series are not entirely null |
| Different column lengths | `DimensionMismatch` error | All value columns must have the same valid observation count |
| Short series | Under-determination | Ensure n > k × p + 1 |
| Non-stationary series | Coefficient matrix unstable (spectral radius > 1) | Difference series before fitting; check stationarity with `ts_kpss_by` or `ts_adf_by` |

## Benchmark

Behavioral parity confirmed against `statsmodels.tsa.api.VAR` on synthetic VAR(1) data
(c=[0.5, 0.3], A=[[0.6, 0.1], [0.05, 0.7]], N=200, seed=42):
- y1 MAE ratio (anofox/statsmodels): **1.000** (PASS)
- y2 MAE ratio: **1.000** (PASS)
- Note: anofox and statsmodels both use OLS, producing identical forecasts on the same data

See `benchmark/m4/var_benchmark/` for committed results.

## Reference

- Lütkepohl (2005), "New Introduction to Multiple Time Series Analysis"
- Sims (1980), "Macroeconomics and Reality"
