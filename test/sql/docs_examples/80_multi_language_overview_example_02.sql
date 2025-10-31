-- queries.sql

-- Get quality stats
-- name: get_quality_stats
SELECT * FROM TS_STATS('sales', product_id, date, amount);

-- Prepare data
-- name: prepare_data
CREATE TABLE sales_prepared AS
WITH filled AS (SELECT * FROM TS_FILL_GAPS('sales', product_id, date, amount))
SELECT * FROM TS_DROP_CONSTANT('filled', product_id, amount);

-- Generate forecast
-- name: forecast_autoets
SELECT * FROM TS_FORECAST_BY('sales_prepared', product_id, date, amount,
                             'AutoETS', 28, {'seasonal_period': 7});

-- Evaluate metrics
-- name: evaluate_forecasts
WITH joined AS (...)
SELECT product_id, TS_MAE(...), TS_RMSE(...) FROM joined;
