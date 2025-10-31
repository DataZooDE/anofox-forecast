-- Step 1: Analyze data quality
WITH quality AS (
    SELECT * FROM TS_QUALITY_REPORT('raw_data', 'date', 'value', 'series_id')
),
-- Step 2: Fill gaps and nulls
cleaned AS (
    SELECT * FROM TS_FILL_GAPS('raw_data', 'date', 'value', 'series_id', 'DAY', 'linear')
),
filled AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD(cleaned, 'date', 'value', 'series_id')
),
-- Step 3: Remove outliers
prepared AS (
    SELECT * FROM TS_REMOVE_OUTLIERS(filled, 'date', 'value', 'series_id', 3.0, 'cap')
)
-- Step 4: Forecast
SELECT 
    series_id,
    result.*
FROM ts_forecast_by(
    prepared,
    'date',
    'value',
    'series_id',
    'AutoETS',
    12,
    {'return_insample': true}
) AS result;
