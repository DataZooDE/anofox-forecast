-- Test forecast timestamps feature
-- Shows how forecast_timestamp field provides actual dates for future predictions

-- Load extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Create AirPassengers dataset with monthly dates
DROP TABLE IF EXISTS air_passengers;
CREATE TABLE air_passengers AS
SELECT 
    DATE '1949-01-01' + INTERVAL (i - 1) MONTH AS date,
    passengers
FROM (
    SELECT UNNEST(GENERATE_SERIES(1, 132)) AS i,
           UNNEST([
               112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118,
               115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140,
               145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166,
               171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194,
               196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201,
               204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229,
               242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278,
               284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306,
               315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336,
               340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337,
               360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405,
               417, 391, 419, 461, 472, 535, 622, 606, 508, 461, 390, 432
           ]::DOUBLE[]) AS passengers
);

SELECT '=== AirPassengers Data Sample (Last 5 months) ===' AS title;
SELECT date, passengers 
FROM air_passengers 
ORDER BY date DESC 
LIMIT 5;

SELECT '=== Generate 12-month Forecast with Theta ===' AS title;

-- Generate forecast
WITH forecast AS (
    SELECT TS_FORECAST(date, passengers, 'Theta', 12, STRUCT_PACK(seasonal_period := 12)) AS result
    FROM air_passengers
)
SELECT 
    UNNEST(result.forecast_step) AS step,
    UNNEST(result.forecast_timestamp) AS forecast_date,
    UNNEST(result.point_forecast) AS forecast_value,
    UNNEST(result.lower_95) AS lower_bound,
    UNNEST(result.upper_95) AS upper_bound
FROM forecast;

SELECT '=== Verify Timestamp Continuity ===' AS title;
SELECT 
    'Last training date' AS description,
    MAX(date) AS last_date
FROM air_passengers
UNION ALL
SELECT 
    'First forecast date' AS description,
    (UNNEST(result.forecast_timestamp))[1] AS first_forecast_date
FROM (
    SELECT TS_FORECAST(date, passengers, 'Theta', 12, STRUCT_PACK(seasonal_period := 12)) AS result
    FROM air_passengers
);

SELECT '=== Compare Different Seasonal Periods ===' AS title;

-- Daily data example
DROP TABLE IF EXISTS daily_sales;
CREATE TABLE daily_sales AS
SELECT 
    DATE '2024-01-01' + INTERVAL (i - 1) DAY AS date,
    100 + 10 * SIN(2 * PI() * i / 7) + RANDOM() * 5 AS sales
FROM (SELECT UNNEST(GENERATE_SERIES(1, 60)) AS i);

SELECT '--- Daily Data (Last 5 days) ---' AS title;
SELECT date, ROUND(sales, 2) AS sales 
FROM daily_sales 
ORDER BY date DESC 
LIMIT 5;

SELECT '--- 7-day Forecast (Daily Timestamps) ---' AS title;
WITH forecast AS (
    SELECT TS_FORECAST(date, sales, 'SeasonalNaive', 7, STRUCT_PACK(seasonal_period := 7)) AS result
    FROM daily_sales
)
SELECT 
    UNNEST(result.forecast_step) AS step,
    UNNEST(result.forecast_timestamp)::DATE AS forecast_date,
    ROUND(UNNEST(result.point_forecast), 2) AS forecast_value
FROM forecast;

SELECT '=== Hourly Data Example ===' AS title;

-- Hourly data example
DROP TABLE IF EXISTS hourly_traffic;
CREATE TABLE hourly_traffic AS
SELECT 
    TIMESTAMP '2024-10-20 00:00:00' + INTERVAL (i - 1) HOUR AS timestamp,
    50 + 20 * SIN(2 * PI() * i / 24) + RANDOM() * 10 AS traffic
FROM (SELECT UNNEST(GENERATE_SERIES(1, 72)) AS i);  -- 3 days

SELECT '--- Hourly Data (Last 5 hours) ---' AS title;
SELECT timestamp, ROUND(traffic, 2) AS traffic 
FROM hourly_traffic 
ORDER BY timestamp DESC 
LIMIT 5;

SELECT '--- 24-hour Forecast (Hourly Timestamps) ---' AS title;
WITH forecast AS (
    SELECT TS_FORECAST(timestamp, traffic, 'SeasonalNaive', 24, STRUCT_PACK(seasonal_period := 24)) AS result
    FROM hourly_traffic
)
SELECT 
    UNNEST(result.forecast_step) AS step,
    UNNEST(result.forecast_timestamp) AS forecast_timestamp,
    ROUND(UNNEST(result.point_forecast), 2) AS forecast_value
FROM forecast
LIMIT 10;  -- Show first 10 hours

SELECT '=== Summary ===' AS title;
SELECT 'Forecast timestamps are now automatically generated!' AS message
UNION ALL SELECT 'They match the interval of your training data' AS message
UNION ALL SELECT 'Works with: monthly, daily, hourly, or any regular interval' AS message;

