-- Demonstration of Forecast Timestamps Feature
-- Shows how forecasts now include actual date/datetime values

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

SELECT '═══════════════════════════════════════════════════════════════' AS separator;
SELECT '   FORECAST TIMESTAMPS FEATURE DEMONSTRATION' AS title;
SELECT '═══════════════════════════════════════════════════════════════' AS separator;

-- Example 1: Monthly Sales Data
DROP TABLE IF EXISTS monthly_sales;
CREATE TABLE monthly_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (i - 1) MONTH AS month,
    (1000 + 50 * i + 200 * SIN(2 * PI() * i / 12))::DOUBLE AS sales
FROM (SELECT UNNEST(GENERATE_SERIES(1, 24)) AS i);

SELECT '' AS blank;
SELECT '📊 Example 1: Monthly Sales Data' AS title;
SELECT '─────────────────────────────────────────────────────────────' AS separator;

SELECT 'Training Data (Last 3 months):' AS info;
SELECT month, ROUND(sales, 2) AS sales 
FROM monthly_sales 
ORDER BY month DESC 
LIMIT 3;

SELECT '' AS blank;
SELECT '🔮 12-Month Forecast with Timestamps:' AS info;

WITH forecast AS (
    SELECT TS_FORECAST(month, sales, 'Theta', 12, MAP{}) AS result
    FROM monthly_sales
)
SELECT 
    UNNEST(result.forecast_step) AS step,
    UNNEST(result.forecast_timestamp)::DATE AS forecast_month,
    ROUND(UNNEST(result.point_forecast), 2) AS forecast_sales
FROM forecast;

-- Example 2: Daily Website Traffic
DROP TABLE IF EXISTS daily_traffic;
CREATE TABLE daily_traffic AS
SELECT 
    DATE '2024-10-01' + INTERVAL (i - 1) DAY AS date,
    (5000 + 1000 * SIN(2 * PI() * i / 7) + 500 * (i / 30.0))::DOUBLE AS visitors
FROM (SELECT UNNEST(GENERATE_SERIES(1, 21)) AS i);  -- 3 weeks

SELECT '' AS blank;
SELECT '📊 Example 2: Daily Website Traffic' AS title;
SELECT '─────────────────────────────────────────────────────────────' AS separator;

SELECT 'Training Data (Last 3 days):' AS info;
SELECT date, ROUND(visitors, 0) AS visitors 
FROM daily_traffic 
ORDER BY date DESC 
LIMIT 3;

SELECT '' AS blank;
SELECT '🔮 7-Day Forecast with Timestamps:' AS info;

WITH forecast AS (
    SELECT TS_FORECAST(date, visitors, 'SeasonalNaive', 7, MAP{'seasonal_period': 7}) AS result
    FROM daily_traffic
)
SELECT 
    UNNEST(result.forecast_step) AS step,
    UNNEST(result.forecast_timestamp)::DATE AS forecast_date,
    ROUND(UNNEST(result.point_forecast), 0) AS forecast_visitors
FROM forecast;

-- Example 3: Hourly Temperature
DROP TABLE IF EXISTS hourly_temp;
CREATE TABLE hourly_temp AS
SELECT 
    TIMESTAMP '2024-10-24 00:00:00' + INTERVAL (i - 1) HOUR AS hour,
    (20 + 5 * SIN(2 * PI() * i / 24) + RANDOM() * 2)::DOUBLE AS temperature
FROM (SELECT UNNEST(GENERATE_SERIES(1, 48)) AS i);  -- 2 days

SELECT '' AS blank;
SELECT '📊 Example 3: Hourly Temperature' AS title;
SELECT '─────────────────────────────────────────────────────────────' AS separator;

SELECT 'Training Data (Last 3 hours):' AS info;
SELECT hour, ROUND(temperature, 1) AS temp_celsius 
FROM hourly_temp 
ORDER BY hour DESC 
LIMIT 3;

SELECT '' AS blank;
SELECT '🔮 12-Hour Forecast with Timestamps:' AS info;

WITH forecast AS (
    SELECT TS_FORECAST(hour, temperature, 'SES', 12, MAP{}) AS result
    FROM hourly_temp
)
SELECT 
    UNNEST(result.forecast_step) AS step,
    UNNEST(result.forecast_timestamp) AS forecast_hour,
    ROUND(UNNEST(result.point_forecast), 1) AS forecast_temp
FROM forecast;

SELECT '' AS blank;
SELECT '═══════════════════════════════════════════════════════════════' AS separator;
SELECT '   KEY FEATURES' AS title;
SELECT '═══════════════════════════════════════════════════════════════' AS separator;
SELECT '✅ Timestamps automatically calculated from training data' AS feature
UNION ALL SELECT '✅ Works with monthly, daily, hourly, or any interval' AS feature
UNION ALL SELECT '✅ Handles irregular intervals (uses median)' AS feature
UNION ALL SELECT '✅ forecast_timestamp field added to result struct' AS feature
UNION ALL SELECT '✅ Can cast to DATE or keep as TIMESTAMP' AS feature;

