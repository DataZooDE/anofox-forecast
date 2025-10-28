-- In-Sample Forecasts & Confidence Levels Demo
-- Demonstrates fitted values and custom confidence levels

-- Setup: Create sample data
CREATE OR REPLACE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 20 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS amount
FROM generate_series(0, 89) t(d);

-- ==============================================================================
-- PART 1: Confidence Levels
-- ==============================================================================

SELECT '=== Part 1: Confidence Levels ===' AS section;

-- 1a. Default confidence level (90%)
SELECT '1a. Default 90% confidence intervals' AS example;
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_90,
    ROUND(upper, 2) AS upper_90,
    ROUND(upper - lower, 2) AS width,
    confidence_level
FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 14, 
                 {'seasonal_period': 7})
WHERE forecast_step <= 5;

-- 1b. Custom 95% confidence intervals
SELECT '1b. Custom 95% confidence intervals (wider)' AS example;
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_95,
    ROUND(upper, 2) AS upper_95,
    ROUND(upper - lower, 2) AS width,
    confidence_level
FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 14, 
                 {'seasonal_period': 7, 'confidence_level': 0.95})
WHERE forecast_step <= 5;

-- 1c. Custom 99% confidence intervals
SELECT '1c. Custom 99% confidence intervals (very wide)' AS example;
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_99,
    ROUND(upper, 2) AS upper_99,
    ROUND(upper - lower, 2) AS width,
    confidence_level
FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 14, 
                 {'seasonal_period': 7, 'confidence_level': 0.99})
WHERE forecast_step <= 5;

-- 1d. Compare interval widths across confidence levels
SELECT '1d. Interval width comparison' AS example;
WITH ci_comparison AS (
    SELECT '80% CI' AS level, AVG(upper - lower) AS avg_width, 0.80 AS conf
    FROM TS_FORECAST('sales_data', date, amount, 'ETS', 14, 
                     {'seasonal_period': 7, 'confidence_level': 0.80})
    UNION ALL
    SELECT '90% CI', AVG(upper - lower), 0.90
    FROM TS_FORECAST('sales_data', date, amount, 'ETS', 14, 
                     {'seasonal_period': 7, 'confidence_level': 0.90})
    UNION ALL
    SELECT '95% CI', AVG(upper - lower), 0.95
    FROM TS_FORECAST('sales_data', date, amount, 'ETS', 14, 
                     {'seasonal_period': 7, 'confidence_level': 0.95})
    UNION ALL
    SELECT '99% CI', AVG(upper - lower), 0.99
    FROM TS_FORECAST('sales_data', date, amount, 'ETS', 14, 
                     {'seasonal_period': 7, 'confidence_level': 0.99})
)
SELECT 
    level,
    ROUND(avg_width, 2) AS avg_interval_width,
    ROUND(avg_width / FIRST_VALUE(avg_width) OVER (ORDER BY conf), 2) AS relative_width
FROM ci_comparison
ORDER BY conf;

-- ==============================================================================
-- PART 2: In-Sample Fitted Values
-- ==============================================================================

SELECT '=== Part 2: In-Sample Fitted Values ===' AS section;

-- 2a. Default (no fitted values)
SELECT '2a. Default - no fitted values' AS example;
SELECT 
    LEN(insample_fitted) AS fitted_count,
    'Fitted values disabled by default' AS note
FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 7, 
                 {'seasonal_period': 7})
LIMIT 1;

-- 2b. Enable fitted values
SELECT '2b. Enable fitted values' AS example;
SELECT 
    LEN(insample_fitted) AS fitted_count,
    model_name,
    'Fitted values match training data size' AS note
FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 7, 
                 {'return_insample': true, 'seasonal_period': 7})
LIMIT 1;

-- 2c. Inspect fitted values
SELECT '2c. First 10 fitted values' AS example;
WITH fc AS (
    SELECT * FROM TS_FORECAST('sales_data', date, amount, 'ETS', 7, 
                              {'return_insample': true, 'seasonal_period': 7})
)
SELECT 
    UNNEST(generate_series(1, 10)) AS obs,
    ROUND(UNNEST(insample_fitted[1:10]), 2) AS fitted_value
