-- Find best SMA window for your data
WITH window_grid AS (
    SELECT 3 AS w UNION ALL SELECT 5 UNION ALL SELECT 7 UNION ALL 
    SELECT 10 UNION ALL SELECT 14 UNION ALL SELECT 21
),
forecasts AS (
    SELECT 
        w,
        TS_FORECAST(date, value, 'SMA', 5, MAP{'window': w}) AS fc
    FROM data, window_grid
    GROUP BY w
)
SELECT w, fc.point_forecast
FROM forecasts;
