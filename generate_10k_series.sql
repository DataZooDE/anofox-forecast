-- Generate 10,000 Time Series for Testing
-- Each series has 365 days of daily data with different patterns

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Drop table if exists
DROP TABLE IF EXISTS timeseries_10k;

-- Create table with 10,000 series × 365 days = 3.65M rows
CREATE TABLE timeseries_10k AS
WITH series_metadata AS (
    -- Generate 10,000 unique series with different characteristics
    SELECT 
        series_id,
        -- Random base level (100-1000)
        100 + (random() * 900)::int AS base_level,
        -- Random trend component (-1 to 1 per day)
        (random() * 2 - 1) * 0.5 AS trend_slope,
        -- Random weekly seasonality amplitude (0-50)
        (random() * 50)::int AS weekly_amplitude,
        -- Random monthly seasonality amplitude (0-30)  
        (random() * 30)::int AS monthly_amplitude,
        -- Random noise level (5-20)
        5 + (random() * 15)::int AS noise_level,
        -- Category assignment (for grouping)
        CASE 
            WHEN series_id % 10 = 0 THEN 'Category_A'
            WHEN series_id % 10 < 5 THEN 'Category_B'
            ELSE 'Category_C'
        END AS category,
        -- Region assignment
        CASE 
            WHEN series_id % 4 = 0 THEN 'North'
            WHEN series_id % 4 = 1 THEN 'South'
            WHEN series_id % 4 = 2 THEN 'East'
            ELSE 'West'
        END AS region
    FROM generate_series(1, 10000) AS t(series_id)
),
time_series_data AS (
    SELECT 
        m.series_id,
        'Series_' || LPAD(m.series_id::VARCHAR, 5, '0') AS series_name,
        m.category,
        m.region,
        DATE '2023-01-01' + INTERVAL (d) DAY AS date,
        d AS day_index,
        -- Generate realistic values with:
        -- 1. Base level
        -- 2. Linear trend
        -- 3. Weekly seasonality (period 7)
        -- 4. Monthly seasonality (period 30)  
        -- 5. Random noise
        GREATEST(0, 
            m.base_level 
            + m.trend_slope * d
            + m.weekly_amplitude * SIN(d * 2 * PI() / 7)
            + m.monthly_amplitude * SIN(d * 2 * PI() / 30)
            + (random() * m.noise_level - m.noise_level / 2)
        )::DOUBLE AS value
    FROM series_metadata m
    CROSS JOIN generate_series(0, 364) AS t(d)
)
SELECT 
    series_id,
    series_name,
    category,
    region,
    date,
    day_index,
    ROUND(value, 2) AS value
FROM time_series_data
ORDER BY series_id, date;

-- Show summary statistics
SELECT 
    '========================================' AS separator;

SELECT 
    'Dataset Created Successfully!' AS status;

SELECT 
    '========================================' AS separator;

SELECT 
    'Total Series:' AS metric,
    COUNT(DISTINCT series_id)::VARCHAR AS value
FROM timeseries_10k
UNION ALL
SELECT 
    'Total Data Points:',
    COUNT(*)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT 
    'Date Range:',
    MIN(date)::VARCHAR || ' to ' || MAX(date)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT
    'Avg Value:',
    ROUND(AVG(value), 2)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT
    'Min Value:',
    ROUND(MIN(value), 2)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT
    'Max Value:',
    ROUND(MAX(value), 2)::VARCHAR
FROM timeseries_10k;

SELECT 
    '========================================' AS separator;

-- Show sample by category
SELECT 
    'Distribution by Category:' AS info;

SELECT 
    category,
    COUNT(DISTINCT series_id) AS num_series,
    ROUND(AVG(value), 2) AS avg_value,
    ROUND(MIN(value), 2) AS min_value,
    ROUND(MAX(value), 2) AS max_value
FROM timeseries_10k
GROUP BY category
ORDER BY category;

SELECT 
    '========================================' AS separator;

-- Show sample by region
SELECT 
    'Distribution by Region:' AS info;

SELECT 
    region,
    COUNT(DISTINCT series_id) AS num_series,
    ROUND(AVG(value), 2) AS avg_value,
    ROUND(MIN(value), 2) AS min_value,
    ROUND(MAX(value), 2) AS max_value
FROM timeseries_10k
GROUP BY region
ORDER BY region;

SELECT 
    '========================================' AS separator;

-- Show sample data from first 5 series
SELECT 
    'Sample Data (First 5 Series, First 7 Days):' AS info;

SELECT 
    series_name,
    date,
    value
FROM timeseries_10k
WHERE series_id <= 5 AND day_index < 7
ORDER BY series_id, date;

SELECT 
    '========================================' AS separator;

-- Show table size
SELECT 
    'Table Size Information:' AS info;

SELECT 
    COUNT(*) AS total_rows,
    COUNT(DISTINCT series_id) AS unique_series,
    COUNT(DISTINCT date) AS unique_dates,
    COUNT(DISTINCT category) AS unique_categories,
    COUNT(DISTINCT region) AS unique_regions
FROM timeseries_10k;

SELECT 
    '========================================' AS separator;
SELECT 
    'Ready for forecasting! Try:' AS next_step;
SELECT 
    'SELECT series_name, TS_FORECAST(date, value, ''AutoETS'', 30, {''season_length'': 7}) AS forecast' AS example;
SELECT
    'FROM timeseries_10k GROUP BY series_name LIMIT 100;' AS example;
SELECT 
    '========================================' AS separator;