FROM fc;

-- ==============================================================================
-- PART 3: Model Diagnostics with Residuals
-- ==============================================================================

SELECT '=== Part 3: Residual Analysis ===' AS section;

-- 3a. Compute residuals
SELECT '3a. Residual statistics' AS example;
WITH fc AS (
    SELECT * FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 7, 
                              {'return_insample': true, 'seasonal_period': 7})
),
actuals_fitted AS (
    SELECT 
        s.amount AS actual,
        UNNEST(fc.insample_fitted) AS fitted,
        ROW_NUMBER() OVER () AS idx
    FROM sales_data s, fc
)
SELECT 
    COUNT(*) AS n_observations,
    ROUND(AVG(actual - fitted), 4) AS mean_residual,
    ROUND(STDDEV(actual - fitted), 2) AS std_residual,
    ROUND(MIN(actual - fitted), 2) AS min_residual,
    ROUND(MAX(actual - fitted), 2) AS max_residual
FROM actuals_fitted;

-- 3b. Detect outliers (residuals > 2 std deviations)
SELECT '3b. Outlier detection' AS example;
WITH fc AS (
    SELECT * FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 7, 
                              {'return_insample': true, 'seasonal_period': 7})
),
actuals_fitted AS (
    SELECT 
        s.date,
        s.amount AS actual,
        UNNEST(fc.insample_fitted) AS fitted,
        ROW_NUMBER() OVER () AS idx
    FROM sales_data s, fc
),
residuals AS (
    SELECT 
        date,
        actual,
        ROUND(fitted, 2) AS fitted,
        ROUND(actual - fitted, 2) AS residual,
        STDDEV(actual - fitted) OVER () AS std_residual
    FROM actuals_fitted
)
SELECT 
    date,
    actual,
    fitted,
    residual,
    CASE 
        WHEN ABS(residual) > 2 * std_residual THEN '⚠️ Outlier'
        ELSE '✓'
    END AS flag
FROM residuals
WHERE ABS(residual) > 2 * std_residual
ORDER BY ABS(residual) DESC
LIMIT 10;

-- 3c. Goodness of fit metrics
SELECT '3c. Model fit quality' AS example;
WITH fc AS (
    SELECT * FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 7, 
                              {'return_insample': true, 'seasonal_period': 7})
),
actuals_fitted AS (
    SELECT 
        s.amount AS actual,
        UNNEST(fc.insample_fitted) AS fitted
    FROM sales_data s, fc
)
SELECT 
    'R-squared' AS metric,
    ROUND(TS_R2(LIST(actual), LIST(fitted)), 4) AS value
FROM actuals_fitted
UNION ALL
SELECT 
    'RMSE',
    ROUND(TS_RMSE(LIST(actual), LIST(fitted)), 2)
FROM actuals_fitted
UNION ALL
SELECT 
    'MAE',
    ROUND(TS_MAE(LIST(actual), LIST(fitted)), 2)
FROM actuals_fitted
UNION ALL
SELECT 
    'MAPE (%)',
    ROUND(TS_MAPE(LIST(actual), LIST(fitted)), 2)
FROM actuals_fitted;

-- ==============================================================================
-- PART 4: Multiple Series with GROUP BY
-- ==============================================================================

SELECT '=== Part 4: GROUP BY Examples ===' AS section;

-- Setup multi-series data
CREATE OR REPLACE TABLE multi_sales AS
SELECT 
    'Product_' || (id % 3) AS product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + id * 10 + 20 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS amount
FROM generate_series(0, 59) t(d), generate_series(1, 3) s(id);

-- 4a. Per-series confidence levels
SELECT '4a. Per-series forecasts with confidence levels' AS example;
SELECT 
    product_id,
    COUNT(*) AS forecast_points,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    ROUND(AVG(upper - lower), 2) AS avg_interval_width,
    FIRST(confidence_level) AS conf_level
