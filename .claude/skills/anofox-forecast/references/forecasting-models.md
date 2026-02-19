# Forecasting Models Reference

## ts_forecast_by (Primary)

Signature:
```sql
ts_forecast_by(table_name VARCHAR, group_col COLUMN, date_col COLUMN, target_col COLUMN, method VARCHAR, horizon INTEGER, frequency VARCHAR, params MAP/STRUCT) -> TABLE
```

Parameters:
- table_name: Source table name (quoted string)
- group_col: Series identifier (unquoted)
- date_col: Date/timestamp (unquoted)
- target_col: Value to forecast (unquoted)
- method: Model name (case-sensitive)
- horizon: Periods to forecast
- frequency: Time step between observations ('1d', '1h', '1mo', etc.) — **required**
- params: MAP or STRUCT with model parameters

Returns: group_col, date_col, yhat (DOUBLE), yhat_lower (DOUBLE), yhat_upper (DOUBLE)

Example:
```sql
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'HoltWinters', 12, '1d',
    MAP{'seasonal_period': '7'});
```

## ts_forecast_exog_by (Exogenous)

Signature:
```sql
ts_forecast_exog_by(table_name, group_col, date_col, target_col, x_cols VARCHAR[], future_table, future_date_col, future_x_cols VARCHAR[], model, horizon, params, frequency) -> TABLE
```

Supported models with exogenous: ARIMAX (from ARIMA/AutoARIMA), ThetaX (from OptimizedTheta), MFLESX (from MFLES)

Example:
```sql
SELECT * FROM ts_forecast_exog_by(
    'sales_history', product_id, date, revenue, ['temperature'],
    'future_weather', date, ['temperature'],
    'AutoARIMA', 7, MAP{}, '1d'
);
```

## All 32 Models

### Automatic Selection (6)
| Model | Optional Params | Description |
|-------|----------------|-------------|
| AutoETS | seasonal_period | Auto ETS model selection |
| AutoARIMA | seasonal_period | Auto ARIMA selection |
| AutoTheta | seasonal_period | Auto Theta selection |
| AutoMFLES | seasonal_periods[] | Auto MFLES |
| AutoMSTL | seasonal_periods[] | Auto MSTL |
| AutoTBATS | seasonal_periods[] | Auto TBATS |

### Basic (6)
| Model | Required | Optional |
|-------|----------|----------|
| Naive | -- | -- |
| SMA | -- | window (def: 5) |
| SeasonalNaive | seasonal_period | -- |
| SES | -- | alpha (def: 0.3) |
| SESOptimized | -- | -- |
| RandomWalkDrift | -- | -- |

### Exponential Smoothing (4)
| Model | Required | Optional |
|-------|----------|----------|
| Holt | -- | alpha, beta |
| HoltWinters | seasonal_period | alpha, beta, gamma |
| SeasonalES | seasonal_period | alpha, gamma |
| SeasonalESOptimized | seasonal_period | -- |

### Theta Methods (5)
| Model | Optional |
|-------|----------|
| Theta | seasonal_period, theta |
| OptimizedTheta | seasonal_period |
| DynamicTheta | seasonal_period, theta |
| DynamicOptimizedTheta | seasonal_period |
| AutoTheta | seasonal_period |

### State Space & ARIMA (4)
| Model | Required | Optional |
|-------|----------|----------|
| ETS | -- | seasonal_period, model |
| AutoETS | -- | seasonal_period |
| ARIMA | p, d, q | P, D, Q, s |
| AutoARIMA | -- | seasonal_period |

### Multiple Seasonality (6)
| Model | Required | Optional |
|-------|----------|----------|
| MFLES | seasonal_periods[] | iterations |
| AutoMFLES | -- | seasonal_periods[] |
| MSTL | seasonal_periods[] | stl_method |
| AutoMSTL | -- | seasonal_periods[] |
| TBATS | seasonal_periods[] | use_box_cox |
| AutoTBATS | -- | seasonal_periods[] |

### Intermittent Demand (6)
| Model | Optional |
|-------|----------|
| CrostonClassic | -- |
| CrostonOptimized | -- |
| CrostonSBA | -- |
| ADIDA | -- |
| IMAPA | -- |
| TSB | alpha_d, alpha_p |

## Parameter Passing

All param values are strings. Arrays use JSON: `'[7, 365]'`.

```sql
MAP{'seasonal_period': '7'}
MAP{'seasonal_periods': '[7, 365]'}
MAP{'p': '1', 'd': '1', 'q': '1'}
```

## Model Selection Guide

| Data Characteristics | Recommended |
|---------------------|-------------|
| No trend, no seasonality | Naive, SES, SESOptimized |
| Trend only | Holt, Theta, RandomWalkDrift |
| Single seasonal period | SeasonalNaive, HoltWinters, SeasonalES |
| Multiple seasonalities | MSTL, MFLES, TBATS |
| Intermittent demand | CrostonClassic, CrostonSBA, TSB |
| Unknown | AutoETS, AutoARIMA, AutoTheta |

## Key Notes
- Seasonality NOT auto-detected -- detect with ts_detect_periods_by first, then pass explicitly
- seasonal_period is for single-season models; seasonal_periods (with JSON array) for multi-season models
- Model names are case-sensitive
