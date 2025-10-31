# Model Parameters Guide

This document describes all parameters available for each of the 31 forecasting models in the DuckDB Time Series Extension.

## Table of Contents

1. [Parameter Syntax](#parameter-syntax)
2. [Global Parameters](#global-parameters)
3. [Basic Models](#basic-models)
4. [Exponential Smoothing](#exponential-smoothing)
5. [Trend Models](#trend-models)
6. [Theta Models](#theta-models)
7. [ARIMA Models](#arima-models)
8. [State Space Models (ETS)](#state-space-models-ets)
9. [Multiple Seasonality Models](#multiple-seasonality-models)
10. [Intermittent Demand Models](#intermittent-demand-models)
11. [Parameter Tuning Examples](#parameter-tuning-examples)
12. [Best Practices](#best-practices)

---

## Parameter Syntax

Parameters are passed as MAP literals using DuckDB's `MAP{}` syntax:

```sql
TS_FORECAST(date, value, 'ModelName', horizon, MAP{'param1': value1, 'param2': value2})
```

For models with no parameters or to use defaults, pass an empty MAP:

```sql
TS_FORECAST(date, value, 'Naive', horizon, MAP{})
```

---

## Global Parameters

These parameters work with **all models**:

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `confidence_level` | DOUBLE | No | 0.90 | Confidence level for prediction intervals (0 < value < 1) |
| `generate_timestamps` | BOOLEAN | No | true | Generate forecast timestamps based on training data intervals |

**Validation:**
- `confidence_level` must be between 0 and 1 (exclusive)
- Higher values produce wider prediction intervals
- Common values: 0.80 (80%), 0.90 (90%), 0.95 (95%), 0.99 (99%)

**Examples:**
```sql
-- Default confidence level (0.90 = 90%)
SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{})
FROM data;

-- Custom confidence level (0.95 = 95%)
SELECT TS_FORECAST(date, value, 'AutoETS', 12, MAP{'confidence_level': 0.95})
FROM data;

-- Narrow intervals for conservative estimates (0.80 = 80%)
SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{'confidence_level': 0.80})
FROM data;

-- Disable timestamp generation for maximum speed
SELECT TS_FORECAST(date, value, 'Naive', 12, MAP{'generate_timestamps': false})
FROM data;

-- Combine multiple global parameters
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'confidence_level': 0.95, 'generate_timestamps': true, 'season_length': 7})
FROM data;
```

---

## Basic Models

### 1. Naive

The Naive forecast uses the last observed value as the forecast for all future periods.

**Parameters:** None

**Use Case:** Baseline model, random walk data

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'Naive', 7, MAP{}) AS forecast
FROM time_series_data;
```

---

### 2. SMA (Simple Moving Average)

Forecasts using a simple moving average of the most recent observations.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `window` | INTEGER | No | 5 | Number of recent observations to average |

**Validation:**
- `window` must be positive

**Use Case:** Smoothing noisy data, short-term trends

**Examples:**

```sql
-- Use default window (5)
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{}) AS forecast
FROM time_series_data;

-- Custom window
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{'window': 10}) AS forecast
FROM time_series_data;

-- Short-term smoothing
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{'window': 3}) AS forecast
FROM time_series_data;
```

---

### 3. SeasonalNaive

Uses the observation from the same season in the previous cycle as the forecast.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | **Yes** | N/A | Length of the seasonal cycle |

**Validation:**
- `seasonal_period` must be positive

**Use Case:** Strong seasonal patterns, seasonal baseline

**Examples:**

```sql
-- Weekly seasonality (7 days)
SELECT TS_FORECAST(date, value, 'SeasonalNaive', 14, MAP{'seasonal_period': 7}) AS forecast
FROM daily_data
GROUP BY product_id;

-- Monthly seasonality (12 months)
SELECT TS_FORECAST(month, value, 'SeasonalNaive', 6, MAP{'seasonal_period': 12}) AS forecast
FROM monthly_data;

-- Hourly data with daily seasonality (24 hours)
SELECT TS_FORECAST(hour, value, 'SeasonalNaive', 48, MAP{'seasonal_period': 24}) AS forecast
FROM hourly_data;
```

---

### 4. RandomWalkWithDrift

Naive forecast with linear trend (drift) component.

**Parameters:** None

**Use Case:** Data with consistent trend, no seasonality

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'RandomWalkWithDrift', 10, MAP{}) AS forecast
FROM trending_data;
```

---

## Exponential Smoothing

### 5. SES (Simple Exponential Smoothing)

Exponential smoothing with fixed smoothing parameter.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `alpha` | DOUBLE | No | 0.3 | Smoothing parameter (0 < alpha < 1) |

**Validation:**
- `alpha` must be between 0 and 1

**Use Case:** Data with no trend or seasonality, level changes

**Examples:**

```sql
-- Default smoothing
SELECT TS_FORECAST(date, value, 'SES', 10, MAP{}) AS forecast
FROM stationary_data;

-- High responsiveness (alpha = 0.8)
SELECT TS_FORECAST(date, value, 'SES', 10, MAP{'alpha': 0.8}) AS forecast
FROM volatile_data;

-- Low responsiveness (alpha = 0.1)
SELECT TS_FORECAST(date, value, 'SES', 10, MAP{'alpha': 0.1}) AS forecast
FROM stable_data;
```

---

### 6. SESOptimized

Simple Exponential Smoothing with automatically optimized alpha parameter.

**Parameters:** None

**Use Case:** SES when optimal alpha is unknown

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'SESOptimized', 10, MAP{}) AS forecast
FROM data;
```

---

### 7. SeasonalES

Exponential smoothing with seasonality (Holt-Winters additive seasonality).

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | **Yes** | N/A | Length of seasonal cycle |
| `alpha` | DOUBLE | No | 0.2 | Level smoothing parameter |
| `gamma` | DOUBLE | No | 0.1 | Seasonal smoothing parameter |

**Validation:**
- `seasonal_period` must be positive
- `alpha`, `gamma` must be between 0 and 1

**Use Case:** Seasonal data without trend

**Example:**
```sql
SELECT TS_FORECAST(date, sales, 'SeasonalES', 12, 
       MAP{'seasonal_period': 7, 'alpha': 0.3, 'gamma': 0.2}) AS forecast
FROM weekly_sales;
```

---

### 8. SeasonalESOptimized

SeasonalES with automatically optimized parameters.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | **Yes** | N/A | Length of seasonal cycle |

**Use Case:** Seasonal data, optimal parameters unknown

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'SeasonalESOptimized', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM monthly_data;
```

---

### 9. SeasonalWindowAverage

Moving average with seasonal adjustment.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | **Yes** | N/A | Length of seasonal cycle |
| `window` | INTEGER | No | 5 | Moving average window size |

**Use Case:** Seasonal data with noise

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'SeasonalWindowAverage', 14, 
       MAP{'seasonal_period': 7, 'window': 3}) AS forecast
FROM noisy_seasonal_data;
```

---

## Trend Models

### 10. Holt

Double exponential smoothing (level + trend).

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `alpha` | DOUBLE | No | 0.3 | Level smoothing parameter |
| `beta` | DOUBLE | No | 0.1 | Trend smoothing parameter |

**Validation:**
- `alpha`, `beta` must be between 0 and 1
- `beta` ≤ `alpha` (typical constraint)

**Use Case:** Data with linear trend, no seasonality

**Examples:**

```sql
-- Default parameters
SELECT TS_FORECAST(date, value, 'Holt', 12, MAP{}) AS forecast
FROM trending_data;

-- Strong trend tracking
SELECT TS_FORECAST(date, value, 'Holt', 12, 
       MAP{'alpha': 0.8, 'beta': 0.3}) AS forecast
FROM accelerating_data;

-- Smooth trend
SELECT TS_FORECAST(date, value, 'Holt', 12, 
       MAP{'alpha': 0.2, 'beta': 0.05}) AS forecast
FROM stable_trend_data;
```

---

### 11. HoltWinters

Triple exponential smoothing (level + trend + seasonality). **Note:** This method internally uses AutoETS for optimal results.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | **Yes** | N/A | Length of seasonal cycle |
| `multiplicative` | INTEGER | No | 0 | Use multiplicative seasonality (1) or additive (0) |
| `alpha` | DOUBLE | No | 0.2 | Level smoothing parameter |
| `beta` | DOUBLE | No | 0.1 | Trend smoothing parameter |
| `gamma` | DOUBLE | No | 0.1 | Seasonal smoothing parameter |

**Validation:**
- `seasonal_period` must be positive
- Parameters must be between 0 and 1

**Use Case:** Data with trend and seasonality

**Example:**
```sql
-- Additive seasonality
SELECT TS_FORECAST(date, sales, 'HoltWinters', 12, 
       MAP{'seasonal_period': 12, 'multiplicative': 0}) AS forecast
FROM monthly_sales;

-- Multiplicative seasonality (for percentage-based seasonality)
SELECT TS_FORECAST(date, sales, 'HoltWinters', 12, 
       MAP{'seasonal_period': 12, 'multiplicative': 1}) AS forecast
FROM retail_sales;
```

---

## Theta Models

### 12. Theta

Classic Theta method with decomposition parameter.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | No | 1 | Length of seasonal cycle (1 = no seasonality) |
| `theta` | DOUBLE | No | 2.0 | Decomposition parameter |

**Validation:**
- `seasonal_period` must be positive
- `theta` typically between 0 and 3

**Use Case:** General forecasting, M3 competition winner

**Examples:**

```sql
-- Non-seasonal Theta
SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{}) AS forecast
FROM non_seasonal_data;

-- Seasonal Theta
SELECT TS_FORECAST(date, value, 'Theta', 12, 
       MAP{'seasonal_period': 12, 'theta': 2.0}) AS forecast
FROM seasonal_data;

-- Custom theta parameter
SELECT TS_FORECAST(date, value, 'Theta', 12, 
       MAP{'theta': 1.5}) AS forecast
FROM data;
```

---

### 13. OptimizedTheta

Theta method with automatically optimized theta parameter.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | No | 1 | Length of seasonal cycle |

**Use Case:** Theta when optimal theta is unknown

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'OptimizedTheta', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM data;
```

---

### 14. DynamicTheta

Theta with dynamic trend component.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | No | 1 | Length of seasonal cycle |
| `theta` | DOUBLE | No | 2.0 | Decomposition parameter |

**Use Case:** Data with changing trends

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'DynamicTheta', 12, 
       MAP{'seasonal_period': 7, 'theta': 2.5}) AS forecast
FROM dynamic_data;
```

---

### 15. DynamicOptimizedTheta

DynamicTheta with optimized parameters.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | No | 1 | Length of seasonal cycle |

**Use Case:** Dynamic trends, optimal parameters unknown

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'DynamicOptimizedTheta', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM complex_data;
```

---

## ARIMA Models

### 16. ARIMA

Box-Jenkins ARIMA model with manual parameter specification.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `p` | INTEGER | No | 1 | Autoregressive order |
| `d` | INTEGER | No | 0 | Differencing order |
| `q` | INTEGER | No | 0 | Moving average order |
| `P` | INTEGER | No | 0 | Seasonal AR order |
| `D` | INTEGER | No | 0 | Seasonal differencing order |
| `Q` | INTEGER | No | 0 | Seasonal MA order |
| `s` | INTEGER | No | 0 | Seasonal period |
| `include_intercept` | INTEGER | No | 1 | Include intercept (1) or not (0) |

**Validation:**
- All orders must be non-negative
- `s` must be positive if seasonal orders > 0

**Use Case:** Expert users, known ARIMA structure

**Examples:**

```sql
-- ARIMA(1,1,1) - simple ARIMA
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 1, 'd': 1, 'q': 1}) AS forecast
FROM data;

-- ARIMA(2,1,2) - higher order
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 2, 'd': 1, 'q': 2}) AS forecast
FROM complex_data;

-- Seasonal ARIMA(1,1,1)(1,1,1)[12]
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 1, 'd': 1, 'q': 1, 'P': 1, 'D': 1, 'Q': 1, 's': 12}) AS forecast
FROM monthly_seasonal_data;

-- AR(3) model
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 3, 'd': 0, 'q': 0}) AS forecast
FROM autoregressive_data;

-- MA(2) model
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 0, 'd': 0, 'q': 2}) AS forecast
FROM moving_average_data;
```

---

### 17. AutoARIMA

Automatically selects optimal ARIMA parameters using stepwise algorithm.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | No | 0 | Seasonal period (0 = non-seasonal) |

**Use Case:** ARIMA when structure unknown, automatic model selection

**Performance:** ~3-10 seconds for 10K rows with early termination

**Examples:**

```sql
-- Non-seasonal AutoARIMA
SELECT TS_FORECAST(date, value, 'AutoARIMA', 12, MAP{}) AS forecast
FROM data;

-- Seasonal AutoARIMA
SELECT TS_FORECAST(date, value, 'AutoARIMA', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM monthly_data;

-- Weekly seasonality
SELECT TS_FORECAST(date, value, 'AutoARIMA', 14, 
       MAP{'seasonal_period': 7}) AS forecast
FROM daily_data;
```

---

## State Space Models (ETS)

### 18. ETS (Error, Trend, Seasonality)

Exponential smoothing state space model with manual component specification.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `error_type` | INTEGER | No | 0 | Error type: 0=Additive, 1=Multiplicative |
| `trend_type` | INTEGER | No | 0 | Trend: 0=None, 1=Additive, 2=Damped Additive, 3=Multiplicative, 4=Damped Multiplicative |
| `season_type` | INTEGER | No | 0 | Season: 0=None, 1=Additive, 2=Multiplicative |
| `season_length` | INTEGER | No | 1 | Seasonal period |
| `alpha` | DOUBLE | No | 0.2 | Level smoothing parameter |
| `beta` | DOUBLE | No | 0.1 | Trend smoothing parameter |
| `gamma` | DOUBLE | No | 0.1 | Seasonal smoothing parameter |
| `phi` | DOUBLE | No | 0.98 | Damping parameter |

**Validation:**
- Smoothing parameters must be between 0 and 1
- `phi` must be between 0 and 1 (typically 0.8-0.98)

**Use Case:** Expert users, specific ETS model required

**Examples:**

```sql
-- ETS(A,N,N) - Simple exponential smoothing
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 0, 'trend_type': 0, 'season_type': 0}) AS forecast
FROM level_data;

-- ETS(A,A,N) - Holt's linear trend
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 0, 'trend_type': 1, 'season_type': 0}) AS forecast
FROM trending_data;

-- ETS(A,Ad,A) - Damped trend with additive seasonality
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 0, 'trend_type': 2, 'season_type': 1, 
           'season_length': 12, 'phi': 0.9}) AS forecast
FROM seasonal_damped_data;

-- ETS(M,N,M) - Multiplicative error and seasonality
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 1, 'trend_type': 0, 'season_type': 2, 
           'season_length': 12}) AS forecast
FROM percentage_seasonal_data;
```

**ETS Model Notation:**
- Error: A=Additive, M=Multiplicative
- Trend: N=None, A=Additive, Ad=Damped Additive, M=Multiplicative, Md=Damped Multiplicative
- Season: N=None, A=Additive, M=Multiplicative

---

### 19. AutoETS

Automatically selects optimal ETS model from 30+ candidates.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `season_length` | INTEGER | No | 1 | Seasonal period (1 = non-seasonal) |
| `model` | VARCHAR | No | 'ZZZ' | Model specification (ZZZ = auto, e.g., 'AAN', 'MAM') |

**Model Specification Format:** `[Error][Trend][Season]`
- Error: A=Additive, M=Multiplicative, Z=Auto
- Trend: N=None, A=Additive, Ad=Damped Additive, M=Multiplicative, Md=Damped Multiplicative, Z=Auto
- Season: N=None, A=Additive, M=Multiplicative, Z=Auto

**Performance:** ~3-4 seconds for 10K rows (3.7× faster with early termination!)

**Use Case:** Automatic model selection, production forecasting

**Examples:**

```sql
-- Full automatic selection
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'season_length': 1}) AS forecast
FROM data;

-- Force additive trend, auto error and season
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'season_length': 12, 'model': 'ZAZ'}) AS forecast
FROM seasonal_data;

-- Force specific model: ETS(M,N,M)
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'season_length': 12, 'model': 'MNM'}) AS forecast
FROM multiplicative_data;

-- Weekly seasonality, auto selection
SELECT TS_FORECAST(date, value, 'AutoETS', 14, 
       MAP{'season_length': 7, 'model': 'ZZZ'}) AS forecast
FROM daily_data;
```

---

## Multiple Seasonality Models

### 20. MFLES (Multiple Fourier and Linear Exponential Smoothing)

Gradient-boosted decomposition for multiple seasonal patterns.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_periods` | INTEGER[] | No | [12] | Array of seasonal periods |
| `n_iterations` | INTEGER | No | 10 | Number of gradient boosting iterations |
| `lr_trend` | DOUBLE | No | 0.3 | Learning rate for trend |
| `lr_season` | DOUBLE | No | 0.5 | Learning rate for seasonality |
| `lr_level` | DOUBLE | No | 0.8 | Learning rate for level |

**Validation:**
- All seasonal periods must be positive
- Learning rates must be between 0 and 1
- n_iterations must be positive

**Use Case:** Complex seasonality (daily + weekly + yearly)

**Examples:**

```sql
-- Single seasonality (yearly)
SELECT TS_FORECAST(date, value, 'MFLES', 12, 
       MAP{'seasonal_periods': [12]}) AS forecast
FROM monthly_data;

-- Multiple seasonality (weekly + yearly in daily data)
SELECT TS_FORECAST(date, value, 'MFLES', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM daily_data;

-- Hourly data (daily + weekly patterns)
SELECT TS_FORECAST(timestamp, value, 'MFLES', 48, 
       MAP{'seasonal_periods': [24, 168]}) AS forecast
FROM hourly_data;

-- Custom learning rates for fine-tuning
SELECT TS_FORECAST(date, value, 'MFLES', 12, 
       MAP{'seasonal_periods': [12], 'n_iterations': 15,
           'lr_trend': 0.4, 'lr_season': 0.6, 'lr_level': 0.9}) AS forecast
FROM data;
```

---

### 21. AutoMFLES

MFLES with automatically optimized learning rates.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_periods` | INTEGER[] | No | [12] | Array of seasonal periods |

**Use Case:** Multiple seasonality, optimal parameters unknown

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'AutoMFLES', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM daily_data;
```

---

### 22. MSTL (Multiple Seasonal-Trend decomposition using Loess)

STL decomposition extended to handle multiple seasonal periods.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_periods` | INTEGER[] | No | [12] | Array of seasonal periods |
| `trend_method` | INTEGER | No | 0 | Trend method: 0=Linear, 1=Loess |
| `seasonal_method` | INTEGER | No | 0 | Seasonal method: 0=Cyclic, 1=Fixed |

**Use Case:** Multiple seasonality with trend decomposition

**Examples:**

```sql
-- Single seasonality
SELECT TS_FORECAST(date, value, 'MSTL', 12, 
       MAP{'seasonal_periods': [12]}) AS forecast
FROM monthly_data;

-- Multiple seasonality
SELECT TS_FORECAST(date, value, 'MSTL', 30, 
       MAP{'seasonal_periods': [7, 365], 'trend_method': 0}) AS forecast
FROM daily_data;
```

---

### 23. AutoMSTL

MSTL with automatically optimized parameters.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_periods` | INTEGER[] | No | [12] | Array of seasonal periods |

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'AutoMSTL', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM daily_data;
```

---

### 24. TBATS (Trigonometric, Box-Cox, ARMA, Trend, Seasonal)

Advanced model for complex seasonality with Box-Cox transformation.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_periods` | INTEGER[] | No | [12] | Array of seasonal periods |
| `use_box_cox` | INTEGER | No | 0 | Use Box-Cox transformation (1) or not (0) |
| `box_cox_lambda` | DOUBLE | No | 1.0 | Box-Cox lambda parameter |
| `use_trend` | INTEGER | No | 1 | Include trend component |
| `use_damped_trend` | INTEGER | No | 0 | Use damped trend |
| `damping_param` | DOUBLE | No | 0.98 | Damping parameter |
| `ar_order` | INTEGER | No | 0 | ARMA autoregressive order |
| `ma_order` | INTEGER | No | 0 | ARMA moving average order |

**Use Case:** Complex patterns with non-linear transformations

**Examples:**

```sql
-- Basic TBATS
SELECT TS_FORECAST(date, value, 'TBATS', 12, 
       MAP{'seasonal_periods': [12]}) AS forecast
FROM monthly_data;

-- TBATS with Box-Cox transformation
SELECT TS_FORECAST(date, value, 'TBATS', 12, 
       MAP{'seasonal_periods': [12], 'use_box_cox': 1, 'box_cox_lambda': 0.5}) AS forecast
FROM non_linear_data;

-- Multiple seasonality with damping
SELECT TS_FORECAST(date, value, 'TBATS', 30, 
       MAP{'seasonal_periods': [7, 365], 'use_damped_trend': 1, 
           'damping_param': 0.95}) AS forecast
FROM daily_data;
```

---

### 25. AutoTBATS

TBATS with automatic parameter selection.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_periods` | INTEGER[] | No | [12] | Array of seasonal periods |

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'AutoTBATS', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM complex_data;
```

---

## Intermittent Demand Models

These models are specifically designed for sparse/intermittent demand patterns (many zeros).

### 26. CrostonClassic

Classic Croston's method for intermittent demand.

**Parameters:** None

**Use Case:** Spare parts demand, slow-moving inventory

**Example:**
```sql
SELECT TS_FORECAST(date, demand, 'CrostonClassic', 12, MAP{}) AS forecast
FROM sparse_demand_data
WHERE product_category = 'spare_parts';
```

---

### 27. CrostonOptimized

Croston's method with optimized smoothing parameters.

**Parameters:** None

**Use Case:** Intermittent demand, optimal parameters unknown

**Example:**
```sql
SELECT TS_FORECAST(date, demand, 'CrostonOptimized', 12, MAP{}) AS forecast
FROM intermittent_data;
```

---

### 28. CrostonSBA

Syntetos-Boylan Approximation variant of Croston's method (less biased).

**Parameters:** None

**Use Case:** Intermittent demand, reduced bias important

**Example:**
```sql
SELECT TS_FORECAST(date, demand, 'CrostonSBA', 12, MAP{}) AS forecast
FROM lumpy_demand_data;
```

---

### 29. ADIDA (Aggregate-Disaggregate Intermittent Demand Approach)

Aggregation-based method for intermittent demand.

**Parameters:** None

**Use Case:** Very sparse demand patterns

**Example:**
```sql
SELECT TS_FORECAST(date, demand, 'ADIDA', 12, MAP{}) AS forecast
FROM very_sparse_data;
```

---

### 30. IMAPA (Intermittent Multiple Aggregation Prediction Algorithm)

Multiple temporal aggregation for intermittent demand.

**Parameters:** None

**Use Case:** Complex intermittent patterns

**Example:**
```sql
SELECT TS_FORECAST(date, demand, 'IMAPA', 12, MAP{}) AS forecast
FROM complex_intermittent_data;
```

---

### 31. TSB (Teunter-Syntetos-Babai)

Advanced intermittent demand method separating demand occurrence and size.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `alpha_d` | DOUBLE | No | 0.1 | Smoothing for demand probability |
| `alpha_p` | DOUBLE | No | 0.1 | Smoothing for demand size |

**Validation:**
- Both alphas must be between 0 and 1

**Use Case:** Intermittent demand with varying sizes

**Examples:**

```sql
-- Default smoothing
SELECT TS_FORECAST(date, demand, 'TSB', 12, MAP{}) AS forecast
FROM intermittent_data;

-- Custom smoothing parameters
SELECT TS_FORECAST(date, demand, 'TSB', 12, 
       MAP{'alpha_d': 0.2, 'alpha_p': 0.15}) AS forecast
FROM lumpy_data;
```

---

## Parameter Tuning Examples

### Comparing Models and Parameters

```sql
-- Compare multiple models for same data
WITH forecasts AS (
    SELECT 'Naive' AS model, TS_FORECAST(date, value, 'Naive', 12, MAP{}) AS fc FROM data
    UNION ALL
    SELECT 'SMA-5', TS_FORECAST(date, value, 'SMA', 12, MAP{'window': 5}) FROM data
    UNION ALL
    SELECT 'SMA-10', TS_FORECAST(date, value, 'SMA', 12, MAP{'window': 10}) FROM data
    UNION ALL
    SELECT 'SES', TS_FORECAST(date, value, 'SES', 12, MAP{'alpha': 0.3}) FROM data
    UNION ALL
    SELECT 'Theta', TS_FORECAST(date, value, 'Theta', 12, MAP{}) FROM data
    UNION ALL
    SELECT 'AutoETS', TS_FORECAST(date, value, 'AutoETS', 12, MAP{'season_length': 1}) FROM data
)
SELECT model, UNNEST(fc.point_forecast) AS forecast, UNNEST(fc.forecast_step) AS step
FROM forecasts;
```

### Grid Search for Optimal Window

```sql
-- Find best SMA window for your data
WITH window_grid AS (
    SELECT 3 AS w UNION ALL SELECT 5 UNION ALL SELECT 7 UNION ALL 
    SELECT 10 UNION ALL SELECT 14 UNION ALL SELECT 21
),
forecasts AS (
    SELECT 
        w,
        TS_FORECAST(date, value, 'SMA', 5, MAP{'window': w}) AS fc
    FROM data, window_grid
    GROUP BY w
)
SELECT w, fc.point_forecast
FROM forecasts;
```

### Seasonal Model Comparison

```sql
-- Compare seasonal models
WITH seasonal_forecasts AS (
    SELECT 'SeasonalNaive' AS model, 
           TS_FORECAST(date, sales, 'SeasonalNaive', 12, MAP{'seasonal_period': 7}) AS fc
    FROM sales_data
    UNION ALL
    SELECT 'SeasonalES',
           TS_FORECAST(date, sales, 'SeasonalES', 12, MAP{'seasonal_period': 7}) AS fc
    FROM sales_data
    UNION ALL
    SELECT 'HoltWinters',
           TS_FORECAST(date, sales, 'HoltWinters', 12, MAP{'seasonal_period': 7}) AS fc
    FROM sales_data
    UNION ALL
    SELECT 'AutoETS',
           TS_FORECAST(date, sales, 'AutoETS', 12, MAP{'season_length': 7}) AS fc
    FROM sales_data
)
SELECT model, UNNEST(fc.point_forecast) AS forecast
FROM seasonal_forecasts;
```

### Parameter Sensitivity Analysis

```sql
-- Test sensitivity to alpha in SES
WITH alpha_values AS (
    SELECT 0.1 AS a UNION ALL SELECT 0.2 UNION ALL SELECT 0.3 UNION ALL
    SELECT 0.5 UNION ALL SELECT 0.7 UNION ALL SELECT 0.9
)
SELECT 
    a AS alpha,
    UNNEST(TS_FORECAST(date, value, 'SES', 10, MAP{'alpha': a}).point_forecast) AS forecast
FROM data, alpha_values
GROUP BY a;
```

---

## Best Practices

### 1. Model Selection Strategy

**Start Simple → Complex:**
```sql
-- Level 1: Baseline
Naive, SMA, SeasonalNaive

-- Level 2: Simple smoothing
SES, SESOptimized, Theta

-- Level 3: Trend models
Holt, OptimizedTheta

-- Level 4: Seasonal models
HoltWinters, SeasonalES, AutoETS

-- Level 5: Complex patterns
AutoARIMA, MSTL, TBATS

-- Level 6: Intermittent
CrostonClassic, TSB, IMAPA
```

### 2. Parameter Tuning Guidelines

**Window Size (SMA, SeasonalWindowAverage):**
- Start with `window ≈ horizon`
- Increase for stable data (less noise)
- Decrease for volatile data (more responsiveness)
- Typical range: 3-21

**Smoothing Parameters (alpha, beta, gamma):**
- alpha (level): 0.1-0.3 for stable, 0.5-0.8 for volatile
- beta (trend): Usually < alpha, typical 0.05-0.15
- gamma (seasonal): Usually smallest, typical 0.05-0.2
- Use optimized variants if unsure

**Seasonal Period:**
- Must match data frequency:
  - Daily data, weekly pattern: 7
  - Daily data, yearly pattern: 365
  - Monthly data, yearly pattern: 12
  - Hourly data, daily pattern: 24
  - Quarterly data, yearly pattern: 4

**ARIMA Orders:**
- Start with AutoARIMA
- If manual tuning needed:
  - p: 0-3 for most data
  - d: 0-2 (usually 0 or 1)
  - q: 0-3 for most data
  - Use ACF/PACF plots (outside DuckDB) to guide

### 3. Data Requirements

**Minimum Data Points:**
- Naive, SMA: 10+ points
- SES, Holt: 20+ points
- Seasonal models: 2× seasonal_period minimum, 3-4× recommended
- ARIMA: 50+ points
- AutoETS: 50+ points, 100+ for seasonal
- Multiple seasonality: 2× max(seasonal_periods)

**Data Quality:**
- Models assume regular timestamps (use mean for interval calculation)
- Models expect sorted data
- No duplicate timestamps allowed
- Missing values: Use NULL (handled by DuckDB aggregation)

### 4. Performance Considerations

**Fast Models (<1ms for 10K rows):**
- Naive, SMA, SeasonalNaive, RandomWalkWithDrift

**Medium Models (10-100ms for 10K rows):**
- SES, Holt, Theta, OptimizedTheta

**Slow Models (1-5s for 10K rows):**
- AutoETS, AutoARIMA, MSTL, TBATS

**Enable Performance Monitoring:**
```bash
export ANOFOX_PERF=1
./duckdb < your_query.sql
# See [PERF] timing breakdown in stderr
```

### 5. Production Deployment

**For Real-time Forecasting:**
```sql
-- Use fast models with GROUP BY parallelization
SELECT 
    product_id,
    TS_FORECAST(date, sales, 'Theta', 30, MAP{'seasonal_period': 7}) AS forecast
FROM sales_history
WHERE date >= CURRENT_DATE - INTERVAL 90 DAY
GROUP BY product_id;
```

**For Batch Forecasting:**
```sql
-- AutoETS provides best automatic results
SELECT 
    product_id,
    region,
    TS_FORECAST(date, sales, 'AutoETS', 30, MAP{'season_length': 7}) AS forecast
FROM sales_history
GROUP BY product_id, region;
```

**For Intermittent Demand:**
```sql
-- Use specialized intermittent models
SELECT 
    sku,
    warehouse,
    TS_FORECAST(date, demand, 'CrostonSBA', 12, MAP{}) AS forecast
FROM inventory_demand
WHERE demand_type = 'intermittent'
GROUP BY sku, warehouse;
```

### 6. Timestamps Control

**Enable (default):**
```sql
-- Real dates in output
TS_FORECAST(date, value, 'Theta', 12, MAP{})
```

**Disable for cleaner output:**
```sql
-- Empty timestamp list (schema consistent)
TS_FORECAST(date, value, 'Theta', 12, MAP{'generate_timestamps': false})
```

**Note:** Mean interval calculation is O(1) and adds negligible overhead (<0.01ms).

---

## Error Handling

The extension validates all parameters before forecasting:

```sql
-- ERROR: window must be positive
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{'window': 0});

-- ERROR: seasonal_period is required
SELECT TS_FORECAST(date, value, 'SeasonalNaive', 7, MAP{});

-- ERROR: alpha must be between 0 and 1
SELECT TS_FORECAST(date, value, 'SES', 7, MAP{'alpha': 1.5});

-- ERROR: Unknown model
SELECT TS_FORECAST(date, value, 'InvalidModel', 7, MAP{});

-- ERROR: seasonal_periods must be array
SELECT TS_FORECAST(date, value, 'MSTL', 7, MAP{'seasonal_periods': 12});

-- CORRECT: seasonal_periods as array
SELECT TS_FORECAST(date, value, 'MSTL', 7, MAP{'seasonal_periods': [12]});
```

---

## Common Use Cases

### Retail Sales Forecasting
```sql
-- Daily sales with weekly seasonality
SELECT 
    store_id,
    TS_FORECAST(date, sales, 'AutoETS', 30, MAP{'season_length': 7}) AS forecast
FROM daily_sales
GROUP BY store_id;
```

### Inventory Planning
```sql
-- Intermittent spare parts demand
SELECT 
    part_number,
    TS_FORECAST(date, demand, 'CrostonSBA', 90, MAP{}) AS forecast
FROM parts_demand
GROUP BY part_number;
```

### Energy Demand
```sql
-- Hourly electricity with daily and weekly patterns
SELECT 
    region,
    TS_FORECAST(hour, kwh, 'MSTL', 168, MAP{'seasonal_periods': [24, 168]}) AS forecast
FROM energy_consumption
GROUP BY region;
```

### Financial Forecasting
```sql
-- Monthly revenue with trend
SELECT 
    department,
    TS_FORECAST(month, revenue, 'Holt', 12, MAP{'alpha': 0.3, 'beta': 0.1}) AS forecast
FROM financial_data
GROUP BY department;
```

### Website Traffic
```sql
-- Hourly visitors with daily pattern
SELECT 
    TS_FORECAST(timestamp, visitors, 'AutoETS', 48, MAP{'season_length': 24}) AS forecast
FROM web_analytics;
```

---

## See Also

- `DEMO_AGGREGATE.sql` - Comprehensive examples of all 31 models
- `QUICKSTART.md` - Getting started guide
- `00_README.md` - General extension documentation
- `51_usage_guide.md` - Advanced usage patterns

---

## Quick Reference Table

| Model | Parameters | Use Case | Speed | Accuracy |
|-------|-----------|----------|-------|----------|
| Naive | None | Baseline | ⚡ Fast | Low |
| SMA | window | Smoothing | ⚡ Fast | Low-Med |
| SeasonalNaive | seasonal_period | Seasonal baseline | ⚡ Fast | Med |
| SES | alpha | Level only | ⚡ Fast | Med |
| Holt | alpha, beta | Trend | ⚡ Fast | Med-High |
| Theta | theta, seasonal_period | General | 🐇 Medium | High |
| OptimizedTheta | seasonal_period | General (auto) | 🐇 Medium | High |
| HoltWinters | seasonal_period, params | Trend + Season | 🐢 Slow | High |
| AutoARIMA | seasonal_period | Auto selection | 🐢 Slow | High |
| AutoETS | season_length, model | Auto selection | 🐢 Slow | Very High |
| MSTL | seasonal_periods | Multiple seasons | 🐢 Slow | High |
| TBATS | seasonal_periods, params | Complex patterns | 🐢 Slow | Very High |
| CrostonSBA | None | Intermittent | ⚡ Fast | High* |
| TSB | alpha_d, alpha_p | Intermittent | ⚡ Fast | High* |

*For intermittent data only

**Speed Legend:**
- ⚡ Fast: <1ms for 10K rows
- 🐇 Medium: 10-100ms for 10K rows
- 🐢 Slow: 1-5s for 10K rows (with early termination optimization)