FROM TS_FORECAST_BY('multi_sales', product_id, date, amount, 'AutoETS', 14,
                    {'confidence_level': 0.95, 'seasonal_period': 7})
GROUP BY product_id
ORDER BY product_id;

-- 4b. Per-series fitted values
SELECT '4b. Per-series fitted value counts' AS example;
SELECT 
    product_id,
    LEN(insample_fitted) AS fitted_count,
    model_name,
    confidence_level
FROM TS_FORECAST_BY('multi_sales', product_id, date, amount, 'AutoETS', 7,
                    {'return_insample': true, 'seasonal_period': 7})
WHERE forecast_step = 1  -- One row per series
ORDER BY product_id;

-- 4c. Per-series goodness of fit
SELECT '4c. Per-series R-squared' AS example;
WITH fc AS (
    SELECT 
        product_id,
        insample_fitted
    FROM TS_FORECAST_BY('multi_sales', product_id, date, amount, 'AutoETS', 7,
                        {'return_insample': true, 'seasonal_period': 7})
    WHERE forecast_step = 1
),
actuals AS (
    SELECT 
        product_id,
        LIST(amount ORDER BY date) AS actual_values
    FROM multi_sales
    GROUP BY product_id
)
SELECT 
    a.product_id,
    ROUND(TS_R2(a.actual_values, fc.insample_fitted), 4) AS r_squared,
    ROUND(TS_MAE(a.actual_values, fc.insample_fitted), 2) AS mae
FROM actuals a
JOIN fc ON a.product_id = fc.product_id
ORDER BY r_squared DESC;

-- ==============================================================================
-- PART 5: Advanced Use Cases
-- ==============================================================================

SELECT '=== Part 5: Advanced Applications ===' AS section;

-- 5a. Model selection based on fit quality
SELECT '5a. Compare models by in-sample performance' AS example;
WITH ets_fc AS (
    SELECT 'ETS' AS model, insample_fitted
    FROM TS_FORECAST('sales_data', date, amount, 'ETS', 7,
                     {'return_insample': true, 'seasonal_period': 7})
    LIMIT 1
),
autoets_fc AS (
    SELECT 'AutoETS' AS model, insample_fitted
    FROM TS_FORECAST('sales_data', date, amount, 'AutoETS', 7,
                     {'return_insample': true, 'seasonal_period': 7})
    LIMIT 1
),
actuals AS (
    SELECT LIST(amount ORDER BY date) AS actual_values
    FROM sales_data
),
model_comparison AS (
    SELECT 
        e.model,
        ROUND(TS_R2(a.actual_values, e.insample_fitted), 4) AS r_squared,
        ROUND(TS_RMSE(a.actual_values, e.insample_fitted), 2) AS rmse
    FROM ets_fc e, actuals a
    UNION ALL
    SELECT 
        ae.model,
        ROUND(TS_R2(a.actual_values, ae.insample_fitted), 4),
        ROUND(TS_RMSE(a.actual_values, ae.insample_fitted), 2)
    FROM autoets_fc ae, actuals a
)
SELECT 
    model,
    r_squared,
    rmse,
    CASE 
        WHEN r_squared = MAX(r_squared) OVER () THEN '✅ Best'
        ELSE ''
    END AS recommendation
FROM model_comparison
ORDER BY r_squared DESC;

-- 5b. Confidence interval coverage analysis
SELECT '5b. Validate interval coverage' AS example;
-- Note: In production, compare with actual out-of-sample data
WITH fc AS (
    SELECT * FROM TS_FORECAST('sales_data', date, amount, 'ETS', 14,
                              {'confidence_level': 0.90, 'seasonal_period': 7})
)
SELECT 
    '90% CI should cover ~90% of actual values' AS interpretation,
    ROUND(AVG(upper - lower), 2) AS avg_width,
    COUNT(*) AS forecast_points
FROM fc;

SELECT '✅ Demo complete!' AS result;
SELECT 'All in-sample forecast and confidence level features demonstrated!' AS summary;

