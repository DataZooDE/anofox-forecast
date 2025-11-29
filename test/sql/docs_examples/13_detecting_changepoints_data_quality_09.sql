-- Create sample process data
CREATE TABLE process_data AS
SELECT 
    machine_id,
    TIMESTAMP '2024-01-01 00:00:00' + INTERVAL (h) HOUR AS timestamp,
    50 + (ROW_NUMBER() OVER (PARTITION BY machine_id ORDER BY machine_id) % 3 + 1) * 10 + 10 * SIN(2 * PI() * h / 24) + (RANDOM() * 5) AS measurement
FROM generate_series(0, 167) t(h)
CROSS JOIN (VALUES ('M001'), ('M002'), ('M003')) machines(machine_id);

-- Monitor manufacturing process for shifts
WITH process_changes AS (
    SELECT 
        machine_id,
        MAX(date_col) FILTER (WHERE is_changepoint) AS last_shift,
        COUNT(*) FILTER (WHERE is_changepoint) AS total_shifts
    FROM anofox_fcst_ts_detect_changepoints_by('process_data', machine_id, timestamp, measurement, MAP{})
    GROUP BY machine_id
)
SELECT 
    machine_id,
    last_shift,
    total_shifts,
    CASE 
        WHEN total_shifts > 5 THEN '🔴 UNSTABLE'
        WHEN total_shifts > 2 THEN '🟡 MONITOR'
        ELSE '🟢 STABLE'
    END AS status
FROM process_changes
ORDER BY total_shifts DESC;
