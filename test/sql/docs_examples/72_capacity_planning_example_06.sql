-- Forecast traffic and determine server requirements
WITH traffic_forecast AS (
    SELECT * FROM TS_FORECAST('hourly_requests', timestamp, request_count,
                              'AutoMSTL', 168,
                              {'seasonal_periods': [24, 168]})  -- Daily + weekly
),
server_requirements AS (
    SELECT 
        DATE_TRUNC('day', date_col) AS date,
        MAX(upper) AS peak_requests,
        AVG(point_forecast) AS avg_requests,
        -- Assume: 1 server handles 10K req/hour
        CEIL(MAX(upper) / 10000.0) AS servers_needed_peak,
        CEIL(AVG(point_forecast) / 10000.0) AS servers_needed_avg
    FROM traffic_forecast
    GROUP BY date
)
SELECT 
    date,
    peak_requests,
    servers_needed_peak,
    servers_needed_avg,
    servers_needed_peak - servers_needed_avg AS auto_scaling_range,
    ROUND(servers_needed_avg * 720 + (servers_needed_peak - servers_needed_avg) * 200, 2) AS estimated_cost
    -- Assuming $720/month base + $200/month for auto-scaling
FROM server_requirements
WHERE date >= CURRENT_DATE
ORDER BY date;
