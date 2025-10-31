-- This processes 10,000 series in parallel across all cores
SELECT 
    group_col AS series_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
FROM TS_DETECT_CHANGEPOINTS_BY('large_dataset', series_id, date, value, MAP{})
GROUP BY group_col;

-- Performance: ~10-20ms per series × (num_series / num_cores)
