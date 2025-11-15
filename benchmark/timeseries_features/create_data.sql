LOAD '../../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';


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
    FROM generate_series(1, 100) AS t(series_id)
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

COPY (
    SELECT 
        series_id AS unique_id,
        date AS ds,
        ROUND(value, 2) AS y
    FROM timeseries_10k
    ORDER BY series_id, date
) TO 'data/time_series_data.parquet' (FORMAT PARQUET);
