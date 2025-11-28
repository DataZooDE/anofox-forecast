-- Create sample data
CREATE TABLE data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);  -- 90 days of data

WITH cp AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
),
segments AS (
    SELECT 
        *,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (ORDER BY date_col) AS segment_id
    FROM cp
)
SELECT 
    segment_id,
    COUNT(*) AS length,
    AVG(value_col) AS avg_value
FROM segments
GROUP BY segment_id;
