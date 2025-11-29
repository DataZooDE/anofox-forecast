-- Create sample historical data
CREATE TABLE historical_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Create real-time data
CREATE TABLE real_time_data AS
SELECT 
    CURRENT_DATE AS date,
    100.0 AS value;

-- Daily forecast quality check
WITH yesterday_forecast AS (
    SELECT point_forecast AS predicted
    FROM anofox_fcst_ts_forecast('historical_data', date, value, 'AutoETS', 1, MAP{})
    LIMIT 1
),
today_actual AS (
    SELECT value AS actual
    FROM real_time_data
    WHERE date = CURRENT_DATE
)
SELECT 
    CURRENT_DATE AS eval_date,
    anofox_fcst_ts_mae(LIST(actual), LIST(predicted)) AS mae,
    anofox_fcst_ts_mape(LIST(actual), LIST(predicted)) AS mape,
    CASE 
        WHEN anofox_fcst_ts_mape(LIST(actual), LIST(predicted)) > 10.0 THEN '⚠️ HIGH ERROR'
        WHEN anofox_fcst_ts_mape(LIST(actual), LIST(predicted)) > 5.0 THEN '⚡ MEDIUM ERROR'
        ELSE '✅ LOW ERROR'
    END AS quality
FROM today_actual, yesterday_forecast;
