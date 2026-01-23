-- Generate Large Test Dataset for Memory/CPU Profiling
-- Creates 1M+ rows with multiple groups for realistic profiling
--
-- Usage: ./build/release/duckdb < benchmark/streaming_api_profiling/generate_test_data.sql

-- Drop existing tables
DROP TABLE IF EXISTS profile_test_data;
DROP TABLE IF EXISTS profile_test_data_small;

-- Create table with 10,000 series x 100 days = 1M rows
CREATE TABLE profile_test_data AS
WITH series_metadata AS (
    SELECT
        series_id,
        100 + (random() * 900)::int AS base_level,
        (random() * 2 - 1) * 0.5 AS trend_slope,
        (random() * 50)::int AS weekly_amplitude,
        (random() * 30)::int AS monthly_amplitude,
        5 + (random() * 15)::int AS noise_level,
        CASE
            WHEN series_id % 10 = 0 THEN 'Category_A'
            WHEN series_id % 10 < 5 THEN 'Category_B'
            ELSE 'Category_C'
        END AS category
    FROM generate_series(1, 10000) AS t(series_id)
),
time_series_data AS (
    SELECT
        m.series_id,
        'Series_' || LPAD(m.series_id::VARCHAR, 5, '0') AS series_name,
        m.category,
        DATE '2023-01-01' + INTERVAL (d) DAY AS date,
        d AS day_index,
        GREATEST(0,
            m.base_level
            + m.trend_slope * d
            + m.weekly_amplitude * SIN(d * 2 * PI() / 7)
            + m.monthly_amplitude * SIN(d * 2 * PI() / 30)
            + (random() * m.noise_level - m.noise_level / 2)
        )::DOUBLE AS value
    FROM series_metadata m
    CROSS JOIN generate_series(0, 99) AS t(d)
)
SELECT
    series_id,
    series_name,
    category,
    date,
    day_index,
    ROUND(value, 2) AS value
FROM time_series_data
ORDER BY series_id, date;

-- Create smaller dataset for quick tests (1000 series x 100 days = 100K rows)
CREATE TABLE profile_test_data_small AS
SELECT * FROM profile_test_data WHERE series_id <= 1000;

-- Show summary
SELECT 'profile_test_data' AS table_name, COUNT(*) AS rows, COUNT(DISTINCT series_id) AS groups FROM profile_test_data
UNION ALL
SELECT 'profile_test_data_small', COUNT(*), COUNT(DISTINCT series_id) FROM profile_test_data_small;
