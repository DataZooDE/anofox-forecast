-- Monitor manufacturing process for shifts
WITH process_changes AS (
    SELECT 
        group_col AS machine_id,
        MAX(date_col) FILTER (WHERE is_changepoint) AS last_shift,
        COUNT(*) FILTER (WHERE is_changepoint) AS total_shifts
    FROM TS_DETECT_CHANGEPOINTS_BY('process_data', machine_id, timestamp, measurement, MAP{})
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
