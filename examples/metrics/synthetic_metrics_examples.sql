-- =============================================================================
-- Metrics Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates forecast accuracy metrics with the anofox-forecast
-- extension using 5 patterns from basic to advanced.
--
-- Run: ./build/release/duckdb < examples/metrics/synthetic_metrics_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'METRICS EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Basic Point Metrics
-- =============================================================================
-- Use case: Evaluate point forecast accuracy.

.print ''
.print '>>> SECTION 1: Basic Point Metrics'
.print '-----------------------------------------------------------------------------'

-- Create actual vs forecast data
CREATE OR REPLACE TABLE forecast_eval AS
SELECT
    i,
    100.0 + i * 2.0 + (RANDOM() - 0.5) * 10 AS actual,
    100.0 + i * 2.0 + (RANDOM() - 0.5) * 5 AS forecast
FROM generate_series(1, 20) AS t(i);

.print 'Forecast evaluation data summary:'
SELECT
    COUNT(*) AS n_points,
    ROUND(AVG(actual), 2) AS mean_actual,
    ROUND(AVG(forecast), 2) AS mean_forecast,
    ROUND(AVG(actual - forecast), 2) AS mean_error
FROM forecast_eval;

.print ''
.print 'Point forecast metrics:'
SELECT
    ROUND(ts_mae(LIST(actual), LIST(forecast)), 4) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(forecast)), 4) AS rmse,
    ROUND(ts_mape(LIST(actual), LIST(forecast)), 4) AS mape,
    ROUND(ts_smape(LIST(actual), LIST(forecast)), 4) AS smape
FROM forecast_eval;

-- =============================================================================
-- SECTION 2: Understanding Each Metric
-- =============================================================================
-- Use case: Compare metric behavior on different error patterns.

.print ''
.print '>>> SECTION 2: Understanding Each Metric'
.print '-----------------------------------------------------------------------------'

-- Create different error scenarios
.print 'Scenario A: Small uniform errors:'
SELECT
    ts_mae([100.0, 100.0, 100.0]::DOUBLE[], [101.0, 99.0, 100.5]::DOUBLE[]) AS mae,
    ts_rmse([100.0, 100.0, 100.0]::DOUBLE[], [101.0, 99.0, 100.5]::DOUBLE[]) AS rmse;

.print ''
.print 'Scenario B: One large error (outlier):'
SELECT
    ts_mae([100.0, 100.0, 100.0]::DOUBLE[], [100.0, 100.0, 110.0]::DOUBLE[]) AS mae,
    ts_rmse([100.0, 100.0, 100.0]::DOUBLE[], [100.0, 100.0, 110.0]::DOUBLE[]) AS rmse;

.print ''
.print 'Note: RMSE is more sensitive to outliers (10 vs 3.33 for MAE)'

-- =============================================================================
-- SECTION 3: Percentage Metrics (MAPE vs SMAPE)
-- =============================================================================
-- Use case: Scale-independent error measurement.

.print ''
.print '>>> SECTION 3: Percentage Metrics (MAPE vs SMAPE)'
.print '-----------------------------------------------------------------------------'

-- MAPE vs SMAPE behavior
.print 'Standard case (values around 100):'
SELECT
    ROUND(ts_mape([100.0, 110.0, 120.0]::DOUBLE[], [105.0, 108.0, 125.0]::DOUBLE[]), 2) AS mape,
    ROUND(ts_smape([100.0, 110.0, 120.0]::DOUBLE[], [105.0, 108.0, 125.0]::DOUBLE[]), 2) AS smape;

.print ''
.print 'Small actuals (MAPE can explode):'
SELECT
    ROUND(ts_mape([1.0, 2.0, 3.0]::DOUBLE[], [2.0, 3.0, 4.0]::DOUBLE[]), 2) AS mape,
    ROUND(ts_smape([1.0, 2.0, 3.0]::DOUBLE[], [2.0, 3.0, 4.0]::DOUBLE[]), 2) AS smape;

.print ''
.print 'Note: SMAPE is bounded [0, 200], MAPE can exceed 100%'

-- =============================================================================
-- SECTION 4: Quantile and Interval Metrics
-- =============================================================================
-- Use case: Evaluate probabilistic forecasts.

.print ''
.print '>>> SECTION 4: Quantile and Interval Metrics'
.print '-----------------------------------------------------------------------------'

-- Create quantile forecast data
CREATE OR REPLACE TABLE quantile_eval AS
SELECT
    i,
    100.0 + (RANDOM() - 0.5) * 20 AS actual,
    90.0 + (RANDOM() - 0.5) * 5 AS q10,
    95.0 + (RANDOM() - 0.5) * 5 AS q25,
    100.0 + (RANDOM() - 0.5) * 5 AS q50,
    105.0 + (RANDOM() - 0.5) * 5 AS q75,
    110.0 + (RANDOM() - 0.5) * 5 AS q90
FROM generate_series(1, 30) AS t(i);

.print 'Quantile loss for different quantiles:'
SELECT
    ROUND(ts_quantile_loss(LIST(actual), LIST(q10), 0.1), 4) AS ql_10,
    ROUND(ts_quantile_loss(LIST(actual), LIST(q25), 0.25), 4) AS ql_25,
    ROUND(ts_quantile_loss(LIST(actual), LIST(q50), 0.5), 4) AS ql_50,
    ROUND(ts_quantile_loss(LIST(actual), LIST(q75), 0.75), 4) AS ql_75,
    ROUND(ts_quantile_loss(LIST(actual), LIST(q90), 0.9), 4) AS ql_90
FROM quantile_eval;

.print ''
.print 'Mean interval width (90% prediction interval):'
SELECT
    ROUND(ts_mean_interval_width(LIST(q10), LIST(q90)), 2) AS mean_width_90
FROM quantile_eval;

-- =============================================================================
-- SECTION 5: Comparing Models
-- =============================================================================
-- Use case: Evaluate multiple forecast methods.

.print ''
.print '>>> SECTION 5: Comparing Models'
.print '-----------------------------------------------------------------------------'

-- Create multi-model comparison
CREATE OR REPLACE TABLE model_comparison AS
SELECT
    i,
    100.0 + i * 2.0 + 10.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 5 AS actual,
    100.0 + i * 2.0 AS naive_forecast,  -- Trend only
    100.0 + i * 2.0 + 10.0 * SIN(2 * PI() * i / 7.0) AS seasonal_forecast  -- With seasonality
FROM generate_series(1, 28) AS t(i);

.print 'Model comparison (trend with weekly seasonality):'
.print ''
.print 'Naive model (trend only):'
SELECT
    ROUND(ts_mae(LIST(actual), LIST(naive_forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(naive_forecast)), 2) AS rmse,
    ROUND(ts_mape(LIST(actual), LIST(naive_forecast)), 2) AS mape
FROM model_comparison;

.print ''
.print 'Seasonal model (with weekly pattern):'
SELECT
    ROUND(ts_mae(LIST(actual), LIST(seasonal_forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(seasonal_forecast)), 2) AS rmse,
    ROUND(ts_mape(LIST(actual), LIST(seasonal_forecast)), 2) AS mape
FROM model_comparison;

.print ''
.print 'The seasonal model should have lower errors.'

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS forecast_eval;
DROP TABLE IF EXISTS quantile_eval;
DROP TABLE IF EXISTS model_comparison;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'METRICS EXAMPLES COMPLETE'
.print '============================================================================='
