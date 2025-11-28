-- Create raw data
CREATE TABLE raw_data AS
SELECT 
    series_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + series_id * 10 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10)
    END AS value
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) series(series_id);

-- Create stats for quality report
CREATE TABLE raw_stats AS
SELECT * FROM TS_STATS('raw_data', series_id, date, value, '1d');

-- Step 1: Analyze data quality
SELECT * FROM TS_QUALITY_REPORT('raw_stats', 30)
LIMIT 5;

-- Step 2: Fill gaps and nulls
CREATE TEMP TABLE cleaned AS
SELECT 
    group_col AS series_id,
    date_col AS date,
    value_col AS value
FROM TS_FILL_GAPS('raw_data', series_id, date, value, '1d');

CREATE TEMP TABLE filled AS
SELECT 
    series_id,
    date,
    value_col AS value
FROM TS_FILL_NULLS_FORWARD('cleaned', series_id, date, value);

-- Step 3: Forecast
SELECT 
    group_col AS series_id,
    *
FROM TS_FORECAST_BY(
    'filled',
    series_id,
    date,
    value,
    'AutoETS',
    12,
    MAP{'return_insample': true}
)
LIMIT 10;
