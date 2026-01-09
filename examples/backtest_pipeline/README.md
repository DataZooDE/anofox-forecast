# Backtest Pipeline Example

This example demonstrates an end-to-end time series forecasting pipeline with backtesting, feature engineering, and model evaluation using the anofox_forecast DuckDB extension.

## Overview

The pipeline covers:
- Creating train/test splits for cross-validation
- Feature engineering (calendar features, changepoint detection)
- Handling known vs unknown features
- Forecasting with MFLES and ARIMAX models
- Model evaluation on held-out test sets

## Files

Run the files in order:

| File | Description |
|------|-------------|
| `01_sample_data.sql` | Creates sample retail dataset (36 months, 2 categories) |
| `02_ts_cv_split_demo.sql` | Demonstrates `ts_cv_split()` for cross-validation folds |
| `03_changepoint_features.sql` | Changepoint detection using BOCPD algorithm |
| `04_calendar_features.sql` | Calendar features (month, season, cyclical encoding) |
| `05_mfles_model.sql` | MFLES and ARIMAX forecasting with exogenous variables |
| `06_known_unknown_features.sql` | Handling unknown features with `ts_fill_unknown()` |

## Quick Start

```sql
-- Load extension
LOAD anofox_forecast;

-- Run sample data creation
.read examples/backtest_pipeline/01_sample_data.sql

-- Run any subsequent example
.read examples/backtest_pipeline/02_ts_cv_split_demo.sql
```

Or run all files together:

```bash
cat examples/backtest_pipeline/01_sample_data.sql \
    examples/backtest_pipeline/02_ts_cv_split_demo.sql \
    | ./build/release/duckdb :memory:
```

## Sample Dataset

The sample dataset (`backtest_sample`) simulates monthly retail sales:

- **Categories**: Electronics, Apparel
- **Period**: Jan 2022 - Dec 2024 (36 months)
- **Features**:
  - `sales`: Target variable
  - `promotion`: Known feature (planned promotions)
  - `temperature`: Unknown feature (weather)
- **Patterns**: Seasonality, trend, changepoint at month 18

## Key Concepts

### Known vs Unknown Features

| Type | Examples | Handling |
|------|----------|----------|
| **Known** | Calendar, planned promotions | Available for future dates |
| **Unknown** | Weather, actual demand | Must fill for backtesting |

### Fill Strategies for Unknown Features

```sql
-- Forward fill from last known value
ts_fill_unknown(..., strategy := 'last_value')

-- Set to NULL (model handles missing)
ts_fill_unknown(..., strategy := 'null')

-- Use constant value (e.g., historical mean)
ts_fill_unknown(..., strategy := 'default', fill_value := 60.0)
```

### Cross-Validation Window Types

```sql
-- Expanding window (default): train grows with each fold
ts_cv_split(..., window_type := 'expanding')

-- Fixed window: train size stays constant
ts_cv_split(..., window_type := 'fixed', min_train_size := 12)
```

## Models Supporting Exogenous Variables

| Base Model | With Exog |
|------------|-----------|
| ARIMA | ARIMAX |
| Theta | ThetaX |
| MFLES | MFLESX* |

*MFLESX currently has numerical issues; use ARIMAX instead.

## Related Functions

- `ts_cv_generate_folds()` - Auto-generate fold boundaries
- `ts_cv_split()` - Create train/test splits
- `ts_cv_split_folds()` - View fold date ranges
- `ts_fill_unknown()` - Fill unknown feature values
- `ts_mark_unknown()` - Mark rows as known/unknown
- `ts_detect_changepoints_by()` - Changepoint detection
- `ts_forecast_by()` - Grouped forecasting
- `_ts_forecast_exog()` - Forecasting with exogenous variables
