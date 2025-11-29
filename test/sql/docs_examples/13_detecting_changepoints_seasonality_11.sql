-- Create sample large dataset
CREATE TABLE large_dataset AS
SELECT 
    series_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + (ROW_NUMBER() OVER (PARTITION BY series_id ORDER BY series_id) % 10 + 1) * 10 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d)
CROSS JOIN (SELECT generate_series(1, 10) AS series_id) s;

-- This processes 10,000 series in parallel across all cores
SELECT 
    series_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
FROM anofox_fcst_ts_detect_changepoints_by('large_dataset', series_id, date, value, MAP{})
GROUP BY series_id;

-- Performance: ~10-20ms per series × (num_series / num_cores)
