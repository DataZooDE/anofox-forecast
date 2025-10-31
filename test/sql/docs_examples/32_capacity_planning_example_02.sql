-- Forecast call volume and determine staffing needs
WITH hourly_calls AS (
    SELECT 
        DATE_TRUNC('hour', call_timestamp) AS hour,
        COUNT(*) AS call_count
    FROM call_center_log
    WHERE call_timestamp >= CURRENT_DATE - INTERVAL '90 days'
    GROUP BY hour
),
call_forecast AS (
    SELECT * FROM TS_FORECAST('hourly_calls', hour, call_count,
                              'AutoMSTL', 168,  -- 1 week ahead (hourly)
                              {'seasonal_periods': [24, 168]})  -- Daily + weekly
),
staffing_needs AS (
    SELECT 
        date_col AS hour,
        point_forecast AS expected_calls,
        upper AS calls_95ci,
        -- Assume: 1 agent handles 6 calls/hour, 80% occupancy target
        CEIL(upper / (6 * 0.80)) AS agents_required,
        EXTRACT(HOUR FROM date_col) AS hour_of_day,
        EXTRACT(DOW FROM date_col) AS day_of_week
    FROM call_forecast
)
SELECT 
    day_of_week,
    hour_of_day,
    AVG(agents_required) AS avg_agents_needed,
    MAX(agents_required) AS peak_agents_needed
FROM staffing_needs
WHERE date_col >= CURRENT_DATE AND date_col < CURRENT_DATE + INTERVAL '7 days'
GROUP BY day_of_week, hour_of_day
ORDER BY day_of_week, hour_of_day;
